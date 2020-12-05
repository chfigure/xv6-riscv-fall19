#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// Buddy allocator

static int nsizes;     // the number of entries in bd_sizes array

#define LEAF_SIZE     16                         // The smallest block size
#define MAXSIZE       (nsizes-1)                 // Largest index in bd_sizes array
#define BLK_SIZE(k)   ((1L << (k)) * LEAF_SIZE)  // Size of block at size k
#define HEAP_SIZE     BLK_SIZE(MAXSIZE) 
#define NBLK(k)       (1 << (MAXSIZE-k))         // Number of block at size k
#define ROUNDUP(n,sz) (((((n)-1)/(sz))+1)*(sz))  // Round up to the next multiple of sz

typedef struct list Bd_list;

// The allocator has sz_info for each size k. Each sz_info has a free
// list, an array alloc to keep track which blocks have been
// allocated, and an split array to to keep track which blocks have
// been split.  The arrays are of type char (which is 1 byte), but the
// allocator uses 1 bit per block (thus, one char records the info of
// 8 blocks).
struct sz_info {
  Bd_list free;
  char *alloc;  //指向每一层的每一块的分配情况
  //我们需要优化alloc的表示方法，让它的空间减半（buddy块之间用异或表示是否被占用）
  //一块占用的是1个bit而不是一个字节！！
  char *split;  //指向对应层的每一块的分裂状况（注意，第0层就1块，它的spilt为NULL）
};
typedef struct sz_info Sz_info;

static Sz_info *bd_sizes; 
static void *bd_base;   // start address of memory managed by the buddy allocator
static struct spinlock lock;

// Return 1 if bit at position index in array is set to 1
int bit_isset(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  return (b & m) == m;
}

// Set bit at position index in array to 1
void bit_set(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  array[index/8] = (b | m);
}

// Clear bit at position index in array
void bit_clear(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  array[index/8] = (b & ~m);
}

void bit_xor(char* array, int index){  //异或规则下的比特改变
//需要达成的效果：根据原有比特，进行更改，也就是异或操作
//请注意，下标从0开始计算！！！
  index >>= 1;    //index右移1位（表示除2），得到需要更改的Bit序号
  char m = 1 << (index % 8);  //把对应的1移到对应位置(index % 8取出的是对应比特在对应字节里的偏移量)
  array[index / 8] ^= m;  //对应比特位与m异或（index/8，求出的是该比特的字节序号）
}

int bit_xorset(char* array, int index){ //与上面的函数配套，表示是否按照此规则改变过
  index >>= 1;    //index右移1位（表示除2）
  char a = array[index / 8];  //数组原来的比特值
  char m = 1 << (index % 8);  //变换后的值
  return (a & m) == m;  //如果对应比特位是一致的，就返回1，否则返回0
}

// Print a bit vector as a list of ranges of 1 bits
void
bd_print_vector(char *vector, int len) {
  int last, lb;
  
  last = 1;
  lb = 0;
  for (int b = 0; b < len; b++) {
    if (last == bit_isset(vector, b))
      continue;
    if(last == 1)
      printf(" [%d, %d)", lb, b);
    lb = b;
    last = bit_isset(vector, b);
  }
  if(lb == 0 || last == 1) {
    printf(" [%d, %d)", lb, len);
  }
  printf("\n");
}

// Print buddy's data structures
void
bd_print() {
  for (int k = 0; k < nsizes; k++) {
    printf("size %d (blksz %d nblk %d): free list: ", k, BLK_SIZE(k), NBLK(k));
    lst_print(&bd_sizes[k].free);
    printf("  alloc:");
    bd_print_vector(bd_sizes[k].alloc, NBLK(k));
    if(k > 0) {
      printf("  split:");
      bd_print_vector(bd_sizes[k].split, NBLK(k));
    }
  }
}

// What is the first k such that 2^k >= n?
int
firstk(uint64 n) {
  int k = 0;
  uint64 size = LEAF_SIZE;

  while (size < n) {
    k++;
    size *= 2;
  }
  return k;
}

// Compute the block index for address p at size k
int
blk_index(int k, char *p) {
  int n = p - (char *) bd_base;
  return n / BLK_SIZE(k);
}

// Convert a block index at size k back into an address
void *addr(int k, int bi) {
  int n = bi * BLK_SIZE(k);
  return (char *) bd_base + n;
}

// allocate nbytes, but malloc won't return anything smaller than LEAF_SIZE
//动态分配内存，传入要分配的字节数，但不能少于LEAF_SIZE=16字节
void *
bd_malloc(uint64 nbytes)
{
  int fk, k;

  acquire(&lock);

  // Find a free block >= nbytes, starting with smallest k possible
  fk = firstk(nbytes);  //能满足内存要求的最小的k值
  for (k = fk; k < nsizes; k++) { //一层层往大的找
    if(!lst_empty(&bd_sizes[k].free))
      break;
  }
  if(k >= nsizes) { // No free blocks?
    release(&lock);
    return 0;
  }

  // Found a block; pop it and potentially split it.
  char *p = lst_pop(&bd_sizes[k].free); //从链表里面拿一块空闲的内存出来
  //以下alloc的逻辑需要更改
  // bit_set(bd_sizes[k].alloc, blk_index(k, p));
  bit_xor(bd_sizes[k].alloc, blk_index(k, p));
  for(; k > fk; k--) {
    // split a block at size k and mark one half allocated at size k-1
    // and put the buddy on the free list at size k-1
    char *q = p + BLK_SIZE(k-1);   // p's buddy
    bit_set(bd_sizes[k].split, blk_index(k, p));
    // bit_set(bd_sizes[k-1].alloc, blk_index(k-1, p));
    bit_xor(bd_sizes[k-1].alloc, blk_index(k-1, p));
    lst_push(&bd_sizes[k-1].free, q);
  }
  release(&lock);

  return p;
}

