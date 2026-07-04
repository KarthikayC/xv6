#include "kernel/types.h"
#include "user/user.h"

#define ALLOC_PAGES 120
#define NCHILDREN 6
#define PAGE_SIZE 4096
#define STRIDE (PAGE_SIZE)

static char *
lazy_alloc(int nbytes)
{
  return (char *)sbrklazy(nbytes);
}

static void
print_vmstats(struct vmstats *s)
{
  printf("page_faults      : %d\n", s->page_faults);
  printf("pages_evicted    : %d\n", s->pages_evicted);
  printf("pages_swapped_in : %d\n", s->pages_swapped_in);
  printf("pages_swapped_out: %d\n", s->pages_swapped_out);
  printf("resident_pages   : %d\n", s->resident_pages);
  printf("disk_reads       : %d\n", s->disk_reads);
  printf("disk_writes      : %d\n", s->disk_writes);
  printf("avg_disk_latency : %d\n", s->avg_disk_latency);
}

static void
test_swap_correctness(void)
{
  printf("\n=== TEST 1: swap-in/swap-out correctness ===\n");

  int pid = getpid();
  struct vmstats before, after;
  getvmstats(pid, &before);

  int nbytes = ALLOC_PAGES * PAGE_SIZE;
  char *mem = lazy_alloc(nbytes);
  if (mem == (char *)-1){
    printf("FAIL: sbrk failed\n");
    exit(1);
  }

  printf("Writing pattern to %d pages\n", ALLOC_PAGES);
  for (int i = 0; i < ALLOC_PAGES; i++){
    mem[i * PAGE_SIZE] = (char)(i & 0xFF);
  }

  printf("Reading back and verifying\n");
  int errors = 0;
  for (int i = 0; i < ALLOC_PAGES; i++){
    char expected = (char)(i & 0xFF);
    if (mem[i * PAGE_SIZE] != expected){
      printf("MISMATCH at page %d: got %d, expected %d\n", i, (int)(unsigned char)mem[i * PAGE_SIZE], (int)(unsigned char)expected);
      errors++;
    }
  }

  if (errors == 0){
    printf("PASS: all %d pages verified correctly\n", ALLOC_PAGES);
  }
  else{
    printf("FAIL: %d page(s) had wrong data\n", errors);
  }

  getvmstats(pid, &after);
  struct vmstats delta;
  delta.page_faults = after.page_faults - before.page_faults;
  delta.pages_evicted = after.pages_evicted - before.pages_evicted;
  delta.pages_swapped_in = after.pages_swapped_in - before.pages_swapped_in;
  delta.pages_swapped_out = after.pages_swapped_out - before.pages_swapped_out;
  delta.resident_pages = after.resident_pages;
  delta.disk_reads = after.disk_reads - before.disk_reads;
  delta.disk_writes = after.disk_writes - before.disk_writes;
  delta.avg_disk_latency = after.avg_disk_latency;

  print_vmstats(&delta);
}

static void
test_multiprocess_swap(void)
{
  printf("\n=== TEST 2: multi-process swap pressure ===\n");
  printf("Forking %d children, each touching %d pages\n", NCHILDREN, ALLOC_PAGES);

  for (int c = 0; c < NCHILDREN; c++){
    int pid = fork();
    if (pid < 0){
      printf("fork failed\n");
      exit(1);
    }

    if (pid == 0){
      int nbytes = ALLOC_PAGES * PAGE_SIZE;
      char *mem = lazy_alloc(nbytes);
      if (mem == (char *)-1){
        exit(2);
      }

      for (int i = 0; i < ALLOC_PAGES; i++){
        mem[i * PAGE_SIZE] = (char)((i + c) & 0xFF);
        // printf("%d %d\n", c, i);
      }

      int errs = 0;
      for (int i = 0; i < ALLOC_PAGES; i++){
        if (mem[i * PAGE_SIZE] != (char)((i + c) & 0xFF)){
          errs++;
        }
      }

      struct vmstats s;
      getvmstats(getpid(), &s);
      printf("child %d: faults=%d evicted=%d swap_in=%d disk_r=%d disk_w=%d errs=%d\n", c, s.page_faults, s.pages_evicted, s.pages_swapped_in, s.disk_reads, s.disk_writes, errs);

      exit(errs == 0 ? 0 : 1);
    }
  }

  int all_ok = 1;
  for (int c = 0; c < NCHILDREN; c++){
    int status;
    wait(&status);
    if (status != 0){
      all_ok = 0;
    }
  }

  printf("Multi-process result: %s\n", all_ok ? "PASS" : "FAIL");
}

int
main(void)
{
  printf("======================================\n");
  printf("  Swap Correctness Test (RAID 5)\n");
  printf("======================================\n");

  if (setraidpolicy(5) < 0){
    printf("setraidpolicy failed\n");
    exit(1);
  }

  if (setdisksched(1) < 0){
    printf("setdisksched failed\n");
    exit(1);
  }

  test_swap_correctness();
  test_multiprocess_swap();

  printf("\nTest done\n");
  exit(0);
}