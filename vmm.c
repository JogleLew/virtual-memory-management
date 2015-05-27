#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <memory.h>
#include <string.h>
#include "vmm.h"
#define DEBUG

/* JG页目录*/
PageCatalogueItem JGpageCatalogue[PAGE_CATALOGUE_SUM];
/* JG页表使用标识 */
BOOL pageStatus[PAGE_CATALOGUE_SUM];
/* 页表 */
PageTableItem pageTable[PAGE_SUM];
/* 实存空间 */
BYTE actMem[ACTUAL_MEMORY_SIZE];
/* 用文件模拟辅存空间 */
FILE *ptr_auxMem;
/* 物理块使用标识 */
BOOL blockStatus[BLOCK_SUM];
/* 访存请求 */
Ptr_MemoryAccessRequest ptr_memAccReq;

/* 管道 */
int fd[2];

void JGinitPageCatalogue()
{
	int i;
	for (i = 0; i < PAGE_CATALOGUE_SUM; i++){
		JGpageCatalogue[i].pageCatalogueNum = i;
		JGpageCatalogue[i].pageNum = 0;
		JGpageCatalogue[i].filled = FALSE;
		JGpageCatalogue[i].count = 0;
		JGpageCatalogue[i].auxAddr = PAGE_SIZE * 2 * i + PAGE_SIZE;
		pageStatus[i] = FALSE;
	}
}

void JGrandomPageIn(int j)
{
	int i, catalogueNum = j / PAGE_SIZE, pageOffset = j % PAGE_SIZE, pageNum;
	for (i = 0; i < PAGE_CATALOGUE_SUM; i++)
		if (!pageStatus[i])
			break;
	if (!JGpageCatalogue[catalogueNum].filled){
		pageNum = i + pageOffset;
		JGpageCatalogue[catalogueNum].pageNum = i;
		JGpageCatalogue[catalogueNum].filled = TRUE;
		pageStatus[i] = TRUE;
		pageTable[pageNum].blockNum = j;
		pageTable[pageNum].filled = TRUE;
		#ifdef DEBUG
			printf("catalogue num: %d -> pageNum: %d\n", catalogueNum, i);
			printf("pageNum: %d -> block: %d\n", pageNum, j);
		#endif
	}
	else{
		pageNum = JGpageCatalogue[catalogueNum].pageNum + pageOffset;
		pageTable[pageNum].blockNum = j;
		pageTable[pageNum].filled = TRUE;
		#ifdef DEBUG
			printf("catalogue num: %d -> pageNum: %d\n", catalogueNum, JGpageCatalogue[catalogueNum].pageNum);
			printf("pageNum: %d -> block: %d\n", pageNum, j);
		#endif
	}
}

void JGdo_print_catalogue_info()
{
	unsigned int i, j, k;
	char str[4];
	printf("***********************页目录***********************\n");
	printf("页目录号页号\t装入\t计数\t辅存\n");
	for (i = 0; i < PAGE_CATALOGUE_SUM; i++)
	{
		printf("%u\t%u\t%u\t%u\t%u\n", 
			i, JGpageCatalogue[i].pageNum * PAGE_SIZE, JGpageCatalogue[i].filled, 
			JGpageCatalogue[i].count, JGpageCatalogue[i].auxAddr);
	}
}

void JGdo_print_aux_info()
{
	int i;
	BYTE auxMem[512];
	if (fseek(ptr_auxMem, 0, SEEK_SET) < 0)
	{
		do_error(ERROR_FILE_SEEK_FAILED);
		exit(1);
	}
	memset(auxMem, 0, sizeof(auxMem));
	fread(auxMem, sizeof(BYTE), 512, ptr_auxMem);
	printf("*****************************辅存信息*****************************\n");
	for (i = 0; i < 512; i += 4)
		printf("地址%3d : %02X | 地址%3d : %02X | 地址%3d : %02X | 地址%3d : %02X\n", 
			i, auxMem[i], i + 1, auxMem[i + 1], i + 2, auxMem[i + 2], i + 3, auxMem[i + 3]);
	printf("\n");
}

