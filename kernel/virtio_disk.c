//
// driver for qemu's virtio disk device.
// uses qemu's mmio interface to virtio.
//
// qemu ... -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "virtio.h"
#include "proc.h"

// the address of virtio mmio register r.
#define R(r) ((volatile uint32 *)(VIRTIO0 + (r)))

// 0 for FCFS, 1 for SSTF
int disk_policy = 0;
uint current_head_position = 0;
#define C 10

// Define a single disk request
struct disk_req {
  struct buf *b;
  int write;
  int priority;
  struct proc *p;
  struct disk_req *next;
};

// We need a pool of these requests (similar to the NPROC pool for processes)
// Let's make a pool of 16 (matching the NUM descriptors)
struct disk_req req_pool[NBUF];
struct spinlock req_lock;

// Head and tail of our pending linked list
struct disk_req *req_queue_head = 0;

static struct disk {
  // a set (not a ring) of DMA descriptors, with which the
  // driver tells the device where to read and write individual
  // disk operations. there are NUM descriptors.
  // most commands consist of a "chain" (a linked list) of a couple of
  // these descriptors.
  struct virtq_desc *desc;

  // a ring in which the driver writes descriptor numbers
  // that the driver would like the device to process.  it only
  // includes the head descriptor of each chain. the ring has
  // NUM elements.
  struct virtq_avail *avail;

  // a ring in which the device writes descriptor numbers that
  // the device has finished processing (just the head of each chain).
  // there are NUM used ring entries.
  struct virtq_used *used;

  // our own book-keeping.
  char free[NUM];  // is a descriptor free?
  uint16 used_idx; // we've looked this far in used[2..NUM].

  // track info about in-flight operations,
  // for use when completion interrupt arrives.
  // indexed by first descriptor index of chain.
  struct {
    struct buf *b;
    char status;
  } info[NUM];

  // disk command headers.
  // one-for-one with descriptors, for convenience.
  struct virtio_blk_req ops[NUM];
  
  struct spinlock vdisk_lock;
  
} disk;

void
virtio_disk_init(void)
{
  uint32 status = 0;

  initlock(&disk.vdisk_lock, "virtio_disk");
  initlock(&req_lock, "disk_req_queue");

  if(*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
     *R(VIRTIO_MMIO_VERSION) != 2 ||
     *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||
     *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551){
    panic("could not find virtio disk");
  }
  
  // reset device
  *R(VIRTIO_MMIO_STATUS) = status;

  // set ACKNOWLEDGE status bit
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(VIRTIO_MMIO_STATUS) = status;

  // set DRIVER status bit
  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(VIRTIO_MMIO_STATUS) = status;

  // negotiate features
  uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
  features &= ~(1 << VIRTIO_BLK_F_RO);
  features &= ~(1 << VIRTIO_BLK_F_SCSI);
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
  features &= ~(1 << VIRTIO_BLK_F_MQ);
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

  // tell device that feature negotiation is complete.
  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // re-read status to ensure FEATURES_OK is set.
  status = *R(VIRTIO_MMIO_STATUS);
  if(!(status & VIRTIO_CONFIG_S_FEATURES_OK))
    panic("virtio disk FEATURES_OK unset");

  // initialize queue 0.
  *R(VIRTIO_MMIO_QUEUE_SEL) = 0;

  // ensure queue 0 is not in use.
  if(*R(VIRTIO_MMIO_QUEUE_READY))
    panic("virtio disk should not be ready");

  // check maximum queue size.
  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if(max == 0)
    panic("virtio disk has no queue 0");
  if(max < NUM)
    panic("virtio disk max queue too short");

  // allocate and zero queue memory.
  disk.desc = kalloc();
  disk.avail = kalloc();
  disk.used = kalloc();
  if(!disk.desc || !disk.avail || !disk.used)
    panic("virtio disk kalloc");
  memset(disk.desc, 0, PGSIZE);
  memset(disk.avail, 0, PGSIZE);
  memset(disk.used, 0, PGSIZE);

  // set queue size.
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

  // write physical addresses.
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)disk.desc;
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)disk.desc >> 32;
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)disk.avail;
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)disk.avail >> 32;
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)disk.used;
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)disk.used >> 32;

  // queue is ready.
  *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;

  // all NUM descriptors start out unused.
  for(int i = 0; i < NUM; i++)
    disk.free[i] = 1;

  // tell device we're completely ready.
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // plic.c and trap.c arrange for interrupts from VIRTIO0_IRQ.
}

