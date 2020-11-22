#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/param.h"   //宏定义头文件，引用MAXARG这个宏，限制命令的最大长度

#define PARAMS 32   //最终执行的命令的总参数个数的最大值，设置为32应该够了
int main(int argn, char *argv[])
{
    //argv[0]是要执行的命令，其余的是该程序运行的参数
    if (argn < 2) //输入关键字太少，退出
        exit();
    char *command = argv[1];  //表示echo字符串
    char *params[PARAMS];   //总的参数列表（命令中带有的参数+用户输入的参数）
    char input_line[MAXARG];  //存储用户输入的参数字符串（一行）
    int index = 0;  //遍历params用的下标
    params[index++] = command;  //也就是echo，放在参数列表第0位
    for (int i = 2; i < argn; i++)
        params[index++] = argv[i];  //输入的命令中带有的参数都先放进params数组里（用户输入的稍后放）
    int digit = 0;  //这一行输入的字符串位数
    while ((digit = read(0, input_line, MAXARG)) > 0)   //先执行左侧语句：控制台输入字符放在line数组中
    //输入字符串的位数放在n中，然后判断n是否 > 0，也就是读取成功就进行循环；读不出来就退出程序（用户不再输入）
    {
        if (fork() == 0)    //子进程操作（对应一行命令）
        {
            char *temp = (char *)malloc(sizeof(char) * MAXARG);  //temp是一个暂存字符串，用于暂存用户输入的数据
            int count = 0;  //计数变量，用于遍历temp字符串
            for (int i = 0; i < digit; i++)  //循环遍历所有输入的参数
            {
                if (input_line[i] == '\n' || input_line[i] == ' ')  //用户输入的参数到头了
                {
                    temp[count] = '\0';    //在temp字符串后面加上终止符
                    params[index++] = temp;    //这里我们将控制台用户输入得到的字符串也放进参数列表中
                    //index继承了上面的数值，所以此时我们可以直接这样赋值
                    count = 0;  //到头了，要把计数变量归零，准备下一次遍历
                    temp = (char *)malloc(sizeof(char) * MAXARG);//为temp字符串申请一段新的空间存储信息
                }
                else
                    temp[count++] = input_line[i];
            }
            exec(command, params);  //使用exec功能，执行我们给定的指令（单行执行，因此放在子进程）
            printf("cannot do exec!\n"); //如果执行到这里，说明指令执行失败，打印信息
            exit();
        }
        else    //父进程等待子进程结束
            wait();
    }
    exit();
}
