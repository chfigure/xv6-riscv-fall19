//
// Support functions for system calls that involve file descriptors.
//

/*  OS_Lab4-1：修改file，突破打开文件数量限制
  1.移除kernel/file.c第19行中file[NFILE]的声明
  2.在filealloc中使用bd_malloc动态申请文件描述符
  3.在fileclose中释放文件描述符
  */

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  //struct file file[NFILE]; //删去数组声明，突破静态文件限制
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

void clear(char *array, int index) {  //清零某一位数据，从buddy复制来
  char b = array[index/8];
  char m = (1 << (index % 8));
  array[index/8] = (b & ~m);
}

// Allocate a file structure.
struct file*
filealloc(void) //给文件分配内存，我们要修改它，让它动态申请
{ //调用bd_malloc动态申请，申请之前请清零内存！
  struct file *f;
  uint64 file_size = sizeof(*f);  //取文件的大小，注意加*，这样才能取出文件正确大小
  /*for(int i = 0; i < file_size; i++)  //清空内存块，这里8行，只能清字节数量大小的比特
    clear((char *)f, i);*/

  acquire(&ftable.lock);
  f = bd_malloc(file_size); //分配文件指针，应该先上锁再分配，保险
  //for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f){  //文件未被使用过，这个条件应该改为指针是否为空的判断
      memset(f, 0, file_size);  //直接用这个函数清零更好
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  //}
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
//没搞清楚这个ff怎么简化，暂时先不简化了
void
fileclose(struct file *f) //关闭文件
{
  struct file ff;
  acquire(&ftable.lock);
  if(f->ref < 1)  //文件引用数已经为0，已经关了，再关闭就出错了
    panic("fileclose");
  if(--f->ref > 0){ //文件还在用
    release(&ftable.lock);
    return;
  }
  ff = *f;  //暂存了文件的数据，用于稍后的文件设置
  f->ref = 0;
  f->type = FD_NONE;
  bd_free(f);  //释放文件描述符号
  release(&ftable.lock);

  if(ff.type == FD_PIPE){ //管道文件的关闭
    pipeclose(ff.pipe, ff.writable);
  } 
  else if(ff.type == FD_INODE || ff.type == FD_DEVICE){ //其他类型文件的关闭
    begin_op(ff.ip->dev);
    iput(ff.ip);
    end_op(ff.ip->dev);
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op(f->ip->dev);
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op(f->ip->dev);

      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}

