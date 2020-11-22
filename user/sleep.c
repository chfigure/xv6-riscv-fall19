#include "kernel/types.h"
#include "user/user.h"

int main(int argn, char *argv[]){
	if(argn != 2){  //判断接受了几个单词（应该是按照分隔符分开的）
		fprintf(2, "must input 1 argument for sleep!\n");
		exit();
	}
    char temp = argv[1][0]; //取输入参数的首字符判断
    if(temp < '0' || temp > '9'){
        fprintf(2, "must legal number!\n");
		exit();
    }
	int sleepNum = atoi(argv[1]);
	printf("(nothing happens for a little while)\n");
	sleep(sleepNum);
	exit();
}