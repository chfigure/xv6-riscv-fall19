#include "../kernel/types.h"
#include "kernel/stat.h"
#include "user.h"
#define READ 0
#define WRITE 1 //设置两个宏增强可读性（用于标识管道读/写端）

void solve(){   //求解函数
    int num[34];
    int i;  //计数变量
    for(i = 2;i <= 35;i++){ //初始化数组
        num[i-2] = i;
    }
    int count = 33;  //计算当前有多少个有效数据需要传递给下一个管道，初始值是34个数-1（0下标的影响）
    int fd[2];  //存储管道句柄
    //由于我们需要不断创建新管道，所以我们需要在一个循环里面调用fork()进行父进程、子进程的动作编写
    while (count > 0) { //在还有有效数据时，不断执行循环
        pipe(fd);
        if (fork() == 0) {
        int prime = 0;     //表示传来的第一个数
        int data;  //暂存管道传来的数
        close(fd[WRITE]);   // 关闭管道写端口（子进程只需要接收数据）
        count = -1;     //初始化count变量，用于首次筛选的情况判定
        // 读的时候，如果父亲还没写，就会block
        while (read(fd[READ], &data, sizeof(data)) != 0) {
            // 设置当前进程收到的第一个数（也就是用来当除数的数）
            if (count == -1) {  //初次进入，做好相应的变量设置
                prime = data;
                count = 0;
            } 
            else {
                if (data % prime != 0) {    //满足条件，放回数组，等待传给下一个进程
                    num[count++] = data;
                }
            }
        }
        printf("prime %d\n",prime);    //打印质数信息（本进程收到的第一个数）
        close(fd[READ]);    // 关闭管道读端口
        }
        //！！！这里子进程还在继续运行！会继续执行循环语句，再创建一个孙进程，以此类推
        //孙进程会获得和子进程一致的cnt和numbers，也就是处理过的数据
        else {    // 父进程动作，仅传递数据       
            close(fd[READ]);    //关闭读管道
            for (i = 0; i < count; i++) {
                write(fd[WRITE], &num[i], sizeof(num[0]));  //把写的数据传到管道写端口
            }
            close(fd[WRITE]);   //写完了，把写端口关掉
            wait(); //等待子进程结束
            break;
        }
    }
}

int main(int argn, char *argv[]){
    if(argn > 1){  //判断接受了几个单词（应该是按照分隔符分开的）
		fprintf(2, "must input 0 argument for primes!\n");
		exit();
	}
    solve();    //调用求解函数
    exit();
}