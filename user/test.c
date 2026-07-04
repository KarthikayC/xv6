// // #include "kernel/types.h"
// // #include "kernel/stat.h"
// // #include "user/user.h"

// // struct vmstats {
// //     int page_faults;
// //     int pages_evicted;
// //     int pages_swapped_in;
// //     int pages_swapped_out;
// //     int resident_pages;
// //     // --- ADDED FOR PA4 ---
// //     int disk_reads;
// //     int disk_writes;
// //     int avg_disk_latency;
// //     // ---------------------
// // };

// // // Helper: Prints current stats cleanly
// // void print_stats(const char *phase, int pid) {
// //     struct vmstats st;
// //     if (getvmstats(pid, &st) == 0) {
// //         printf("\n--- [%s - PID %d] ---\n", phase, pid);
// //         printf("  Page Faults  : %d\n", st.page_faults);
// //         printf("  Evicted      : %d\n", st.pages_evicted);
// //         printf("  Swapped Out  : %d\n", st.pages_swapped_out);
// //         printf("  Swapped In   : %d\n", st.pages_swapped_in);
// //         printf("  Resident     : %d\n", st.resident_pages);
// //         // --- ADDED FOR PA4 ---
// //         printf("  Disk Reads   : %d\n", st.disk_reads);
// //         printf("  Disk Writes  : %d\n", st.disk_writes);
// //         printf("  Avg Latency  : %d\n", st.avg_disk_latency);
// //         // ---------------------
// //         printf("---------------------------\n");
// //     }
// // }

// // // TEST 1: VM Stats Infrastructure (From test_vmstats.c)
// // void test_stats_infra() {
// //     printf("\n=== TEST 1: VM Stats Infrastructure ===\n");
// //     struct vmstats stats;
// //     int pid = getpid();

// //     printf("Testing getvmstats for valid PID %d...\n", pid);
// //     if (getvmstats(pid, &stats) < 0) {
// //         printf("[FAIL] getvmstats system call returned an error!\n");
// //         exit(1);
// //     }
// //     printf("[PASS] getvmstats executed successfully.\n");
// //     print_stats("Initial State", pid);

// //     // Relaxed sanity check: exec() naturally causes page faults before main() starts.
// //     if (stats.page_faults >= 0 && stats.resident_pages >= -10) {
// //         printf("[PASS] Stats are active. (Initial page faults from exec() are normal).\n");
// //     } else {
// //         printf("[WARN] Stats contain extreme garbage data. Check your initialization.\n");
// //     }

// //     printf("Testing getvmstats for invalid PID (99999)...\n");
// //     if (getvmstats(99999, &stats) < 0) {
// //         printf("[PASS] Invalid PID correctly rejected (-1 returned).\n");
// //     } else {
// //         printf("[FAIL] Invalid PID was accepted!\n");
// //     }
// // }

// // // TEST 2: Aggressive Exhaustion & Re-use (From test_4.c & test_swapspace.c)
// // void test_exhaustion() {
// //     printf("\n=== TEST 2: Aggressive Paging & Integrity Test ===\n");
// //     int total_pages = 4000; 
    
// //     printf("1. Allocating %d pages using sbrk()...\n", total_pages);
// //     char *mem = sbrk(total_pages * 4096);
// //     if(mem == (char*)-1) {
// //         printf("sbrk failed\n");
// //         exit(1);
// //     }

// //     printf("2. Writing to pages (Forcing page replacement & evictions)...\n");
// //     for(int i = 0; i < total_pages; i++) {
// //         mem[i * 4096] = (char)(i % 256); 
// //     }

// //     printf("3. Reading data back (Triggering faults & reusing evicted pages)...\n");
// //     int errors = 0;
// //     for(int i = 0; i < total_pages; i++) {
// //         if(mem[i * 4096] != (char)(i % 256)) {
// //             errors++;
// //         }
// //     }

