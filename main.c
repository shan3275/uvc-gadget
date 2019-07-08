#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#define __USE_GNU
#include <sched.h>
#include <ctype.h>
#include <string.h>
#include <pthread.h>

#include "uvc-gadget.h"
#include "uvc-yuv.h"
#include "uvc-jpg.h"
#include "mq_ring.h"

#define THREAD_MAX_NUM 3 //1个CPU内的最多进程数
ring_t* yuv_msgr = NULL;
ring_t* jpg_msgr = NULL;

static int thread_init(void)
{
	int tid[THREAD_MAX_NUM];
    int i;
    pthread_t thread[THREAD_MAX_NUM];
    int num = 0;

    num = sysconf(_SC_NPROCESSORS_CONF);  //获取核数
    if (num < THREAD_MAX_NUM) {
       printf("num of cores[%d] is smaller than THREAD_MAX_NUM[%d]!\n", num, THREAD_MAX_NUM);
       return -1;
    }
    printf("system has %i processor(s). \n", num);

    for(i=0;i<THREAD_MAX_NUM;i++)
    {
        tid[i] = i;  //每个线程必须有个tid[i]
        if (i == 0)
        {
        	pthread_create(&thread[i], NULL, uvc_gadget_main, (void*)&tid[i]);
        }
        else
        if (i == 1)
        {
        	pthread_create(&thread[i], NULL, uvc_yuv_main,    (void*)&tid[i]);
        }
        else
        {
        	pthread_create(&thread[i], NULL, uvc_jpg_main,    (void*)&tid[i]);
        }
    }
    for(i=0; i< THREAD_MAX_NUM; i++)
    {
        pthread_join(thread[i],NULL);//等待所有的线程结束，线程为死循环所以CTRL+C结束
    }
    return 0;
}

static ring_t  * ring_init(uint32_t size)
{
	uint32_t i;
	ring_t  *msgr = NULL;
	mq_pic_t *pic = NULL;
	printf("initialize ring begin\n");
	msgr = initialize_ring(size);
	if (!msgr)
	{
		printf("init ring fail! exit\n");
		return NULL;
	}
	for (i = 0; i < size; i++)
	{
		pic = calloc(1,sizeof(mq_pic_t));
		if (pic)
		{
			msgr->idx_buff[i] = (uint64_t *)pic;
		}
		else
		{
			printf("calloc mq_pic_t fail, when i = %d\n", i);
			return NULL;
		}
	}
	printf("initialize ring success!\n");
	return msgr;
}

int main(int argc, char *argv[])
{	int rv;
	yuv_msgr = ring_init(MQ_PIC_RING_SIZE);
	if (!yuv_msgr)
	{
		printf("initialize yuv fail! exit\n");
		return 0;
	}
	jpg_msgr = ring_init(MQ_PIC_RING_SIZE);
	if (!yuv_msgr)
	{
		printf("initialize jpg fail! exit\n");
		return 0;
	}
	thread_init();
	return 0;
}

