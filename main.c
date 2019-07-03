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
#include "uvc-video.h"

#define THREAD_MAX_NUM 2 //1个CPU内的最多进程数


int main(int argc, char *argv[])
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
        	pthread_create(&thread[i],NULL,uvc_gadget_main,(void*)&tid[i]);
        }
        else
        {
        	pthread_create(&thread[i],NULL,uvc_video_main,(void*)&tid[i]);
        }
    }
    for(i=0; i< THREAD_MAX_NUM; i++)
    {
        pthread_join(thread[i],NULL);//等待所有的线程结束，线程为死循环所以CTRL+C结束
    }
    return 0;
}