// Find the size of the block that p points to.
int
size(char *p) {
  for (int k = 0; k < nsizes; k++) {
    if(bit_isset(bd_sizes[k+1].split, blk_index(k+1, p))) {
      return k;
    }
  }
  return 0;
}

// Free memory pointed to by p, which was earlier allocated using
// bd_malloc.
//释放一块内存（与bd_malloc成对出现）
void  
bd_free(void *p) {
  void *q;
  int k;

  acquire(&lock);
  for (k = size(p); k < MAXSIZE; k++) {
    int bi = blk_index(k, p);
    int buddy = (bi % 2 == 0) ? bi+1 : bi-1;
    //下面的逻辑需要更改
    // bit_clear(bd_sizes[k].alloc, bi);  // free p at size k
    bit_xor(bd_sizes[k].alloc, bi);  // free p at size k
    // if (bit_isset(bd_sizes[k].alloc, buddy)) {  // is buddy allocated?
    if (bit_xorset(bd_sizes[k].alloc, bi)) {  // is buddy allocated?
    //检测到对应alloc位为1，说明1空闲1占用。由于bi刚刚释放，所以这里就能检测到buddy是否为空
      break;   // break out of loop
    }
    // budy is free; merge with buddy
    q = addr(k, buddy);
    lst_remove(q);    // remove buddy from free list
    if(buddy % 2 == 0) {
      p = q;
    }
    // at size k+1, mark that the merged buddy pair isn't split
    // anymore
    bit_clear(bd_sizes[k+1].split, blk_index(k+1, p));
  }
  lst_push(&bd_sizes[k].free, p);
  release(&lock);
}

// Compute the first block at size k that doesn't contain p
int
blk_index_next(int k, char *p) {
  int n = (p - (char *) bd_base) / BLK_SIZE(k);
  if((p - (char*) bd_base) % BLK_SIZE(k) != 0)
      n++;
  return n ;
}

int
log2(uint64 n) {
  int k = 0;
  while (n > 1) {
    k++;
    n = n >> 1;
  }
  return k;
}

// Mark memory from [start, stop), starting at size 0, as allocated. 
//把所有范围内的的内存块都标记为已分配
void
bd_mark(void *start, void *stop)
{
  int bi, bj;

  if (((uint64) start % LEAF_SIZE != 0) || ((uint64) stop % LEAF_SIZE != 0))
    panic("bd_mark");

  for (int k = 0; k < nsizes; k++) {
    bi = blk_index(k, start);
    bj = blk_index_next(k, stop);
    for(; bi < bj; bi++) {
      if(k > 0) {
        // if a block is allocated at size k, mark it as split too.
        //如果一块内存（非底层）已经被分配了，同时也把它标记为已分裂？为啥
        bit_set(bd_sizes[k].split, bi);
      }
      // bit_set(bd_sizes[k].alloc, bi);
      bit_xor(bd_sizes[k].alloc, bi);
      //这样改为什么有用呢？第一块异或变为1之后，其buddy也来异或，就又变回去了
      //还有一个好处：边界条件无需特别判定，直接异或操作即可
    }
  }
} 

// return 1 if addr is in range (left,right)
int addr_valid(void *addr, void *left, void *right, int memory_size){
  return ( addr >= left ) &&( (addr + memory_size) < right);
}

// If a block is marked as allocated and the buddy is free, put the
// buddy on the free list at size k.
//初始化内存块.如果有一块被分配了，另一块空着，那么就把它加入空闲链表
//返回该块尺寸（提示：这是初始化函数，而不是分配函数，这里的处理可以借助一些已知条件）
int
bd_initfree_pair(int k, int bi, void *left, void *right) {
  int buddy = (bi % 2 == 0) ? bi+1 : bi-1;
  int free = 0;
  //下面的原逻辑要修改
  // if(bit_isset(bd_sizes[k].alloc, bi) !=  bit_isset(bd_sizes[k].alloc, buddy)) {
  if(bit_xorset(bd_sizes[k].alloc, bi)){
    // one of the pair is free
    free = BLK_SIZE(k);
    // printf("bi (%p,%p,in range:%d)\tbuddy(%p,%p,in range:%d)\n",addr(k,bi),addr(k,bi)+free,addr_in_range(addr(k,bi),left,right,free),addr(k,buddy),addr(k,buddy)+free,addr_in_range(addr(k,buddy),left,right,free));
    if( addr_valid(addr(k, bi), left, right, free) ){
      lst_push(&bd_sizes[k].free, addr(k, bi)); 
    }else{
      lst_push(&bd_sizes[k].free, addr(k, buddy)); 
    }
    /*
    if(bit_isset(bd_sizes[k].alloc, bi))
      lst_push(&bd_sizes[k].free, addr(k, buddy));   // put buddy on free list
    else
       lst_push(&bd_sizes[k].free, addr(k, bi));      // put bi on free list
    */
  }
  return free;
}
  
