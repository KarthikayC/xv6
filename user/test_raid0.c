#include "kernel/types.h"
#include "user/user.h"

#define ALLOC_PAGES 1024
#define PAGE_SIZE 4096

static char *
lazy_alloc(int nbytes)
{
  return (char *)sbrklazy(nbytes);
}

static void
print_vmstats_short(void)
{
  struct vmstats s;
  getvmstats(getpid(), &s);
  printf("[stats] disk_r=%d disk_w=%d faults=%d evicted=%d swapin=%d\n", s.disk_reads, s.disk_writes, s.page_faults, s.pages_evicted, s.pages_swapped_in);
}

static int
write_verify(char *mem, int npages, int random)
{
  for (int i = 0; i < npages; i++) {
    char *page = mem + (i * PAGE_SIZE);
    for (int b = 0; b < PAGE_SIZE; b++){
      page[b] = (char)((i * 7 + b + random) & 0xFF);
    }
  }

  int mismatches = 0;
  for (int i = 0; i < npages; i++){
    char *page = mem + (i * PAGE_SIZE);
    for (int b = 0; b < PAGE_SIZE; b++){
      char expected = (char)((i * 7 + b + random) & 0xFF);
      if (page[b] != expected){
        mismatches++;
        printf("MISMATCH page=%d byte=%d got=%d exp=%d\n", i, b, (int)(unsigned char)page[b], (int)(unsigned char)expected);
      }
    }
  }

  return mismatches;
}

// TEST: RAID 0
static void
test_raid0(void)
{
  printf("\n=== RAID 0: Striping ===\n");

  if (setraidpolicy(0) < 0){
    printf("setraidpolicy failed\n");
    exit(1);
  }

  if (setdisksched(0) < 0){
    printf("setdisksched failed\n");
    exit(1);
  }

  int pid = fork();
  if (pid < 0){
    printf("fork failed\n");
    exit(1);
  }

  if (pid == 0){
    char *mem = lazy_alloc(ALLOC_PAGES * PAGE_SIZE);
    if (mem == (char *)-1){
      printf("sbrk failed\n");
      exit(1);
    }

    int mismatches = write_verify(mem, ALLOC_PAGES, 11);
    print_vmstats_short();
    if (mismatches == 0){
      printf("PASS: RAID 0 - all %d pages verified\n", ALLOC_PAGES);
    }
    else{
      printf("FAIL: RAID 0 - %d mismatches\n", mismatches);
    }

    exit(mismatches == 0 ? 0 : 1);
  }

  int status;
  wait(&status);
}

int
main(void)
{
  printf("==========================================\n");
  printf("  RAID 0 Verification Test\n");
  printf("==========================================\n");

  test_raid0();

  printf("\nTest done\n");
  exit(0);
}