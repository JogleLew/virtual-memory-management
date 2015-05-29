#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "vmm.h"

int
main(int argc, char* argv[])
{
	char input[255], sig[255];
	int i, addr, writeValue, pos = 0, fd;
	char c;

	mode_t mode = 0666;
	while (1){
		if ((fd = open("fifo", O_WRONLY)) < 0)  {  
			printf("打开FIFO失败。\n");  
			exit(1);  
		}
		memset(sig, 0, sizeof(sig));
		pos = 0;
		printf("请输入请求地址，或输入r随机生成请求：");
		scanf(" %s", input);
		if (input[0] == 'r'){
			sig[pos++] = 'r';
			sig[pos++] = '\0';
			write(fd, sig, 255);
			close(fd);
			continue;
		}
		for (i = 0; i < strlen(input); i++)
			if (input[i] < '0' || input[i] > '9'){
				printf("输入错误，请重新输入。\n");
				continue;
			}
		addr = 0;
		for (i = 0; i < strlen(input); i++)
			addr = addr * 10 + (input[i] - '0');

		// 请求地址正确，记录到sig数组中。
		for (i = 0; i < strlen(input); i++)
			sig[pos++] = input[i];
		sig[pos++] = '\n';

		printf("请输入请求类型(r/w/x)：");
		scanf(" %s", input);
		if (strlen(input) != 1){
			printf("输入错误，请重新输入。\n"); 
			continue;
		}

		c = input[0];
		// 请求类型正确，记录到sig数组中。
		sig[pos++] = c;
		sig[pos++] = '\n';

		switch (c){
			case 'r': case 'R': case 'x': case 'X': 
				;
			break;
			case 'w': case 'W': 
				printf("请输入写入内容：");
				scanf(" %s", input);
				for (i = 0; i < strlen(input); i++)
					if (input[i] < '0' || input[i] > '9'){
						printf("输入错误，请重新输入。\n");
						continue;
					}
				// 请求内容正确，记录到sig数组中。
				for (i = 0; i < strlen(input); i++)
					sig[pos++] = input[i];
				sig[pos++] = '\n';
			break;
			default: 
				printf("输入错误，请重新输入。\n"); continue;
		}
		sig[pos++] = '\0';
		write(fd, sig, 255);
		close(fd);
	}
	return 0;
}