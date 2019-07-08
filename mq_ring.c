#include "mq_ring.h"

/*
* 参数size 代表fifo的大小，必须为2的幂次方
*/
ring_t* initialize_ring(uint32_t size) 
{
   ring_t *r = calloc(1, sizeof(ring_t));
   r->idx_buff = calloc(size, sizeof(uint64_t));
   r->size     = size;
   r->in = r->out = 0;
   return r;
}
 
bool enring(ring_t *r, uint8_t *data, uint32_t size) 
{
   mq_pic_t* pic = NULL;
   if (r->in - r->out == r->size)
   {
      return false;
   }
   pic = (mq_pic_t *)r->idx_buff[r->in & (r->size-1)];
   memcpy(pic->data, data, size);
   pic->len = size;
   ++r->in;
   return true;
}
 

bool dering(ring_t *r, uint8_t *data, uint32_t *size) 
{
   mq_pic_t* pic = NULL;
   if(r->in == r->out)
   {
      return false;
   }
   pic = (mq_pic_t *)r->idx_buff[r->out & (r->size-1)];
   //memset(data,0,pic->len);
   memcpy(data, pic->data, pic->len);
   *size = pic->len;
   ++r->out; 
   return true;           
}

void destory_ring( ring_t* r)
{ 
   int i;
   mq_pic_t *pkt=NULL;
   for (i = 0; i < r->size; ++i)
   {
      pkt = (mq_pic_t *)r->idx_buff[i];
      if (pkt)
      {
         free(pkt);
      }
      else
      {
         break;
      }
   }
   if (r->idx_buff)
   {
      free(r->idx_buff);
   }
   free(r);
}
	