// //     if(errors == 0) {
// //         printf("-> [SUCCESS] All reused swapped pages maintained data integrity!\n");
// //     } else {
// //         printf("-> [FAILURE] %d pages corrupted.\n", errors);
// //     }

// //     print_stats("Final State", getpid());
// // }

// // // TEST 3: Concurrent Paging Stress Test (From test_2.c)
// // void child_work(int id) {
// //     int pages = 10000;
// //     char *mem = sbrk(pages * 4096);
// //     if(mem == (char*)-1) {
// //         printf("Child %d sbrk failed!\n", id);
// //         exit(1);
// //     }

// //     // Write a unique character for this specific child
// //     for(int i = 0; i < pages; i++) {
// //         mem[i * 4096] = 'A' + id;
// //     }
    
// //     // Read it back to ensure another process didn't overwrite our swapped pages
// //     int errors = 0;
// //     for(int i = 0; i < pages; i++) {
// //         if(mem[i * 4096] != 'A' + id) errors++;
// //     }
    
// //     struct vmstats st;
// //     getvmstats(getpid(), &st);
// //     printf("Child %d -> Evictions: %d, Faults: %d, Corruptions: %d, Latency: %d\n", 
// //            id, st.pages_evicted, st.page_faults, errors, st.avg_disk_latency);
// //     exit(0);
// // }

// // void test_concurrent() {
// //     printf("\n=== TEST 3: Concurrent Paging Stress Test ===\n");
// //     printf("Spawning 3 children to allocate 10,000 pages EACH (30,000 total)...\n");
    
// //     for(int i = 1; i <= 3; i++) {
// //         if(fork() == 0) {
// //             child_work(i);
// //         }
// //     }
    
// //     // Parent waits for all 3 children to finish surviving the memory pressure
// //     for(int i = 0; i < 3; i++) {
// //         wait(0);
// //     }
    
// //     printf("-> [SUCCESS] Concurrent test complete. OS survived race conditions!\n");
// // }

// // // TEST 4: Time-Synced MLFQ Priority Test (From test_3.c)
// // void test_mlfq() {
// //     printf("\n=== TEST 4: Time-Synced MLFQ Priority Test ===\n");

// //     printf("[Parent] Allocating initial 2,000 pages...\n");
// //     int parent_initial = 2000;
// //     char *p_mem = sbrk(parent_initial * 4096);
// //     for(int i = 0; i < parent_initial; i++) p_mem[i * 4096] = 'P';

// //     int pid = fork();

// //     if(pid == 0) {
// //         // CHILD PROCESS
// //         printf("[Child] Allocating 10,000 pages...\n");
// //         int child_pages = 10000;
// //         char *c_mem = sbrk(child_pages * 4096);
// //         for(int i = 0; i < child_pages; i++) c_mem[i * 4096] = 'C';

// //         printf("[Child] Burning CPU for a few seconds to drop MLFQ priority...\n");
// //         for(int j = 0; j < 15; j++) {
// //             for(volatile int i = 0; i < 100000000; i++);
// //         }

// //         // Child goes to sleep, allowing the Parent to wake up and steal its pages!
// //         pause(100); 

// //         struct vmstats st;
// //         getvmstats(getpid(), &st);
// //         printf("--> [Child] (Low Priority) Pages Evicted: %d, Latency: %d\n", st.pages_evicted, st.avg_disk_latency);
// //         exit(0);
        
// //     } else {
// //         // PARENT PROCESS
// //         pause(30); // Let child burn CPU and allocate first

// //         printf("[Parent] Waking up at High Priority. Allocating 16,000 overflow pages...\n");
// //         int parent_overflow = 16000;
// //         char *p_mem_2 = sbrk(parent_overflow * 4096);
// //         for(int i = 0; i < parent_overflow; i++) p_mem_2[i * 4096] = 'O';

// //         struct vmstats st;
// //         getvmstats(getpid(), &st);
// //         printf("--> [Parent] (High Priority) Pages Evicted: %d, Latency: %d\n", st.pages_evicted, st.avg_disk_latency);