void JGdo_print_act_info()
{
	int i;
	printf("*****************************实存信息*****************************\n");
	for (i = 0; i < ACTUAL_MEMORY_SIZE; i += 4)
		printf("物理块%3d || 地址%3d : %02X | 地址%3d : %02X | 地址%3d : %02X | 地址%3d : %02X\n", 
			i / 4, i, actMem[i], i + 1, actMem[i + 1], i + 2, actMem[i + 2], i + 3, actMem[i + 3]);
}

unsigned int JGcalPageCatalogueNum()
{
	return ptr_memAccReq->virAddr / PAGE_SIZE / PAGE_SIZE;
}

unsigned int JGcalPageCatalogueOffset()
{
	return ptr_memAccReq->virAddr / PAGE_SIZE % PAGE_SIZE;
}

void JGresponsePrint(unsigned int num, unsigned int off, unsigned int off2)
{
	printf("页目录号为：%u\t页目录偏移为：%u\t页内偏移为：%u\n", num, off, off2);
}

void JGunknown()
{
	#ifdef DEBUG
		printf("unknown error!\n");
	#endif
}

void JGdo_page_catalogue_fault(Ptr_PageCatalogueItem ptr_pageCatalogueItem)
{
	unsigned int i, j, virAddr;
	PageTableItem pageTabItem;
	printf("产生缺页目录中断，开始进行调页...\n");
	for (i = 0; i < BLOCK_SUM; i++)
	{
		if (!blockStatus[i])
		{
			pageTabItem.auxAddr = ptr_pageCatalogueItem->auxAddr;
			/* 读辅存内容，写入到实存 */
			do_page_in(&pageTabItem, i);
			
			/* 更新页目录和页表内容 */
			virAddr = ptr_memAccReq->virAddr;
			for (j = 0; j < PAGE_CATALOGUE_SUM; j++)
				if (!pageStatus[j])
					break;
			ptr_pageCatalogueItem->pageNum = j;
			pageStatus[j] = TRUE;

			pageTable[j * PAGE_SIZE + JGcalPageCatalogueOffset()].blockNum = i;
			pageTable[j * PAGE_SIZE + JGcalPageCatalogueOffset()].filled = TRUE;
			pageTable[j * PAGE_SIZE + JGcalPageCatalogueOffset()].edited = FALSE;
			pageTable[j * PAGE_SIZE + JGcalPageCatalogueOffset()].count = 0;
			
			blockStatus[i] = TRUE;
			return;
		}
	}
	/* 没有空闲物理块，进行页面替换 */
	JGunknown();
}

int JGdo_request()
{
	char input[255], sig[255];
	int i, addr, writeValue, pos = 0;
	char c;
	printf("请输入请求地址：");
	scanf(" %s", input);
	for (i = 0; i < strlen(input); i++)
		if (input[i] < '0' || input[i] > '9'){
			printf("输入错误，请重新输入。\n");
			return 1;
		}
	addr = 0;
	for (i = 0; i < strlen(input); i++)
		addr = addr * 10 + (input[i] - '0');
	if (addr >= VIRTUAL_MEMORY_SIZE){
		printf("输入错误，请重新输入。\n");
		return 1;
	}

	// 请求地址正确，记录到sig数组中。
	for (i = 0; i < strlen(input); i++)
		sig[pos++] = input[i];
	sig[pos++] = '\n';

	printf("请输入请求类型(r/w/x)：");
	scanf(" %s", input);
	if (strlen(input) != 1){
		printf("输入错误，请重新输入。\n"); 
		return 1;
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
					return 1;
				}
			// 请求内容正确，记录到sig数组中。
			for (i = 0; i < strlen(input); i++)
				sig[pos++] = input[i];
			sig[pos++] = '\n';
		break;
		default: 
			printf("输入错误，请重新输入。\n"); return 1;
	}
	sig[pos++] = '\0';
	close(fd[0]);
	write(fd[1], sig, 255);
	close(fd[1]);
	return 0;
}