// find a free descriptor, mark it non-free, return its index.
static int
alloc_desc()
{
  for(int i = 0; i < NUM; i++){
    if(disk.free[i]){
      disk.free[i] = 0;
      return i;
    }
  }
  return -1;
}

// mark a descriptor as free.
static void
free_desc(int i)
{
  if(i >= NUM)
    panic("free_desc 1");
  if(disk.free[i])
    panic("free_desc 2");
  disk.desc[i].addr = 0;
  disk.desc[i].len = 0;
  disk.desc[i].flags = 0;
  disk.desc[i].next = 0;
  disk.free[i] = 1;
  wakeup(&disk.free[0]);
}

// free a chain of descriptors.
static void
free_chain(int i)
{
  while(1){
    int flag = disk.desc[i].flags;
    int nxt = disk.desc[i].next;
    free_desc(i);
    if(flag & VRING_DESC_F_NEXT)
      i = nxt;
    else
      break;
  }
}

// allocate three descriptors (they need not be contiguous).
// disk transfers always use three descriptors.
static int
alloc3_desc(int *idx)
{
  for(int i = 0; i < 3; i++){
    idx[i] = alloc_desc();
    if(idx[i] < 0){
      for(int j = 0; j < i; j++)
        free_desc(idx[j]);
      return -1;
    }
  }
  return 0;
}

struct disk_req* 
alloc_req(void)
{
  acquire(&req_lock);
  while (1){
    for (int i = 0; i < NBUF; i++){
      if (req_pool[i].b == 0){
        req_pool[i].b = (struct buf *)-1;
        req_pool[i].next = 0;
        release(&req_lock);
        return &req_pool[i];
      }
    }
    sleep(&req_pool, &req_lock);
  }
}