// //         // Parent waits for the sleeping child to wake up, print its stats, and exit
// //         wait(0); 
// //         printf("EXPECTED: Child (Low Priority) should have significantly more evictions.\n");
// //     }
// // }
// // // TEST 5: Fork Survival Test (From commented section of test_swap.c)
// // void test_fork() {
// //     printf("\n=== TEST 5: The Fork Survival Test ===\n");
// //     int pages = 5000; // Scaled up to ensure some pages hit the swap disk before fork

// //     printf("Parent: Allocating and dirtying %d pages...\n", pages);
// //     char *mem = sbrk(pages * 4096);
// //     for(int i = 0; i < pages; i++) {
// //         mem[i * 4096] = (char)(i % 256); 
// //     }

// //     printf("Parent: Spawning child process...\n");
// //     int pid = fork();

// //     if(pid < 0) {
// //         printf("FAIL: Fork completely failed.\n");
// //         exit(1);
// //     }

// //     if(pid == 0) {
// //         // CHILD PROCESS
// //         printf("Child: Waking up. Verifying inherited memory...\n");
// //         int child_failed = 0;
        
// //         for(int i = 0; i < pages; i++) {
// //             if(mem[i * 4096] != (char)(i % 256)) {
// //                 child_failed++;
// //             }
// //         }
        
// //         if(child_failed) printf("-> [FAIL] Child data corruption on %d pages.\n", child_failed);
// //         else printf("-> [SUCCESS] Child successfully inherited swapped data.\n");
        
// //         exit(0);
// //     } else {
// //         // PARENT PROCESS
// //         wait(0); 
        
// //         printf("Parent: Child finished. Verifying my own memory...\n");
// //         int parent_failed = 0;
// //         for(int i = 0; i < pages; i++) {
// //             if(mem[i * 4096] != (char)(i % 256)) {
// //                 parent_failed++;
// //             }
// //         }

// //         if(parent_failed) printf("-> [FAIL] Parent data corruption on %d pages.\n", parent_failed);
// //         else printf("-> [SUCCESS] Parent memory is strictly intact after the fork.\n");
// //     }
// // }

// // // MAIN DISPATCHER
// // int main(int argc, char *argv[]) {
// //     if(argc != 2) {
// //         printf("Usage: vmtest [1|2|3|4|5|all]\n");
// //         printf("  1: VM Stats Infrastructure\n");
// //         printf("  2: Aggressive Paging & Integrity\n");
// //         printf("  3: Concurrent Paging Stress Test\n");
// //         printf("  4: Time-Synced MLFQ Priority Test\n");
// //         printf("  5: Fork Swap Survival Test\n");
// //         exit(0);
// //     }

// //     int test_num = argv[1][0] - '0';

// //     if(test_num == 1) test_stats_infra();
// //     else if(test_num == 2) test_exhaustion();
// //     else if(test_num == 3) test_concurrent();
// //     else if(test_num == 4) test_mlfq();
// //     else if(test_num == 5) test_fork();
// //     else if(argv[1][0] == 'a') {
// //         // Run all tests sequentially
// //         test_stats_infra();
// //         if(fork() == 0) { test_exhaustion(); exit(0); } else { wait(0); }
// //         if(fork() == 0) { test_concurrent(); exit(0); } else { wait(0); }
// //         if(fork() == 0) { test_mlfq();       exit(0); } else { wait(0); }
// //         if(fork() == 0) { test_fork();       exit(0); } else { wait(0); }
// //         printf("\n*** ALL TESTS PASSED! ***\n");
// //     } else {
// //         printf("Invalid test number.\n");
// //     }

// //     exit(0);
// // }



// #include "kernel/types.h"
// #include "kernel/stat.h"
// #include "user/user.h"