void JGrequest_handle()
{
	char c, sig[255], input1[255], input2[255], input3[255];
	int i, addr, writeValue;

	close(fd[1]);
	read(fd[0], sig, 255);
	close(fd[0]);
	sscanf(sig, "%s%s%s", input1, input2, input3);

	addr = 0;
	for (i = 0; i < strlen(input1); i++)
		addr = addr * 10 + (input1[i] - '0');
	ptr_memAccReq->virAddr = addr;

	c = input2[0];
	switch (c){
		case 'r': case 'R': 
			ptr_memAccReq->reqType = REQUEST_READ;
			printf("请求：\n地址：%u\t类型：读取\n", ptr_memAccReq->virAddr);
		break;
		case 'w': case 'W': 
			ptr_memAccReq->reqType = REQUEST_WRITE;

			writeValue = 0;
			for (i = 0; i < strlen(input3); i++)
				writeValue = writeValue * 10 + (input3[i] - '0');
			ptr_memAccReq->value = writeValue;
			printf("请求：\n地址：%u\t类型：写入\n", ptr_memAccReq->virAddr);
		break;
		case 'x': case 'X': 
			ptr_memAccReq->reqType = REQUEST_EXECUTE;
			printf("产生请求：\n地址：%u\t类型：执行\n", ptr_memAccReq->virAddr);
		break;
	}
}

void JGcreateRequestProcess()
{
	pid_t pid;
	pipe(fd);
	if ((pid = fork()) < 0){
		printf("fork failed.\n");
		exit(0);
	}
	if (pid == 0){ // child process
		while (JGdo_request()) ;
		exit(0);
	}
	else{
		waitpid(-1, NULL, 0);
		JGrequest_handle();
	}
}

/* 初始化环境 */
void do_init()
{
	int i, j;
	srandom(time(NULL));
	for (i = 0; i < PAGE_SUM; i++)
	{
		pageTable[i].pageNum = i;
		pageTable[i].filled = FALSE;
		pageTable[i].edited = FALSE;
		pageTable[i].count = 0;
		/* 使用随机数设置该页的保护类型 */
		switch (random() % 7)
		{
			case 0:
			{
				pageTable[i].proType = READABLE;
				break;
			}
			case 1:
			{
				pageTable[i].proType = WRITABLE;
				break;
			}
			case 2:
			{
				pageTable[i].proType = EXECUTABLE;
				break;
			}
			case 3:
			{
				pageTable[i].proType = READABLE | WRITABLE;
				break;
			}
			case 4:
			{
				pageTable[i].proType = READABLE | EXECUTABLE;
				break;
			}
			case 5:
			{
				pageTable[i].proType = WRITABLE | EXECUTABLE;
				break;
			}
			case 6:
			{
				pageTable[i].proType = READABLE | WRITABLE | EXECUTABLE;
				break;
			}
			default:
				break;
		}
		/* 设置该页对应的辅存地址 */
		pageTable[i].auxAddr = i * PAGE_SIZE * 2;
	}
	JGinitPageCatalogue();
	for (j = 0; j < BLOCK_SUM; j++)
	{
		/* 随机选择一些物理块进行页面装入 */
		if (random() % 2 == 0)
		{
			do_page_in(&pageTable[j], j);
			//pageTable[j].blockNum = j;
			//pageTable[j].filled = TRUE;
			blockStatus[j] = TRUE;
			JGrandomPageIn(j);
		}
		else
			blockStatus[j] = FALSE;
	}
}