void
disk_schedule(void)
{
  struct disk_req *r;
  struct buf *b;
  int write;
  int idx[3];

  acquire(&disk.vdisk_lock);
  if (alloc3_desc(idx) != 0) {
    release(&disk.vdisk_lock);
    return;
  }

  acquire(&req_lock);  
  if (req_queue_head == 0){
    free_desc(idx[0]);
    free_desc(idx[1]);
    free_desc(idx[2]);
    release(&req_lock);
    release(&disk.vdisk_lock);
    return;
  }

  struct disk_req *curr = req_queue_head, *first_req = 0, *first_req_prev = 0, *best_req = 0, *prev = 0, *best_prev = 0;
  int max_priority = 4;
  uint min_distance = 0xFFFFFFFF;
  while (curr != 0){
    if (curr -> priority < max_priority){
      max_priority = curr -> priority;
      first_req = curr;
      first_req_prev = prev;

      uint distance;
      if (curr -> b -> blockno > current_head_position){
        distance = curr -> b -> blockno - current_head_position;
      }
      else{
        distance = current_head_position - curr -> b -> blockno;
      }
      
      min_distance = distance;
      best_req = curr;
      best_prev = prev;
    }
    else if (curr -> priority == max_priority){
      uint distance;
      if (curr -> b -> blockno > current_head_position){
        distance = curr -> b -> blockno - current_head_position;
      }
      else{
        distance = current_head_position - curr -> b -> blockno;
      }

      if (disk_policy == 1 && distance < min_distance){
        min_distance = distance;
        best_req = curr;
        best_prev = prev;
      }
    }

    prev = curr;
    curr = curr -> next;
  }

  if (disk_policy == 0){
    if (first_req_prev == 0){
      req_queue_head = first_req -> next;
    }
    else{
      first_req_prev -> next = first_req -> next;
    }

    r = first_req;
  }
  else{
    if (best_prev == 0){
      req_queue_head = best_req -> next;
    }
    else{
      best_prev -> next = best_req -> next;
    }

    r = best_req;
  }
  
  current_head_position = r -> b -> blockno;

  uint latency = min_distance + C;
  if (r -> p != 0){
    if (r -> write){
      r -> p -> mystats.disk_writes++;
    }
    else{
      r -> p -> mystats.disk_reads++;
    }

    r -> p -> mystats.total_disk_latency += latency;
    r -> p -> mystats.total_disk_requests++;
    r -> p -> mystats.avg_disk_latency = r -> p -> mystats.total_disk_latency / r -> p -> mystats.total_disk_requests;
  }

  // // Simulating the latency delay
  // for (volatile int i = 0; i < latency; i++){
  // }
  
  b = r->b;
  write = r->write;
  
  // Free the ticket
  r->b = 0;
  r->next = 0;
  wakeup(&req_pool);
  release(&req_lock);
  
  // 4. SUBMIT TO HARDWARE
  uint64 sector = b->blockno * (BSIZE / 512);

  // We already have idx[3] from the top of the function, so we format directly
  struct virtio_blk_req *buf0 = &disk.ops[idx[0]];

  if(write)
    buf0->type = VIRTIO_BLK_T_OUT; 
  else
    buf0->type = VIRTIO_BLK_T_IN; 
    
  buf0->reserved = 0;
  buf0->sector = sector;

  disk.desc[idx[0]].addr = (uint64) buf0;
  disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
  disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
  disk.desc[idx[0]].next = idx[1];

  disk.desc[idx[1]].addr = (uint64) b->data;
  disk.desc[idx[1]].len = BSIZE;
  if(write)
    disk.desc[idx[1]].flags = 0; 
  else
    disk.desc[idx[1]].flags = VRING_DESC_F_WRITE; 
    
  disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  disk.desc[idx[1]].next = idx[2];

  disk.info[idx[0]].status = 0xff; 
  disk.desc[idx[2]].addr = (uint64) &disk.info[idx[0]].status;
  disk.desc[idx[2]].len = 1;
  disk.desc[idx[2]].flags = VRING_DESC_F_WRITE; 
  disk.desc[idx[2]].next = 0;

  disk.info[idx[0]].b = b;

  disk.avail->ring[disk.avail->idx % NUM] = idx[0];

  __sync_synchronize();

  disk.avail->idx += 1; 

  __sync_synchronize();

  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; 

  release(&disk.vdisk_lock);
}

void
virtio_disk_rw(struct buf *b, int write)
{
  struct disk_req *r = alloc_req();
  r -> b = b;
  r -> write = write;
  r -> priority = myproc() -> level;
  r -> p = myproc();

  // Mark the buffer as busy BEFORE adding it to the queue
  acquire(&disk.vdisk_lock);
  b->disk = 1;
  release(&disk.vdisk_lock);

  // 2. Add it to the end of our pending queue
  acquire(&req_lock);
  if (req_queue_head == 0){
    req_queue_head = r;
  }
  else{
    struct disk_req *curr = req_queue_head;
    while (curr -> next != 0){
      curr = curr -> next;
    }
    
    curr -> next = r;
  }
  release(&req_lock);

  disk_schedule();

  // 4. Wait for the request to be finished
  acquire(&disk.vdisk_lock);
  while (b -> disk == 1){
    sleep(b, &disk.vdisk_lock);
  }
  release(&disk.vdisk_lock);
}

void
virtio_disk_intr()
{
  acquire(&disk.vdisk_lock);

  // the device won't raise another interrupt until we tell it
  // we've seen this interrupt, which the following line does.
  // this may race with the device writing new entries to
  // the "used" ring, in which case we may process the new
  // completion entries in this interrupt, and have nothing to do
  // in the next interrupt, which is harmless.
  *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

  __sync_synchronize();

  // the device increments disk.used->idx when it
  // adds an entry to the used ring.

  while(disk.used_idx != disk.used->idx){
    __sync_synchronize();
    int id = disk.used->ring[disk.used_idx % NUM].id;

    if(disk.info[id].status != 0)
      panic("virtio_disk_intr status");

    struct buf *b = disk.info[id].b;
    disk.info[id].b = 0;
    free_chain(id);
    b->disk = 0;   // disk is done with buf
    wakeup(b);

    disk.used_idx += 1;
  }

  release(&disk.vdisk_lock);

  disk_schedule();
}