// void print_stats(const char *phase, int pid) {
//     struct vmstats st;
//     if (getvmstats(pid, &st) == 0) {
//         printf("\n--- [%s - PID %d] ---\n", phase, pid);
//         printf("  Page Faults  : %d\n", st.page_faults);
//         printf("  Evicted      : %d\n", st.pages_evicted);
//         printf("  Swapped Out  : %d\n", st.pages_swapped_out);
//         printf("  Swapped In   : %d\n", st.pages_swapped_in);
//         printf("  Resident     : %d\n", st.resident_pages);
//         printf("  Disk Reads   : %d\n", st.disk_reads);
//         printf("  Disk Writes  : %d\n", st.disk_writes);
//         printf("  Avg Latency  : %d\n", st.avg_disk_latency);
//         printf("---------------------------\n");
//     }
// }

// // TEST 1: Infrastructure (Unchanged)
// void test_stats_infra() {
//     printf("\n=== TEST 1: VM Stats Infrastructure ===\n");
//     struct vmstats stats;
//     int pid = getpid();
//     if (getvmstats(pid, &stats) < 0) {
//         printf("[FAIL] getvmstats error!\n");
//         exit(1);
//     }
//     printf("[PASS] getvmstats executed successfully.\n");
// }

// // TEST 2: Aggressive Paging (Adjusted to 4500 pages)
// // 4500 pages = 17.5 MB. Forces ~400 pages to the swap disk.
// void test_exhaustion() {
//     printf("\n=== TEST 2: Aggressive Paging & Integrity Test ===\n");
//     int total_pages = 1000; 
    
//     printf("1. Allocating %d pages (17.5 MB)...\n", total_pages);
//     char *mem = sbrk(total_pages * 4096);
//     if(mem == (char*)-1) { exit(1); }

//     for(int i = 0; i < total_pages; i++) {
//         printf("%d\n", i);
//         mem[i * 4096] = (char)(i % 256); 
//     }

//     int errors = 0;
//     for(int i = 0; i < total_pages; i++) {
//         if(mem[i * 4096] != (char)(i % 256)) { errors++; }
//     }

//     if(errors == 0) printf("-> [SUCCESS] Data integrity maintained!\n");
//     else printf("-> [FAILURE] %d pages corrupted.\n", errors);

//     print_stats("Final State", getpid());
// }

// // TEST 3: Concurrent Paging (Adjusted to 1500 pages per child)
// // 3 * 1500 = 4500 total pages. Guarantees contention without OOM.
// void child_work(int id) {
//     int pages = 400;
//     char *mem = sbrk(pages * 4096);
//     for(int i = 0; i < pages; i++) { mem[i * 4096] = 'A' + id; }
    
//     int errors = 0;
//     for(int i = 0; i < pages; i++) {
//         if(mem[i * 4096] != 'A' + id) errors++;
//     }
    
//     pause(5);

//     struct vmstats st;
//     getvmstats(getpid(), &st);
//     printf("Child %d -> Evictions: %d, Faults: %d, Corruptions: %d\n", 
//            id, st.pages_evicted, st.page_faults, errors);
//     exit(0);
// }

// void test_concurrent() {
//     printf("\n=== TEST 3: Concurrent Paging Stress Test ===\n");
//     for(int i = 1; i <= 3; i++) {
//         if(fork() == 0) child_work(i);
//     }
//     for(int i = 0; i < 3; i++) wait(0);
//     printf("-> [SUCCESS] Concurrent test complete.\n");
// }

// // TEST 4: Priority Test (Adjusted scale)
// void test_mlfq() {
//     printf("\n=== TEST 4: Time-Synced MLFQ Priority Test ===\n");
//     int parent_initial = 1500;
//     char *p_mem = sbrk(parent_initial * 4096);
//     for(int i = 0; i < parent_initial; i++) p_mem[i * 4096] = 'P';

//     if(fork() == 0) {
//         int child_pages = 2500;
//         char *c_mem = sbrk(child_pages * 4096);
//         for(int i = 0; i < child_pages; i++) c_mem[i * 4096] = 'C';