/* 响应请求 */
void do_response()
{
	Ptr_PageTableItem ptr_pageTabIt;
	Ptr_PageCatalogueItem ptr_pageCatalogueItem;
	unsigned int JGpageCatalogueNum, JGpageCatalogueOffset, pageNum, offAddr;
	unsigned int actAddr;
	
	/* 检查地址是否越界 */
	if (ptr_memAccReq->virAddr < 0 || ptr_memAccReq->virAddr >= VIRTUAL_MEMORY_SIZE)
	{
		do_error(ERROR_OVER_BOUNDARY);
		return;
	}
	
	/* 计算页号和页内偏移值 */
	//pageNum = ptr_memAccReq->virAddr / PAGE_SIZE;
	JGpageCatalogueNum = JGcalPageCatalogueNum();
	JGpageCatalogueOffset = JGcalPageCatalogueOffset();
	offAddr = ptr_memAccReq->virAddr % PAGE_SIZE;
	//printf("页号为：%u\t页内偏移为：%u\n", pageNum, offAddr);
	JGresponsePrint(JGpageCatalogueNum, JGpageCatalogueOffset, offAddr);

	/* 获取对应页表项 */
	ptr_pageCatalogueItem = &JGpageCatalogue[JGpageCatalogueNum];
	if (!ptr_pageCatalogueItem->filled)
		JGdo_page_catalogue_fault(ptr_pageCatalogueItem);
	pageNum = ptr_pageCatalogueItem->pageNum * PAGE_SIZE + JGpageCatalogueOffset;
	ptr_pageTabIt = &pageTable[pageNum];
	
	/* 根据特征位决定是否产生缺页中断 */
	if (!ptr_pageTabIt->filled)
	{
		do_page_fault(ptr_pageTabIt);
	}
	
	actAddr = ptr_pageTabIt->blockNum * PAGE_SIZE + offAddr;
	printf("实地址为：%u\n", actAddr);
	
	/* 检查页面访问权限并处理访存请求 */
	switch (ptr_memAccReq->reqType)
	{
		case REQUEST_READ: //读请求
		{
			ptr_pageTabIt->count++;
			if (!(ptr_pageTabIt->proType & READABLE)) //页面不可读
			{
				do_error(ERROR_READ_DENY);
				return;
			}
			/* 读取实存中的内容 */
			printf("读操作成功：值为%02X\n", actMem[actAddr]);
			break;
		}
		case REQUEST_WRITE: //写请求
		{
			ptr_pageTabIt->count++;
			if (!(ptr_pageTabIt->proType & WRITABLE)) //页面不可写
			{
				do_error(ERROR_WRITE_DENY);	
				return;
			}
			/* 向实存中写入请求的内容 */
			actMem[actAddr] = ptr_memAccReq->value;
			ptr_pageTabIt->edited = TRUE;			
			printf("写操作成功\n");
			break;
		}
		case REQUEST_EXECUTE: //执行请求
		{
			ptr_pageTabIt->count++;
			if (!(ptr_pageTabIt->proType & EXECUTABLE)) //页面不可执行
			{
				do_error(ERROR_EXECUTE_DENY);
				return;
			}			
			printf("执行成功\n");
			break;
		}
		default: //非法请求类型
		{	
			do_error(ERROR_INVALID_REQUEST);
			return;
		}
	}
}

/* 处理缺页中断 */
void do_page_fault(Ptr_PageTableItem ptr_pageTabIt)
{
	unsigned int i;
	printf("产生缺页中断，开始进行调页...\n");
	for (i = 0; i < BLOCK_SUM; i++)
	{
		if (!blockStatus[i])
		{
			/* 读辅存内容，写入到实存 */
			do_page_in(ptr_pageTabIt, i);
			
			/* 更新页表内容 */
			ptr_pageTabIt->blockNum = i;
			ptr_pageTabIt->filled = TRUE;
			ptr_pageTabIt->edited = FALSE;
			ptr_pageTabIt->count = 0;
			
			blockStatus[i] = TRUE;
			return;
		}
	}
	/* 没有空闲物理块，进行页面替换 */
	do_LFU(ptr_pageTabIt);
}

