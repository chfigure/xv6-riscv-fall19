// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#define NBUCKETS 13
/*  OS_Lab3-2:磁盘缓存
  1.修改bcache数据结构，增加哈希表
  2.修改bget()和brelse()，使得查找和释放缓存中的不同块时，锁之间的冲突更少
  3.修改bpin()和bunpin()，增加或减少refcnt之前获取对应数据缓存块的锁，操作完成之后再释放锁
  （大体思路与Lab3-1完全相同，从一个CPU对应一个链表 变成了 一个哈希桶对应一把锁）*/
//原子操作：多线程编程术语，一旦开始就无法被线程调度打断的操作

struct {
  struct spinlock lock[NBUCKETS]; //整个bcache的锁，现在它变成了每个哈希表的锁
  struct buf buf[NBUF]; //每一块缓存块内部有一个sleeplock，锁它自己的

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf hashbucket[NBUCKETS]; //每个哈希队列一个linked list及一个lock
} bcache;

char bcache_lock_name[13][8] = {"bcache0", "bcache1", "bcache2", "bcache3", "bcache4", "bcache5", 
    "bcache6", "bcache7","bcache8", "bcache9", "bcache10", "bcache11", "bcache12"};
//给锁起名字，C语言字符串处理比较麻烦，直接用常量的方式
//这里注意，变量名不能与内核其他变量重复！否则make qemu会报错

//自定义函数，取得一个块号对应的哈希值
int getHashValue(uint num){  
  return num % NBUCKETS;
}
// 0 1
// ...
// 22 23
// 24 25
// 6 26 27

void
binit(void)
{
  struct buf *b;
  int buf_num = 0;  //cache块序号
  for(int i = 0; i < NBUCKETS; i++){   //初始化所有哈希表的锁
    initlock(&bcache.lock[i], bcache_lock_name[i]);
  }

  // Create linked list of buffers
  for(int i = 0; i < NBUCKETS; i++){  //初始化每一个哈希桶的链表
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
  }
  
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){  //一共30块，考虑一个哈希表分到2~3块左右
    //哈希规则：除2（整数除），得到多少就放对应的桶，如果超出13，就再除1次
    int buf_num_remain = getHashValue(buf_num);  //得到对应哈希值，放入对应链表
    b->next = bcache.hashbucket[buf_num_remain].next;
    b->prev = &bcache.hashbucket[buf_num_remain]; //头插法维护双向链表，这里维护的是旧节点
    initsleeplock(&b->lock, "buffer");
    bcache.hashbucket[buf_num_remain].next->prev = b;
    bcache.hashbucket[buf_num_remain].next = b; //头插法维护双向链表，这里维护的是新节点
    buf_num++;  //序号+1
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)  //检查请求的磁盘块是否在缓存中
{
  struct buf *b;
  int hash_value = getHashValue(blockno); //取得哈希桶值
  //// printf("hash_value:%d\n",hash_value);
  acquire(&bcache.lock[hash_value]);  //对应哈希桶的bcache上锁
  // printf("1\n");
  // Is the block already cached?（链表向后遍历）
  for(b = bcache.hashbucket[hash_value].next; b != &bcache.hashbucket[hash_value]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){ //在链表中找到对应的缓存块
      b->refcnt++; 
      release(&bcache.lock[hash_value]);
      acquiresleep(&b->lock); //对应缓存块上锁
      return b;
    }
  }
  release(&bcache.lock[hash_value]);
  // Not cached; recycle an unused buffer.（链表向前遍历，后面的才更有可能符合条件，链头都是release放出来的）
  //这里是核心改动！（类似实验3-1中的偷东西，如果当前表找不到，我们就要去其他表找！并且，找到之后要放到当前的表中！）
  int bucket_num = hash_value + 1;  //当前遍历的表，执行到这里说明在原来的表中找不到，我们要去其他地方找
  while(bucket_num != hash_value){ //从当前的表开始找，找不到就要去下一个表，直到找完一轮为止
    if(bucket_num >= NBUCKETS)  //哈希表序号到头了，回复成0号继续遍历
      bucket_num = 0;
    // printf("bucket_num:%d    2_x\n",bucket_num);
    acquire(&bcache.lock[bucket_num]);  //对应哈希桶的bcache上锁，这样下面才能进去找
    // printf("2\n");
    for(b = bcache.hashbucket[bucket_num].prev; b != &bcache.hashbucket[bucket_num]; b = b->prev){
      if(b->refcnt == 0) {  //如果该块未被引用，把它赶出去，它就是我们要找的东西
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        //将当前块与当前链表解锁
        b->next->prev = b->prev;
        b->prev->next = b->next;
        //然后，插到当前的链表中去
        // printf("3_x\n");
        acquire(&bcache.lock[hash_value]);  //对应哈希桶的bcache上锁
        // printf("3_1\n");
        b->next = bcache.hashbucket[hash_value].next;
        b->prev = &bcache.hashbucket[hash_value]; //头插法维护双向链表，这里维护的是旧节点
        bcache.hashbucket[hash_value].next->prev = b;
        bcache.hashbucket[hash_value].next = b; //头插法维护双向链表，这里维护的是新节点
        release(&bcache.lock[hash_value]);  //操作完，释放锁
        release(&bcache.lock[bucket_num]);  //操作完，释放锁
        acquiresleep(&b->lock); //该块内存上锁
        // printf("3_y\n");
        return b;
      }
    }
    release(&bcache.lock[bucket_num]);  //没找到，要记得释放锁！（上锁，解锁是成对操作）
    bucket_num++; //没找到，遍历的表数++
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b->dev, b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b->dev, b, 1);
}

// Release a locked buffer.
// Move to the head of the MRU list.
void
brelse(struct buf *b) //释放一块内存
{
  int hash_value = getHashValue(b->blockno); //获取哈希桶号
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock); //给缓存块开锁

  acquire(&bcache.lock[hash_value]);  //对应哈希桶上锁
  // printf("4\n");
  b->refcnt--;
  if (b->refcnt == 0) { //当这一块没有引用了，就把它移出去
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.hashbucket[hash_value].next;
    b->prev = &bcache.hashbucket[hash_value];
    bcache.hashbucket[hash_value].next->prev = b;
    bcache.hashbucket[hash_value].next = b; //这几句是把这一块移动到链头（MRU算法）
  }
  
  release(&bcache.lock[hash_value]);
}

void
bpin(struct buf *b) { //引用次数增加，我们需要增加一个获取单个缓存块的锁的操作
  int hash_value = getHashValue(b->blockno); //获取哈希桶号
  acquire(&bcache.lock[hash_value]);
  // printf("5\n");
  b->refcnt++;
  release(&bcache.lock[hash_value]);
}

void
bunpin(struct buf *b) { //引用次数减少，修改同上
  int hash_value = getHashValue(b->blockno); //获取哈希桶号
  acquire(&bcache.lock[hash_value]);
  // printf("6\n");
  b->refcnt--;
  release(&bcache.lock[hash_value]);
}


