#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"   //copy from sh.c
//#include "user/ulib.c"  //可能使用的关于字符串的函数放在这个头文件
//OS Lab2:自己实现的一个简易的shell
//echo,grep,cat,wc函数的内部逻辑无需实现，直接调用user文件夹下写好的即可
//实验要求手动的是：实现两个进程间的管道+重定向

#define MAXARGS 16  //最多16个参数
#define MAXWORD 32  //每个参数最长位数32
#define MAXLENGTH 128 //命令最长长度设为128位    
char whitespace[] = " \t\r\n\v";    //空格字符
char args[MAXARGS][MAXWORD];    //copy from sh.c
//为什么要搞这么个二维数组？因为本次实验不让用mal_loc等动态分配内存的方式，我们曲线救国，弄一个静态的空间存储数据
//mal_loc不能打全，检测原理就是字符串匹配……
void myPipe(int argc, char* argv[]); //声明一个函数原型，防止编译器报错

int getcmd(char *buf, int nbuf) //从终端取得命令，copy from sh.c
{
    fprintf(2, "@ ");   //为了区分开来，这个自定义的shell使用@号
    memset(buf, 0, nbuf);
    gets(buf, nbuf);
    if (buf[0] == 0) // EOF
        return -1;
    return 0;
}

void setargs(char *cmd, int* argc, char* argv[])    //处理得到的命令（把命令、参数都取出来，存储在argv数组）
{   //处理通常指去掉空格等分隔字符
    for(int i = 0; i < MAXARGS; i++)    //初始化argv的指针地址（实验禁止使用mal_loc动态分配，因此我们需要手动初始化一下地址）
        argv[i] = &args[i][0];
    int i = 0;  //表示有i个单词，用于统计输入的参数个数
    int j = 0;  //指令字符串的指针
    for(; cmd[j] != '\n' && cmd[j] != '\0'; j++){  //命令字符串没结束，就逐位遍历
        //循环遍历命令中的每一个有效参数（包括命令名）
        //让argv[i]分别指向他们的开头，并且将echo，hi后面的空格设为\0
        while(strchr(whitespace, cmd[j]))   //跳过空格
            j++;
        argv[i++] = cmd + j;    //没有空格了，说明找到第一个词语，词语数+1，把词语地址放在字符串数组中
        while(strchr(whitespace, cmd[j]) == 0)  //只要不是空格，指针下移（因为是参数），找到下一个空格再停下来
            j++;
        cmd[j] = '\0';  //找到下一个空格后，替换成字符串终结符（这样处理，就能自动地断开字符串，也就达到了参数列表处理的效果）
    }
    argv[i] = 0;    //参数列表最后补0，保证正确执行命令
    *argc = i;  //表示参数个数
}

void runcmd(int argc, char* argv[]) //调用写好的exec函数来运行这些命令（省去编写内部逻辑）
{ //传入：参数个数、参数列表
    for(int i = 1; i < argc; i++)   //注意这里从1开始，下面管道运行命令的时候要注意哪一位才是起始位
        if(!strcmp(argv[i], "|")) 
            myPipe(argc, argv);     // 如果遇到 | 管道指令，说明需要使用管道，调用自己编写的管道函数
            //注意，找到一个|之后我们仍然需要继续找，因为可能存在多个管道的情况，因此这里没有break退出循环
    //接下来，我们仍旧需要重头扫描字符串，查看是否有重定向符号< or >
    for(int i = 1; i < argc; i++){
        if(!strcmp(argv[i], ">")){      //遇到 > ，需要输出重定向，关闭stdout，文件描述符为1
            close(1);
            open(argv[i + 1], O_CREATE | O_WRONLY);   //输出重定向到文件，也就是创建并写文件
            argv[i] = 0;    //命令末尾的0，为了保证运行正确
        }
        else if(!strcmp(argv[i], "<")){     //遇到< ,需要输入重定向，关闭stdin，文件描述符为0
            close(0);
            open(argv[i + 1], O_RDONLY);  //只读打开对应文件
            argv[i] = 0;
        }
    }
    exec(argv[0], argv);    //运行该命令，argv[0]就表示命令名
}

//emmm，按理说已经在main函数前面声明了，但是编译器还是报错，所以在前面加个函数原型
void myPipe(int argc, char* argv[]){  //自己编写的管道（仿照sh.c的管道部分）
    // 从前到后，找到第一个就停止，后面都递归调用
    int i;  //计数变量
    for(i = 0; i < argc; i++){   //循环遍历，找到命令中的第一个 |，把它换成0，表示前一条命令的结束
        if(!strcmp(argv[i], "|")){
            argv[i] = 0;
            break;
        }
    }
    //考虑原始情况：a order | another order
    int p[2];
    pipe(p);   //调用系统函数创建管道，记住0口读1口写……
    if(fork() == 0){    //标准的双进程编程套路……看多就会了
        //子进程的任务，执行 | 左边的命令，它的输出会传递给右侧命令，因此我们要关闭子进程的1口（stdout）
        close(1);
        dup(p[1]);      //把管道端口复制一份到1口（1空出来了），也就起到了重定向的作用
        //子进程和父进程一样，也有管道的两个端口！
        close(p[0]);    //关闭子进程的读端口，子进程不需要从管道中读数据
        close(p[1]);    //关闭子进程的写端口，因为写端口被重定向到了父进程
        runcmd(i, argv);
    }
    else{//父进程的任务：执行 | 右边的命令，它的输入从左侧命令来，因此我们要关闭父进程的0口（stdin）
        close(0);
        dup(p[0]);      //把管道端口复制一份到0口（0空出来了）
        close(p[0]);    //关闭父进程的读端口，因为父进程的输入被重定向到了子进程的输出端口，关掉它才能使用默认的输入端口
        close(p[1]);    //关闭父进程的写端口，父进程不需要向管道中写数据传给子进程
        runcmd(argc - i - 1, argv + i + 1); //注意参数个数、参数列表起始点的计算！
    }
}

int main(){  //主函数，sh.c的main函数简化得到
    char buf[MAXLENGTH];  //暂存命令字符串的缓冲区
    // Read and run input commands.
    while(getcmd(buf, sizeof(buf)) >= 0)
    {
        if (fork() == 0)   
        {
            char* argv[MAXARGS];    //命令字符串数组
            int argc = -1;
            setargs(buf, &argc, argv);  //处理命令字符串
            runcmd(argc, argv);  //运行该命令
        }
        wait(0);    //在旧版文件跑一下，新版文件出错……暂时去掉这个0
    }
    exit(0);
}