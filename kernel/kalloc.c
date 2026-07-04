// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct frame {
  int in_use;
  int pinned;
  struct proc *owner;
  uint64 va;
  uint64 pa;
};

struct {
  struct spinlock lock;
  struct frame frames[NFRAMES];
  int clock_hand;
} frame_table;

// int
// count_free_pages(void)
// {
//   struct run *r;
//   int count = 0;

//   acquire(&kmem.lock);
//   for(r = kmem.freelist; r; r = r->next)
//     count++;
//   release(&kmem.lock);

//   return count;
// }

void
register_frame(uint64 pa, struct proc *owner, uint64 va)
{
  acquire(&frame_table.lock);
  for (int i = 0; i < NFRAMES; i++){
    if (frame_table.frames[i].in_use == 0){
      frame_table.frames[i].in_use = 1;
      frame_table.frames[i].pinned = 0;
      frame_table.frames[i].owner = owner;
      frame_table.frames[i].va = va;
      frame_table.frames[i].pa = pa;
      release(&frame_table.lock);
      return;
    }
  }
  release(&frame_table.lock);

  panic("register_frame: out of slots!");
}

void
pin_page(uint64 pa)
{
  acquire(&frame_table.lock);
  for(int i = 0; i < NFRAMES; i++){
    if(frame_table.frames[i].pa == pa && frame_table.frames[i].in_use){
      frame_table.frames[i].pinned = 1;
      break;
    }
  }
  release(&frame_table.lock);
}

void
unpin_page(uint64 pa)
{
  acquire(&frame_table.lock);
  for(int i = 0; i < NFRAMES; i++){
    if(frame_table.frames[i].pa == pa && frame_table.frames[i].in_use){
      frame_table.frames[i].pinned = 0;
      break;
    }
  }
  release(&frame_table.lock);
}

int
evict_page(void)
{
  pte_t *pte;
  
  acquire(&frame_table.lock);
  int start = frame_table.clock_hand, best_level = -1, best_index = -1;
  do{    
    if (frame_table.frames[frame_table.clock_hand].in_use == 1 && frame_table.frames[frame_table.clock_hand].pinned == 0){
      struct proc *p = frame_table.frames[frame_table.clock_hand].owner;
      uint64 va = frame_table.frames[frame_table.clock_hand].va;

      pte = walk(p->pagetable, va, 0);

      if (pte != 0 && (*pte & PTE_V)){
        if (!(*pte & PTE_U) || (*pte & PTE_X)){

        }
        else{
          if (*pte & PTE_A){
            *pte &= ~PTE_A;
          }
          else{
            if (best_index == -1){
              best_level = p -> level;
              best_index = frame_table.clock_hand;
            }
            else{
              if (best_level < p -> level){
                best_level = p -> level;
                best_index = frame_table.clock_hand;
              }
            }
          }
        }
      }
    }

    frame_table.clock_hand = (frame_table.clock_hand + 1) % NFRAMES;
  } while (frame_table.clock_hand != start || best_index == -1);

  struct proc *p = frame_table.frames[best_index].owner;
  uint64 va = frame_table.frames[best_index].va;
  uint64 pa = frame_table.frames[best_index].pa;

  // printf("PID of OWNER: %d", p -> pid);
  
  pte = walk(p->pagetable, va, 0);

  frame_table.frames[best_index].in_use = 0;
  frame_table.frames[best_index].owner = 0;
  frame_table.frames[best_index].va = 0;
  frame_table.frames[best_index].pa = 0;
  frame_table.clock_hand = (best_index + 1) % NFRAMES;
  release(&frame_table.lock);

  int swap_idx = write_to_swap(pa);
  if (swap_idx == -1){
    printf("SWAP FULL: no swap slots available\n");
    return -1;
  }

  (p -> mystats).pages_evicted++;
  (p -> mystats).pages_swapped_out++;
  if((p -> mystats).resident_page > 0) {
    (p -> mystats).resident_page--;
  }

  int flags = PTE_FLAGS(*pte);
  flags &= ~PTE_V;
  flags |= PTE_S;
  *pte = (swap_idx << 10) | flags;

  memset((void*)pa, 1, PGSIZE); 
  struct run *r = (struct run*)pa;
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);

  return 0;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&frame_table.lock, "frame_table");
  
  frame_table.clock_hand = 0;
  freerange(end, (void*)(KERNBASE + (NFRAMES * PGSIZE)));
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  acquire(&frame_table.lock);
  for (int i = 0; i < NFRAMES; i++){
    if (frame_table.frames[i].pa == (uint64)pa && frame_table.frames[i].in_use){
      frame_table.frames[i].in_use = 0;
      frame_table.frames[i].owner = 0;
      frame_table.frames[i].va = 0;
      break;
    }
  }
  release(&frame_table.lock);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  while(!r) {
    if (evict_page() == -1){
      return 0;
    }

    acquire(&kmem.lock);
    r = kmem.freelist;
    if (r){
      kmem.freelist = r -> next;
    }
    release(&kmem.lock);
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
