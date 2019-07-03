#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#define __USE_GNU
#include <sched.h>
#include <ctype.h>
#include <string.h>
#include "thread_bind_core.h"

void thread_bind_core(int thread_id)  //arg  传递线程标号（自己定义）
{
     cpu_set_t mask;  //CPU核的集合
     cpu_set_t get;   //获取在集合中的CPU
     int i;
     int num=0;  //cpu中核数

     num = sysconf(_SC_NPROCESSORS_CONF);
     printf("the thread is:%d\n",thread_id);  //显示是第几个线程
     CPU_ZERO(&mask);    //置空
     CPU_SET(thread_id,&mask);   //设置亲和力值
     if (sched_setaffinity(0, sizeof(mask), &mask) == -1)//设置线程CPU亲和力
     {
          printf("warning: could not set CPU affinity, continuing...\n");
     }

     CPU_ZERO(&get);
     if (sched_getaffinity(0, sizeof(get), &get) == -1)//获取线程CPU亲和力
     {
          printf("warning: cound not get thread affinity, continuing...\n");
     }
     for (i = 0; i < num; i++)
     {
          if (CPU_ISSET(i, &get))//判断线程与哪个CPU有亲和力
          {
               printf("this thread %d is running processor : %d\n", i,i);
          }
     }
}