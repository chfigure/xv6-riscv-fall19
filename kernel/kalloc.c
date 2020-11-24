// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

/*  OS Lab3：内存分配器
    1.让每个CPU核有独立的空闲块链表
    2.CPU优先拿自己的空闲块
    3.自己的块没有空闲的时候，拿别的CPU的
    4.别的CPU也没有，返回0
    5.释放内存块时，将内存块加入当前CPU的空闲表
    注：1~4步均需要加锁实现 */

/*  测试须知：
    1.写之前、之后在xv6环境下运行usertests，确保没有破坏xv6系统原功能
    2.在xv6环境下运行kalloctest以进行测试
    3.测试结果中，test-and-set < 10即为通过 */

/*  其他要求：
    1.锁名字以kmem开头
    2.使用initlock()初始化锁
    3.不要乱改原来的函数定义！！否则系统无法正常工作 */

void freerange(void *pa_start, void *pa_end); //为所有运行该函数的CPU分配空闲内存
uint check_cpuid(); //加函数原型

extern char end[]; // first address after kernel.
// defined by kernel.ld.

struct run
{
    struct run *next;
};

struct kmem
{
    struct spinlock lock;
    struct run *freelist;
}; //类型名字是struct kmem

struct kmem kmems[NCPU];  //声明与CPU数量相同的空闲表，每个表都有锁
char lock_name[8][6] = {"kmem0", "kmem1", "kmem2", "kmem3", "kmem4", "kmem5", "kmem6", "kmem7"};
//给锁起名字，C语言字符串处理比较麻烦，直接用常量的方式

//自定义函数，用于查看当前cpu的id
uint check_cpuid()
{
    uint result;
    push_off(); //关闭中断，保证cpuid函数的安全隐形
    result = cpuid();
    pop_off();  //打开中断
    return result;
}

void
kinit() //初始化锁
{
    for(int i = 0; i < NCPU; i++)
    {
        initlock(&kmems[i].lock, lock_name[i]);
    }
    freerange(end, (void*)PHYSTOP);
}

//传入参数：内存地址起点地址、终点地址
//原来的实现是统一分到一个共享空闲表，我们需要改成每个CPU的专属表（在kfree中）
//由于freerange函数是多CPU并行执行，所以这里的分配实际上是随机的，我们不用特殊处理
void
freerange(void *pa_start, void *pa_end) //把所有可分配内存分到空闲表中
{
    char *p;
    //uint64 memory_length = (uint64)pa_end - (uint64)pa_start; //计算内存长度
    //uint64 page_number = (memory_length / PGSIZE) / NCPU; //得到每个CPU大概分得的页面数
    p = (char*)PGROUNDUP((uint64)pa_start);
    for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) //按页释放内存
        kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
    struct run *r;

    if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);  //洗掉这一页内存的内容

    r = (struct run*)pa;  //即本页内存的起始指针

    uint cpu_id = check_cpuid();  //获取当前CPU的序号
    acquire(&kmems[cpu_id].lock);  //上锁
    r->next = kmems[cpu_id].freelist;   //一开始时，空指针为0
    kmems[cpu_id].freelist = r; //往链表头加空内存的地址
    release(&kmems[cpu_id].lock);  //解锁
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.

void * steal_memory(int cpu_id)   //从其他cpu偷内存块
{
    struct run *r;
    uint random_start = cpu_id+1; //随机从一个cpu开始
    uint times = 0; //表示搜索的cpu个数
    while (times < 7) //没有搜索完所有cpu时继续
    {
        acquire(&kmems[random_start].lock);
        r = kmems[random_start].freelist;
        if(r)
        {
            kmems[random_start].freelist = r->next;
            release(&kmems[random_start].lock);
            memset((char*)r, 5, PGSIZE);
            break;
        } //如果空闲表有内存，就取一个分配，链表少1位
        times++;  //已经搜索的cpu数+1
        release(&kmems[random_start].lock);
        random_start++; //搜索cpu序号+1
        if(random_start == 8) //如果序号到8，就从0开始
            random_start = 0;
    }
    return (void*)r;
}

void *
kalloc(void)  //分配内存，我们需要优先获取当前CPU的空闲表-->偷其他CPU的-->都没有，返回0
{
    struct run *r;
    uint cpu_id = check_cpuid();  //获取当前CPU的序号
    acquire(&kmems[cpu_id].lock);
    r = kmems[cpu_id].freelist;
    if(r) //如果空闲表有内存，就取一个分配，链表少1位
        kmems[cpu_id].freelist = r->next;
    release(&kmems[cpu_id].lock);
    if(!r){
      r = steal_memory(cpu_id);
      return (void *)r;
    }  //如果r为空，说明当前cpu的空闲表为0，需要偷其他CPU的
        
    if(r)
        memset((char*)r, 5, PGSIZE); // fill with junk，让正在用这块内存的程序赶紧报错退出
    return (void*)r;
}
