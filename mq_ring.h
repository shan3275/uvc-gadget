#ifndef _MQ_RING_
#define _MQ_RING_
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>

typedef struct ring_s {
    uint64_t *idx_buff;     /* the buffer holding the data pointer */
    uint32_t size;         /* the size of the allocated buffer */
    uint32_t in;           /* data is added at offset (in % size) */
    uint32_t out;          /* data is extracted from off. (out % size) */
}ring_t;

#define MQ_PIC_RING_SIZE   32
#define MQ_PIC_BUFF_LEN   614404
typedef struct mq_pic_s{
	    uint32_t len;
	    u_char   data[MQ_PIC_BUFF_LEN];
}mq_pic_t;

ring_t* initialize_ring(uint32_t size);
bool enring(ring_t *r, uint8_t *data, uint32_t size);
bool dering(ring_t *r, uint8_t *data, uint32_t *size);
void destory_ring( ring_t* r ) ;

#endif