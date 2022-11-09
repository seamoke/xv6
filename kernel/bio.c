// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13
extern uint ticks;

struct {
  struct spinlock lock[NBUCKET];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[NBUCKET];
} bcache;

int hash (int n) {
  int result = n % NBUCKET;
  return result;
}

void
binit(void)
{
  struct buf *b;
  for(int i=0;i<NBUCKET;i++)
    initlock(&bcache.lock[i], "bcache");

  // Create linked list of buffers
  //bcache.head[0].next= &bcache.buf[0];
  //bcache.buf->prev = &bcache.head[0];
  //bcache.buf[NBUF-1].next = 0;
  for(int i=0;i<NBUCKET;i++){
    bcache.head[i].next=&bcache.head[i];
    bcache.head[i].prev=&bcache.head[i];
  }
  int id=0;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head[id].next;
    bcache.head[id].next->prev = b;
    b->prev = &bcache.head[id];
    bcache.head[id].next = b;
    id=(id+1)%NBUCKET;
    initsleeplock(&b->lock, "buffer");
  }
}

int index_is_avaliable(int i,int j){
  int t=j-i;
  if(t<0){
    t+=NBUCKET;
  }
  if(t>=0&&t<=NBUCKET/2){
    return 1;
  }
  return 0;
}
// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf* b;
  int id = hash(blockno);
  //printf("block:%d id: %d ", blockno, id);
  acquire(&bcache.lock[id]);
  // Is the block already cached?
  b = bcache.head[id].next;
  //printf("addb:%d ", &b);
  while (b!=&bcache.head[id]) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.lock[id]);
      acquiresleep(&b->lock);
      return b;
    }
    b = b->next;
  }
  //release(&bcache.lock[id]);
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  int least_time = 0x3f3f3f3f;
  int min_index = -1;
  struct buf* temp = 0;
  //printf(" find ");
  for (int k = 0; k < NBUCKET; k++) {
    int i = (k + id) % NBUCKET;
    if (!index_is_avaliable(id, i))
      continue;
    if(id!=i)
      acquire(&bcache.lock[i]);
    b = bcache.head[i].next;
    while (b!=&bcache.head[i]) {
      if (b->refcnt == 0 && b->time < least_time) {
        if (min_index != -1 && min_index != i && min_index!=id)
          release(&bcache.lock[min_index]);
        least_time = b->time;
        min_index = i;
        temp = b;
      }
      b = b->next;
    }
    if (min_index != i && i!=id) {
      release(&bcache.lock[i]);
    }
  }
  if (min_index == -1) {
    printf("%d %d\n", &(*b), least_time);
    panic("bget: no buffers");
  }
  b = temp;
  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  b->refcnt = 1;
  if (min_index == id) {
    release(&bcache.lock[min_index]);
    acquiresleep(&b->lock);
    return b;
  }
  b->next->prev = b->prev;
  b->prev->next = b->next;
  
  b->next = bcache.head[id].next;
  bcache.head[id].next->prev = b;
  b->prev = &bcache.head[id];
  bcache.head[id].next = b;

  release(&bcache.lock[id]);
  release(&bcache.lock[min_index]);
  acquiresleep(&b->lock);
  return b;
} 
/* static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock[0]);

  // Is the block already cached?
  for(b = bcache.head[0].next; b ; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[0]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head[0].next; b ; b = b->next){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[0]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
} */
// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  /* for(int i=0;i<NBUCKET;i++){
    struct buf * temp = bcache.head[i].next;
    int count =0;
    while(temp!=&bcache.head[i]){
      count++;
      temp=temp->next;
    }
    if(i!=NBUCKET-1){
      printf("%d ",count);
    }
    else{
      printf("%d\n",count);
    }
  } */
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  int id = hash(b->blockno);
  acquire(&bcache.lock[id]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->time = ticks;
  }
  
  release(&bcache.lock[id]);
}

void
bpin(struct buf *b) {
  int id = hash(b->blockno);
  acquire(&bcache.lock[id]);
  b->refcnt++;
  release(&bcache.lock[id]);
}

void
bunpin(struct buf *b) {
  int id = hash(b->blockno);
  acquire(&bcache.lock[id]);
  b->refcnt--;
  release(&bcache.lock[id]);
}