// Initialize the free lists for each size k.  For each size k, there
// are only two pairs that may have a buddy that should be on free list:
// bd_left and bd_right.
//初始化整层的空闲链表，返回初始化的容量
//初始化过程，只有边界左侧、右侧的两个端点处的内存块可能出现在空闲列表中
//az，啥意思（
int
bd_initfree(void *bd_left, void *bd_right) {
  int free = 0;

  for (int k = 0; k < MAXSIZE; k++) {   // skip max size
    int left = blk_index_next(k, bd_left);
    int right = blk_index(k, bd_right);
    free += bd_initfree_pair(k, left, bd_left, bd_right);
    if(right <= left)
      continue;
    free += bd_initfree_pair(k, right, bd_left, bd_right);
  }
  return free;
}

// Mark the range [bd_base,p) as allocated
int
bd_mark_data_structures(char *p) {
  int meta = p - (char*)bd_base;
  printf("bd: %d meta bytes for managing %d bytes of memory\n", meta, BLK_SIZE(MAXSIZE));
  bd_mark(bd_base, p);
  return meta;
}

// Mark the range [end, HEAPSIZE) as allocated
int
bd_mark_unavailable(void *end, void *left) {
  int unavailable = BLK_SIZE(MAXSIZE)-(end-bd_base);
  if(unavailable > 0)
    unavailable = ROUNDUP(unavailable, LEAF_SIZE);
  printf("bd: 0x%x bytes unavailable\n", unavailable);

  void *bd_end = bd_base+BLK_SIZE(MAXSIZE)-unavailable;
  bd_mark(bd_end, bd_base+BLK_SIZE(MAXSIZE));
  return unavailable;
}

// Initialize the buddy allocator: it manages memory from [base, end).
//初始化给定区间的内存，重点是alloc相关的修改
void
bd_init(void *base, void *end) {
  char *p = (char *) ROUNDUP((uint64)base, LEAF_SIZE); //p是整个内存（首部+有效内存+无效区）的起点
  int sz;

  initlock(&lock, "buddy");
  bd_base = (void *) p;

  // compute the number of sizes we need to manage [base, end)
  nsizes = log2(((char *)end-p)/LEAF_SIZE) + 1;
  if((char*)end-p > BLK_SIZE(MAXSIZE)) {
    nsizes++;  // round up to the next power of 2
  }
  //运行到此，得到整个内存最大的层数
  printf("bd: memory sz is %d bytes; allocate an size array of length %d\n",
         (char*) end - p, nsizes);

  // allocate bd_sizes array
  bd_sizes = (Sz_info *) p; //首部数据的指针
  p += sizeof(Sz_info) * nsizes;  //内存真正的起始指针
  memset(bd_sizes, 0, sizeof(Sz_info) * nsizes);

  // initialize free list and allocate the alloc array for each size k
  //初始化每一层的空闲链表
  for (int k = 0; k < nsizes; k++) {
    lst_init(&bd_sizes[k].free);
    sz = sizeof(char)* ROUNDUP(NBLK(k), 16) >> 4;//为了优化，我们给它的尺寸减半，原来除以8，现在除以16
    bd_sizes[k].alloc = p;
    memset(bd_sizes[k].alloc, 0, sz);
    p += sz;
  }

  // allocate the split array for each size k, except for k = 0, since
  // we will not split blocks of size k = 0, the smallest size.
  //初始化每一层的分裂链表，且第0层为空，我们就跳过了第0层
  for (int k = 1; k < nsizes; k++) {
    sz = sizeof(char)* (ROUNDUP(NBLK(k), 8))/8;
    bd_sizes[k].split = p;
    memset(bd_sizes[k].split, 0, sz);
    p += sz;
  }
  p = (char *) ROUNDUP((uint64) p, LEAF_SIZE);

  // done allocating; mark the memory range [base, p) as allocated, so
  // that buddy will not hand out that memory.
  //标记元数据区为已分配
  int meta = bd_mark_data_structures(p);
  
  // mark the unavailable memory range [end, HEAP_SIZE) as allocated,
  // so that buddy will not hand out that memory.
  //标记无效区为已分配
  int unavailable = bd_mark_unavailable(end, p);
  void *bd_end = bd_base+BLK_SIZE(MAXSIZE)-unavailable;
  
  // initialize free lists for each size k
  int free = bd_initfree(p, bd_end);

  // check if the amount that is free is what we expect
  if(free != BLK_SIZE(MAXSIZE)-meta-unavailable) {
    printf("free %d %d\n", free, BLK_SIZE(MAXSIZE)-meta-unavailable);
    panic("bd_init: free mem");
  }
}
