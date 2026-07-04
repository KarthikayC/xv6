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
scattered_access(char *mem, int npages, int seed)
{
  unsigned int state = (unsigned int)seed;
  for (int iter = 0; iter < npages; iter++){
    state = state * 1664525u + 1013904223u;
    int page = (int)(state % (unsigned int)npages);
    mem[page * PAGE_SIZE] = (char)(page & 0xFF);
  }

  state = (unsigned int)seed;
  for (int iter = 0; iter < npages; iter++){
    state = state * 1664525u + 1013904223u;
    int page = (int)(state % (unsigned int)npages);
    volatile char dummy = mem[page * PAGE_SIZE];
    (void)dummy;
  }
}

typedef struct {
  int disk_reads;
  int disk_writes;
  int avg_disk_latency;
  int total_disk_latency;
  int total_disk_requests;
  int page_faults;
  int pages_evicted;
  int pages_swapped_in;
} Result;

static Result
run_workload(int sched_policy, const char *policy_name)
{
  printf("\n--- Running workload with %s (policy=%d) ---\n", policy_name, sched_policy);

  if (setdisksched(sched_policy) < 0){
    printf("setdisksched(%d) failed\n", sched_policy);
    exit(1);
  }

  int pid = fork();
  if (pid < 0){
    printf("fork failed\n");
    exit(1);
  }

  if (pid == 0){
    struct vmstats s;
    int me = getpid();
    getvmstats(me, &s);

    int before_dreads = s.disk_reads;
    int before_dwrites = s.disk_writes;
    int before_faults = s.page_faults;
    int before_evictions = s.pages_evicted;
    int before_swapin = s.pages_swapped_in;

    char *mem = lazy_alloc(ALLOC_PAGES * PAGE_SIZE);
    if (mem == (char *)-1){
      exit(2);
    }

    scattered_access(mem, ALLOC_PAGES, 42);

    getvmstats(me, &s);

    printf("page_faults      : %d\n", s.page_faults - before_faults);
    printf("pages_evicted    : %d\n", s.pages_evicted - before_evictions);
    printf("pages_swapped_in : %d\n", s.pages_swapped_in - before_swapin);
    printf("disk_reads       : %d\n", s.disk_reads - before_dreads);
    printf("disk_writes      : %d\n", s.disk_writes - before_dwrites);
    printf("avg_disk_latency : %d\n", s.avg_disk_latency);
    printf("total_disk_lat   : %d\n", s.total_disk_latency);
    printf("total_disk_req   : %d\n", s.total_disk_requests);

    exit(0);
  }

  int status;
  wait(&status);

  Result r = {0};
  return r;
}

int
main(void)
{
  printf("==========================================\n");
  printf("  PA4 Disk Scheduling Comparison Test\n");
  printf("==========================================\n");

  if (setraidpolicy(0) < 0){
    printf("setraidpolicy failed\n");
    exit(1);
  }

  run_workload(0, "FCFS");
  run_workload(1, "SSTF");

  printf("\nTest done\n");
  exit(0);
}