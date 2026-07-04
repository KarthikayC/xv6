#include "kernel/types.h"
#include "user/user.h"

#define ALLOC_PAGES 1024
#define PAGE_SIZE 4096

static char *
lazy_alloc(int nbytes)
{
  return (char *)sbrk(nbytes);
}

static void
print_vmstats(void)
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

static void
test_raid5_normal(void)
{
  printf("\n=== RAID 5: Parity striping (no failure) ===\n");

  if (setraidpolicy(5) < 0){
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

    int mismatches = write_verify(mem, ALLOC_PAGES, 44);
    print_vmstats();
    if (mismatches == 0){
      printf("PASS: RAID 5 (no failure) - all pages correct\n");
    }
    else{
      printf("FAIL: RAID 5 - %d mismatches\n", mismatches);
    }

    exit(mismatches == 0 ? 0 : 1);
  }

  int status;
  wait(&status);
}

static void
test_raid5_reconstruction(int failed_disk)
{
  printf("\n=== RAID 5: Reconstruction with disk %d failed ===\n", failed_disk);

  if (setraidpolicy(5) < 0){
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
        page[b] = (char)((i * 17 + b + 55) & 0xFF);
      }
    }

    int mismatches = 0;
    for (int i = 0; i < ALLOC_PAGES; i++){
      char *page = mem + (i * PAGE_SIZE);
      for (int b = 0; b < PAGE_SIZE; b++){
        char expected = (char)((i * 17 + b + 55) & 0xFF);
        if (page[b] != expected){
          mismatches++;
          if (mismatches <= 3){
            printf("MISMATCH page=%d byte=%d got=%d exp=%d\n", i, b, (int)(unsigned char)page[b], (int)(unsigned char)expected);
          }
        }
      }
    }

    print_vmstats();
    if (mismatches == 0){
      printf("PASS: RAID 5 reconstruction (disk %d failed) - all data restored\n", failed_disk);
    }
    else{
      printf("FAIL: RAID 5 reconstruction - %d mismatches\n", mismatches);
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
  printf("  RAID 5 Verification Test\n");
  printf("==========================================\n");

  // RAID 5
  test_raid5_normal();
  test_raid5_reconstruction(0);  // failed disk = 0
  test_raid5_reconstruction(1);  // failed disk = 1
  test_raid5_reconstruction(2);  // failed disk = 2
  test_raid5_reconstruction(3);  // failed disk = 3

  printf("\nTest done\n");
  exit(0);
}