/* 根据LFU算法进行页面替换 */
void do_LFU(Ptr_PageTableItem ptr_pageTabIt)
{
	unsigned int i, min, page;
	printf("没有空闲物理块，开始进行LFU页面替换...\n");
	for (i = 0, min = 0xFFFFFFFF, page = 0; i < PAGE_SUM; i++)
	{
		if (pageTable[i].count < min)
		{
			min = pageTable[i].count;
			page = i;
		}
	}
	printf("选择第%u页进行替换\n", page);
	if (pageTable[page].edited)
	{
		/* 页面内容有修改，需要写回至辅存 */
		printf("该页内容有修改，写回至辅存\n");
		do_page_out(&pageTable[page]);
	}
	pageTable[page].filled = FALSE;
	pageTable[page].count = 0;


	/* 读辅存内容，写入到实存 */
	do_page_in(ptr_pageTabIt, pageTable[page].blockNum);
	
	/* 更新页表内容 */
	ptr_pageTabIt->blockNum = pageTable[page].blockNum;
	ptr_pageTabIt->filled = TRUE;
	ptr_pageTabIt->edited = FALSE;
	ptr_pageTabIt->count = 0;
	printf("页面替换成功\n");
}

/* 将辅存内容写入实存 */
void do_page_in(Ptr_PageTableItem ptr_pageTabIt, unsigned int blockNum)
{
	unsigned int readNum;
	if (fseek(ptr_auxMem, ptr_pageTabIt->auxAddr, SEEK_SET) < 0)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%u\tftell=%u\n", ptr_pageTabIt->auxAddr, ftell(ptr_auxMem));
#endif
		do_error(ERROR_FILE_SEEK_FAILED);
		exit(1);
	}
	if ((readNum = fread(actMem + blockNum * PAGE_SIZE, 
		sizeof(BYTE), PAGE_SIZE, ptr_auxMem)) < PAGE_SIZE)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%u\tftell=%u\n", ptr_pageTabIt->auxAddr, ftell(ptr_auxMem));
		printf("DEBUG: blockNum=%u\treadNum=%u\n", blockNum, readNum);
		printf("DEGUB: feof=%d\tferror=%d\n", feof(ptr_auxMem), ferror(ptr_auxMem));
#endif
		do_error(ERROR_FILE_READ_FAILED);
		exit(1);
	}
	printf("调页成功：辅存地址%u-->>物理块%u\n", ptr_pageTabIt->auxAddr, blockNum);
}

/* 将被替换页面的内容写回辅存 */
void do_page_out(Ptr_PageTableItem ptr_pageTabIt)
{
	unsigned int writeNum;
	if (fseek(ptr_auxMem, ptr_pageTabIt->auxAddr, SEEK_SET) < 0)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%u\tftell=%u\n", ptr_pageTabIt, ftell(ptr_auxMem));
#endif
		do_error(ERROR_FILE_SEEK_FAILED);
		exit(1);
	}
	if ((writeNum = fwrite(actMem + ptr_pageTabIt->blockNum * PAGE_SIZE, 
		sizeof(BYTE), PAGE_SIZE, ptr_auxMem)) < PAGE_SIZE)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%u\tftell=%u\n", ptr_pageTabIt->auxAddr, ftell(ptr_auxMem));
		printf("DEBUG: writeNum=%u\n", writeNum);
		printf("DEGUB: feof=%d\tferror=%d\n", feof(ptr_auxMem), ferror(ptr_auxMem));
#endif
		do_error(ERROR_FILE_WRITE_FAILED);
		exit(1);
	}
	printf("写回成功：物理块%u-->>辅存地址%03X\n", ptr_pageTabIt->auxAddr, ptr_pageTabIt->blockNum);
}

/* 错误处理 */
void do_error(ERROR_CODE code)
{
	switch (code)
	{
		case ERROR_READ_DENY:
		{
			printf("访存失败：该地址内容不可读\n");
			break;
		}
		case ERROR_WRITE_DENY:
		{
			printf("访存失败：该地址内容不可写\n");
			break;
		}
		case ERROR_EXECUTE_DENY:
		{
			printf("访存失败：该地址内容不可执行\n");
			break;
		}		
		case ERROR_INVALID_REQUEST:
		{
			printf("访存失败：非法访存请求\n");
			break;
		}
		case ERROR_OVER_BOUNDARY:
		{
			printf("访存失败：地址越界\n");
			break;
		}
		case ERROR_FILE_OPEN_FAILED:
		{
			printf("系统错误：打开文件失败\n");
			break;
		}
		case ERROR_FILE_CLOSE_FAILED:
		{
			printf("系统错误：关闭文件失败\n");
			break;
		}
		case ERROR_FILE_SEEK_FAILED:
		{
			printf("系统错误：文件指针定位失败\n");
			break;
		}
		case ERROR_FILE_READ_FAILED:
		{
			printf("系统错误：读取文件失败\n");
			break;
		}
		case ERROR_FILE_WRITE_FAILED:
		{
			printf("系统错误：写入文件失败\n");
			break;
		}
		default:
		{
			printf("未知错误：没有这个错误代码\n");
		}
	}
}

