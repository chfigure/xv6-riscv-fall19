#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

/*实现find指令
即：在给定路径的文件夹树中找到全部与name匹配的文件
从ls.c文件中复制过来稍加改造（ */
//返回path中最后一个 / 之后的字符串位置
char *getLastString(char *path)
{
    char *p = path;    //遍历字符的指针，初始值为它的
    char *last = 0;
    while(*p != '\0')
    {
        if(*p == '/')
            last = p;   //找到了斜线位置，用last存起来
        p++;
    }
    //或者没有/符号，则直接返回p，它就是文件名
    if(last == 0)
        return path;
    return last + 1;    //注意，这里
}

void find(char* path,char *name)    //自定义的实现ls功能的函数
{
    char buffer[512];  //字符缓冲区
    char *p = 0;    //字符串指针
    int fd; //一个句柄，存放目录打开之后的信息
    struct dirent de;   //kernel/fs.h文件下的一个结构体，应该装的是命令输入的文件个数+文件名？
    struct stat st;     //kernel/stat.h文件下的一个结构体，里面是文件信息的一些描述

    if((fd = open(path, 0)) < 0)    //ls原逻辑
    {
        fprintf(2, "ls: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0)
    {
        fprintf(2, "ls: cannot stat %s\n", path);
        close(fd);
        return;
    }
    switch(st.type)
    {
    case T_FILE:    //普通文件类型
        p = getLastString(path);  //找到路径中包含的文件名
        if(strcmp(p,name) == 0) //如果二者一致，输出查找结果
            printf("%s\n",path);
        break;

    case T_DIR: //目录文件类型（请注意，目录也是一个文件！）
        if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buffer))     //输入字符太多，直接退出了……
        {
            printf("ls: path too long\n");
            break;
        }
        strcpy(buffer, path);  //目录字符串放在缓冲区
        p = buffer + strlen(buffer);  //指针移动到末尾
        *p++ = '/'; //在末尾加上 / ，指针同时移到/的后面
        while(read(fd, &de, sizeof(de)) == sizeof(de))  //持续从命令中读取文件名？
        {
            if(de.inum == 0)    //基本是ls的原逻辑
                continue;
            int dir_num = strlen(de.name) > DIRSIZ ? DIRSIZ : strlen(de.name);    //得到路径长度
            memmove(p, de.name, dir_num); //把这一段路径和之前的拼起来得到完整的目录
            p[dir_num] = 0;
            // p让buf这个字符串现在是当前dirent的完整路径
            if(stat(buffer, &st) < 0)
            {
                printf("ls: cannot stat %s\n", buffer);
                continue;
            }
            if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;   //实验题目要求，所以对于这两种情况继续执行，阻止其递归
            find(buffer, name); //递归查找目录下的子目录
        }
        break;
    }
    close(fd);
}

int main(int argn, char* argv[])
{
    // 这里为了简单，假定一定按照usage使用
    // 实际上如果只有一个参数，那么搜索路径为当前路径
    if(argn > 3)
    {
        exit();
    }
    find(argv[1], argv[2]);     //调用寻找函数
    exit();

}