//         for(int j = 0; j < 5; j++) {
//             for(volatile int i = 0; i < 10000000; i++);
//         }
//         pause(50); 
//         struct vmstats st;
//         getvmstats(getpid(), &st);
//         printf("--> [Child] (Low Prio) Evicted: %d, Latency: %d\n", st.pages_evicted, st.avg_disk_latency);
//         exit(0);
//     } else {
//         pause(20); 
//         int parent_overflow = 1000;
//         char *p_mem_2 = sbrk(parent_overflow * 4096);
//         for(int i = 0; i < parent_overflow; i++) p_mem_2[i * 4096] = 'O';

//         struct vmstats st;
//         getvmstats(getpid(), &st);
//         printf("--> [Parent] (High Prio) Evicted: %d, Latency: %d\n", st.pages_evicted, st.avg_disk_latency);
//         wait(0); 
//     }
// }

// // TEST 5: Fork Survival (Adjusted to 3000 pages)
// void test_fork() {
//     printf("\n=== TEST 5: The Fork Survival Test ===\n");
//     int pages = 3000; 
//     char *mem = sbrk(pages * 4096);
//     for(int i = 0; i < pages; i++) { mem[i * 4096] = (char)(i % 256); }

//     int pid = fork();
//     if(pid == 0) {
//         int child_failed = 0;
//         for(int i = 0; i < pages; i++) {
//             if(mem[i * 4096] != (char)(i % 256)) child_failed++;
//         }
//         if(child_failed) printf("-> [FAIL] Child corruption: %d\n", child_failed);
//         else printf("-> [SUCCESS] Child inherited swapped data.\n");
//         exit(0);
//     } else {
//         wait(0); 
//         printf("-> [SUCCESS] Parent memory intact.\n");
//     }
// }

// int main(int argc, char *argv[]) {
//     if(argc != 2) { exit(0); }
//     int test_num = argv[1][0] - '0';
//     if(test_num == 1) test_stats_infra();
//     else if(test_num == 2) test_exhaustion();
//     else if(test_num == 3) test_concurrent();
//     else if(test_num == 4) test_mlfq();
//     else if(test_num == 5) test_fork();
//     exit(0);
// }

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define SWAP_PAGES 1300 

int main(void) {
    printf("\n==========================================\n");
    printf("  RAID 0: STRIPING VERIFICATION\n");
    printf("==========================================\n");

    setraidpolicy(0);
    setdiskfail(-1); // Ensure all disks are spinning

    char *mem = (char *)sbrklazy(SWAP_PAGES * PGSIZE);
    
    printf("-> Writing data across 100 pages (forcing swap)...\n");
    for (int i = 0; i < SWAP_PAGES; i++) {
        mem[i * PGSIZE] = (i % 100);
    }

    printf("-> Verifying healthy array...\n");
    int errs = 0;
    for (int i = 0; i < SWAP_PAGES; i++) {
        if (mem[i * PGSIZE] != (char)(i % 100)) errs++;
    }
    if (errs == 0) printf("  [PASS] Data striped and read perfectly.\n");
    else printf("  [FAIL] %d mismatches on healthy array.\n", errs);

    printf("\n-> ASSASSINATING DISK 2...\n");
    setdiskfail(2); // Kill a disk

    printf("-> Reading from dead RAID 0 array (Expecting massive data loss)...\n");
    errs = 0;
    for (int i = 0; i < SWAP_PAGES; i++) {
        // Force a read to trigger the page fault and disk read
        if (mem[i * PGSIZE] != (char)(i % 100)){
            errs++;
        }
    }

    // Since 1 out of 4 disks is dead, roughly 25% of blocks should return 0s
    if (errs > 0) printf("  [PASS] RAID 0 correctly lost data! Detected %d corrupted pages.\n", errs);
    else printf("  [FAIL] Data survived? Your RAID 0 is secretly a RAID 1.\n");

    setdiskfail(-1); // Reset
    exit(0);
}