/* 产生访存请求 */
void do_request()
{
	/* 随机产生请求地址 */
	ptr_memAccReq->virAddr = random() % VIRTUAL_MEMORY_SIZE;
	/* 随机产生请求类型 */
	switch (random() % 3)
	{
		case 0: //读请求
		{
			ptr_memAccReq->reqType = REQUEST_READ;
			printf("产生请求：\n地址：%u\t类型：读取\n", ptr_memAccReq->virAddr);
			break;
		}
		case 1: //写请求
		{
			ptr_memAccReq->reqType = REQUEST_WRITE;
			/* 随机产生待写入的值 */
			ptr_memAccReq->value = random() % 0xFFu;
			printf("产生请求：\n地址：%u\t类型：写入\t值：%02X\n", ptr_memAccReq->virAddr, ptr_memAccReq->value);
			break;
		}
		case 2:
		{
			ptr_memAccReq->reqType = REQUEST_EXECUTE;
			printf("产生请求：\n地址：%u\t类型：执行\n", ptr_memAccReq->virAddr);
			break;
		}
		default:
			break;
	}	
}

/* 打印页表 */
void do_print_info()
{
	unsigned int i, j, k;
	char str[4];
	printf("***********************页表***********************\n");
	printf("页号\t块号\t装入\t修改\t保护\t计数\t辅存\n");
	for (i = 0; i < PAGE_SUM; i++)
	{
		printf("%u\t%u \t%u\t%u\t%s\t%u  \t%u\n", i, pageTable[i].blockNum, pageTable[i].filled, 
			pageTable[i].edited, get_proType_str(str, pageTable[i].proType), 
			pageTable[i].count, pageTable[i].auxAddr);
	}
}

/* 获取页面保护类型字符串 */
char *get_proType_str(char *str, BYTE type)
{
	if (type & READABLE)
		str[0] = 'r';
	else
		str[0] = '-';
	if (type & WRITABLE)
		str[1] = 'w';
	else
		str[1] = '-';
	if (type & EXECUTABLE)
		str[2] = 'x';
	else
		str[2] = '-';
	str[3] = '\0';
	return str;
}

int main(int argc, char* argv[])
{
	char c;
	int i;
	if (!(ptr_auxMem = fopen(AUXILIARY_MEMORY, "r+")))
	{
		do_error(ERROR_FILE_OPEN_FAILED);
		exit(1);
	}

	do_init();
	JGdo_print_catalogue_info();
	do_print_info();
	ptr_memAccReq = (Ptr_MemoryAccessRequest) malloc(sizeof(MemoryAccessRequest));
	/* 在循环中模拟访存请求与处理过程 */
	while (TRUE)
	{
		//do_request();
		JGcreateRequestProcess();
		do_response();
		printf("按Y打印页表，辅存和实存，按其他键不打印...\n");
		if ((c = getchar()) == 'y' || c == 'Y'){
			JGdo_print_catalogue_info();
			do_print_info();
			JGdo_print_aux_info();
			JGdo_print_act_info();
		}
		while (c != '\n')
			c = getchar();
		printf("按X退出程序，按其他键继续...\n");
		if ((c = getchar()) == 'x' || c == 'X')
			break;
		while (c != '\n')
			c = getchar();
		//sleep(5000);
	}

	if (fclose(ptr_auxMem) == EOF)
	{
		do_error(ERROR_FILE_CLOSE_FAILED);
		exit(1);
	}
	return (0);
}
