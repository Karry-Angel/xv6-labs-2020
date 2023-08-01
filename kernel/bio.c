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

#define NBUCKET 13    //add
#undef NBUF  //add
#define NBUF (NBUCKET * 3)    //add

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  //struct buf head;    delete
} bcache;

//add bgein
struct bucket {
  struct spinlock lock;
  struct buf head;
}hashtable[NBUCKET];

int
hash(uint dev, uint blockno)
{
  return blockno % NBUCKET;
}
//add end

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  //add begin
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
  }

  b = bcache.buf;
  for (int i = 0; i < NBUCKET; i++) {
    initlock(&hashtable[i].lock, "bcache_bucket");
    for (int j = 0; j < NBUF / NBUCKET; j++) {
      b->blockno = i; // hash(b) should equal to i
      b->next = hashtable[i].head.next;
      hashtable[i].head.next = b;
      b++;
    }
  }
  //add end
  // Create linked list of buffers
  //bcache.head.prev = &bcache.head;
  //bcache.head.next = &bcache.head;
  //for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    //b->next = bcache.head.next;
    //b->prev = &bcache.head;
    //initsleeplock(&b->lock, "buffer");
    //bcache.head.next->prev = b;
    //bcache.head.next = b;
  //}
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int idx = hash(dev, blockno);  //add
  struct bucket* bucket = hashtable + idx;   //add
  acquire(&bucket->lock);

  // Is the block already cached?
  //for(b = bcache.head.next; b != &bcache.head; b = b->next){
    //if(b->dev == dev && b->blockno == blockno){
      //b->refcnt++;
      //release(&bcache.lock);
      //acquiresleep(&b->lock);
      //return b;
    //}
  //}
  //add begin
  for(b = bucket->head.next; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bucket->lock);
      acquiresleep(&b->lock);
      // printf("Cached %p\n", b);
      return b;
    }
  }
  //add end

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  //add begin
  // First try to find in current bucket.
  int min_time = 0x8fffffff;
  struct buf* replace_buf = 0;

  for(b = bucket->head.next; b != 0; b = b->next){
    if(b->refcnt == 0 && b->timestamp < min_time) {
      replace_buf = b;
      min_time = b->timestamp;
    }
  }
  if(replace_buf) {
    // printf("Local %d %p\n", idx, replace_buf);
    goto find;
  }

  // Try to find in other bucket.
  acquire(&bcache.lock);
  refind:
  for(b = bcache.buf; b < bcache.buf + NBUF; b++) {
    if(b->refcnt == 0 && b->timestamp < min_time) {
      replace_buf = b;
      min_time = b->timestamp;
    }
  }
  if (replace_buf) {
    // remove from old bucket
    int ridx = hash(replace_buf->dev, replace_buf->blockno);
    acquire(&hashtable[ridx].lock);
    if(replace_buf->refcnt != 0)  // be used in another bucket's local find between finded and acquire
    {
      release(&hashtable[ridx].lock);
      goto refind;
    }
    struct buf *pre = &hashtable[ridx].head;
    struct buf *p = hashtable[ridx].head.next;
    while (p != replace_buf) {
      pre = pre->next;
      p = p->next;
    }
    pre->next = p->next;
    release(&hashtable[ridx].lock);
    // add to current bucket
    replace_buf->next = hashtable[idx].head.next;
    hashtable[idx].head.next = replace_buf;
    release(&bcache.lock);
    // printf("Global %d -> %d %p\n", ridx, idx, replace_buf);
    goto find;
  }
  else {
    panic("bget: no buffers");
  }

  find:
  replace_buf->dev = dev;
  replace_buf->blockno = blockno;
  replace_buf->valid = 0;
  replace_buf->refcnt = 1;
  release(&bucket->lock);
  acquiresleep(&replace_buf->lock);
  return replace_buf;
 //add end
 // for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
   // if(b->refcnt == 0) {
     // b->dev = dev;
      //b->blockno = blockno;
      //b->valid = 0;
      //b->refcnt = 1;
      //release(&bcache.lock);
      //acquiresleep(&b->lock);
      //return b;
    //}
  //}
  //panic("bget: no buffers");
}

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

  int idx = hash(b->dev, b->blockno);   //add

  acquire(&hashtable[idx].lock);        //add
  //acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    //b->next->prev = b->prev;
    //b->prev->next = b->next;
    //b->next = bcache.head.next;
    //b->prev = &bcache.head;
    //bcache.head.next->prev = b;
    //bcache.head.next = b;
    b->timestamp = ticks;     //add
  }
  
  //release(&bcache.lock);
  release(&hashtable[idx].lock);//add
}

void
bpin(struct buf *b) {
  //acquire(&bcache.lock);
  int idx = hash(b->dev, b->blockno);   //add
  acquire(&hashtable[idx].lock);        //add
  b->refcnt++;
  //release(&bcache.lock);
  release(&hashtable[idx].lock);       //add
}

void
bunpin(struct buf *b) {
  //acquire(&bcache.lock);
  int idx = hash(b->dev, b->blockno);    //add
  acquire(&hashtable[idx].lock);        //add
  b->refcnt--;
  //release(&bcache.lock);
  release(&hashtable[idx].lock);      //add
}


