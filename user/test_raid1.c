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

// TEST: RAID 1
static void
test_raid1_normal(void)
{
  printf("\n=== RAID 1: Mirroring (no failure) ===\n");

  if (setraidpolicy(1) < 0){
    printf("setraidpolicy failed\n");
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

    int mismatches = write_verify(mem, ALLOC_PAGES, 22);
    print_vmstats_short();
    if (mismatches == 0){
      printf("PASS: RAID 1 (no failure) - all pages correct\n");
    }
    else{
      printf("FAIL: RAID 1 - %d mismatches\n", mismatches);
    }

    exit(mismatches == 0 ? 0 : 1);
  }

  int status;
  wait(&status);
}

static void
test_raid1_with_failure(int failed_disk)
{
  printf("\n=== RAID 1: Mirroring (disk %d failed) ===\n", failed_disk);

  if (setraidpolicy(1) < 0){
    printf("setraidpolicy failed\n");
    exit(1);
  }

  if (setdiskfail(failed_disk) < 0){
    printf("setdiskfail failed\n");
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

    for (int i = 0; i < ALLOC_PAGES; i++){
      char *page = mem + (i * PAGE_SIZE);
      for (int b = 0; b < PAGE_SIZE; b++){
        page[b] = (char)((i * 13 + b + 33) & 0xFF);
      }
    }

    int mismatches = 0;
    for (int i = 0; i < ALLOC_PAGES; i++){
      char *page = mem + (i * PAGE_SIZE);
      for (int b = 0; b < PAGE_SIZE; b++){
        char expected = (char)((i * 13 + b + 33) & 0xFF);
        if (page[b] != expected){
          mismatches++;
        }
      }
    }

    print_vmstats_short();
    if (mismatches == 0){
      printf("PASS: RAID 1 failover - all pages reconstructed from mirror\n");
    }
    else{
      printf("FAIL: RAID 1 failover - %d mismatches\n", mismatches);
    }

    exit(mismatches == 0 ? 0 : 1);
  }

  int status;
  wait(&status);

  setdiskfail(-1);
}

int
main(void)
{
  printf("==========================================\n");
  printf("  RAID 1 Verification Test\n");
  printf("==========================================\n");

  // RAID 1
  test_raid1_normal();
  test_raid1_with_failure(0);   // disk 0 is the primary of pair {0,1}
  test_raid1_with_failure(2);   // disk 2 is the primary of pair {2,3}

  printf("\nTest done\n");
  exit(0);
}
