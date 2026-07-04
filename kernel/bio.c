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

#define VIRTUAL_DISK_START (FSSIZE / 5)
#define VIRTUAL_DISK_SIZE (FSSIZE / 5)
#define DISK_0_START (VIRTUAL_DISK_START)
#define DISK_1_START (VIRTUAL_DISK_START + VIRTUAL_DISK_SIZE)
#define DISK_2_START (VIRTUAL_DISK_START + VIRTUAL_DISK_SIZE * 2)
#define DISK_3_START (VIRTUAL_DISK_START + VIRTUAL_DISK_SIZE * 3)

uint disk_starts[4] = {DISK_0_START, DISK_1_START, DISK_2_START, DISK_3_START};
int raid_policy = 5;
int disk_fail = -1;

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
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

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}

void
raid_write(uint logical_block, uint64 pa_offset)
{
  if (raid_policy == 0){
    int target_disk = logical_block % 4; 
    int block_offset = logical_block / 4;
    uint physical_block = 0;

    if (target_disk == 0){
      physical_block = DISK_0_START + block_offset;
    }
    else if (target_disk == 1){
      physical_block = DISK_1_START + block_offset;
    }
    else if (target_disk == 2){
      physical_block = DISK_2_START + block_offset;
    }
    else if (target_disk == 3){
      physical_block = DISK_3_START + block_offset;
    }
    
    if (block_offset >= VIRTUAL_DISK_SIZE){
      panic("RAID write overflow!");
    }
    
    if (target_disk != disk_fail){
      struct buf *b = bread(1, physical_block);
      memmove(b -> data, (void*)pa_offset, BSIZE);
      bwrite(b);
      brelse(b);
    }
  }
  else if (raid_policy == 1){
    int target_pair = logical_block % 2; 
    int block_offset = logical_block / 2;

    int primary_disk = target_pair * 2;
    int mirror_disk = primary_disk + 1;
    
    if (block_offset >= VIRTUAL_DISK_SIZE){
      panic("RAID write overflow!");
    }

    if (primary_disk != disk_fail){
      uint primary_physical = disk_starts[primary_disk] + block_offset;

      struct buf *b1 = bread(1, primary_physical);
      memmove(b1 -> data, (void*)pa_offset, BSIZE);
      bwrite(b1);
      brelse(b1);
    }
    
    if (mirror_disk != disk_fail){
      uint mirror_physical = disk_starts[mirror_disk] + block_offset;

      struct buf *b2 = bread(1, mirror_physical);
      memmove(b2 -> data, (void*)pa_offset, BSIZE);
      bwrite(b2);
      brelse(b2);
    }
  }
  else{
    uint stripe = logical_block / 3;
    uint parity_disk = stripe % 4;
    
    uint target_disk = logical_block % 3;
    if (target_disk >= parity_disk){
      target_disk++;
    }
    
    uint block_offset = stripe;
    if (block_offset >= VIRTUAL_DISK_SIZE){
      panic("RAID write overflow!");
    }

    uint target_physical = disk_starts[target_disk] + block_offset;
    uint parity_physical = disk_starts[parity_disk] + block_offset;

    struct buf *b_old_data = bread(1, target_physical);
    struct buf *b_parity_data = bread(1, parity_physical);
    
    char *new_data = (char *)pa_offset;

    for (int i = 0; i < BSIZE; i++){
      b_parity_data -> data[i] = b_old_data -> data[i] ^ new_data[i] ^ b_parity_data -> data[i];
    }

    memmove(b_old_data -> data, new_data, BSIZE);
    bwrite(b_old_data);
    bwrite(b_parity_data);
    brelse(b_old_data);
    brelse(b_parity_data);
  }
}

void
raid_read(uint logical_block, uint64 pa_offset)
{
  if (raid_policy == 0){
    int target_disk = logical_block % 4; 
    int block_offset = logical_block / 4;
    uint physical_block = 0;
    
    if (target_disk == 0){
      physical_block = DISK_0_START + block_offset;
    }
    else if (target_disk == 1){
      physical_block = DISK_1_START + block_offset;
    }
    else if (target_disk == 2){
      physical_block = DISK_2_START + block_offset;
    }
    else if (target_disk == 3){
      physical_block = DISK_3_START + block_offset;
    }
    
    if (block_offset >= VIRTUAL_DISK_SIZE){
      panic("RAID write overflow!");
    }

    if (target_disk == disk_fail){
      memset((void*)pa_offset, 0, BSIZE);      
      return;
    }
    
    struct buf *b = bread(1, physical_block);
    memmove((void*)pa_offset, b -> data, BSIZE);
    brelse(b);
  }
  else if (raid_policy == 1){
    int target_pair = logical_block % 2;
    int block_offset = logical_block / 2;
    
    int primary_disk = target_pair * 2;
    
    if (block_offset >= VIRTUAL_DISK_SIZE){
      panic("RAID read overflow!");
    }

    if (primary_disk == disk_fail){
      primary_disk++;
    }
    
    uint primary_physical = disk_starts[primary_disk] + block_offset;
    
    struct buf *b = bread(1, primary_physical);
    memmove((void*)pa_offset, b -> data, BSIZE);
    brelse(b);
  }
  else{
    uint stripe = logical_block / 3;
    uint parity_disk = stripe % 4;

    uint target_disk = logical_block % 3;
    if (target_disk >= parity_disk){
      target_disk++;
    }
    
    uint block_offset = stripe;
    if (block_offset >= VIRTUAL_DISK_SIZE){
      panic("RAID read overflow!");
    }

    if (target_disk != disk_fail){
      uint target_physical = disk_starts[target_disk] + block_offset;

      struct buf *b = bread(1, target_physical);
      memmove((void*)pa_offset, b -> data, BSIZE);
      brelse(b);
    }
    else{
      char reconstructed_data[BSIZE];
      memset(reconstructed_data, 0, BSIZE);

      for (int d = 0; d < 4; d++){
          if (d == target_disk){
            continue;
          }
          
          uint target_physical = disk_starts[d] + block_offset;
          struct buf *b = bread(1, target_physical);
          
          for (int i = 0; i < BSIZE; i++){
            reconstructed_data[i] ^= b -> data[i];
          }

          brelse(b);
      }

      memmove((void*)pa_offset, reconstructed_data, BSIZE);
    }
  }
}