#include "../kernel/types.h"
#include "user.h"

int main(int argn, char *argv[])
{
    if(argn != 1)   //判断接受了几个单词（应该是按照分隔符分开的）
    {
        fprintf(2, "there is no argument for pingpong!\n");
        exit();
    }
    int parentToson_fd[2]; //父进程到子进程的管道句柄，0读1写
    int sonToParent_fd[2]; //子进程到父进程的管道句柄
    char* ping = "ping";
    char* pong = "pong";    //传输的内容
    pipe(parentToson_fd);  //建立父-->子的管道
    pipe(sonToParent_fd);  //建立子-->父的管道
    char* buf = malloc( sizeof(ping) * sizeof(char) );  //声明一个缓冲区装载接收数据
    if(fork() == 0)     //子进程创建成功
    {
        if(write(sonToParent_fd[1],pong,sizeof(pong)) != sizeof(pong))
        {
            //说明发生了写入错误（子-->父，子写）
            printf("son-->parent write error!\n");
            exit();
        }
        close(sonToParent_fd[1]);   //关闭写入通道
        if(read(parentToson_fd[0],buf,sizeof(buf)) != sizeof(buf))
        {
            //说明发生了读取错误（父-->子，子读）
            printf("parent-->son read error!\n");
            exit();
        }
        close(parentToson_fd[0]);   //关闭读取通道
        printf("%d: received ping\n",getpid());
        exit();
    }   //子进程任务结束，下面是父进程
    if(write(parentToson_fd[1],ping,sizeof(ping)) != sizeof(ping))
    {
        //说明发生了写入错误（父-->子，父写）
        printf("parent-->son write error!\n");
        exit();
    }
    close(parentToson_fd[1]);   //关闭写入通道
    if(read(sonToParent_fd[0],buf,sizeof(buf)) != sizeof(buf))
    {
        //说明发生了读取错误（子-->父，父读）
        printf("son-->parent read error!\n");
        exit();
    }
    close(sonToParent_fd[0]);   //关闭读取通道
    printf("%d: received pong\n",getpid()); //输出按格式，不然没法匹配……测评程序就会判断错误
    free(buf);
    wait();    //等待子进程结束
    exit();
}
