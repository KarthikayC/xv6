# OS Assignment - 1

Author: Karthikay Chandana (CS24BTECH11033)  
Date: 8th February


Overview
--------
This submission extends the xv6 operating system by adding multiple new system calls related to kernel interaction, process relationships, and system call accounting. All system calls were implemented following xv6 locking discipline and tested using custom test programs.


Implemented System Calls
------------------------


Part A: Warm-up System Calls
----------------------------

1. hello()
   - Prints the message: "Hello from the kernel!" directly to the xv6 console. This system call always returns 0.

2. getpid2()
   - Returns the PID of the calling process.
   - This system call is implemented independently of the existing getpid() and directly accesses the current process structure.


Part B: Process Relationships
-----------------------------

3. getppid()
   - Returns the PID of the parent process of the calling process. If the calling process has no parent, the system call returns -1.

4. getnumchild()
   - Returns the number of currently alive child processes of the calling process. Zombie processes are explicitly excluded from the count.
   - The process table is traversed while holding appropriate locks to ensure correctness under concurrent execution.


Part C: System Call Accounting
------------------------------

5. Per-process system call counter
   - Each process maintains a counter (syscall_count) that tracks the total number of system calls it has invoked since creation.
   - The counter is initialized to 0 in allocproc().
   - The counter is incremented once per system call in the syscall() function.

6. getsyscount()
   - Returns the system call count of the calling process.

7. getchildsyscount(int pid)
   - Returns the system call count of a child process with the specified PID.
   - If the PID corresponds to a live child of the caller, its syscall count is returned.
   - If the PID is invalid, not a child, or already reaped, the system call returns -1.


Design Decisions and Assumptions
--------------------------------
- The syscall counter is stored in struct proc to maintain per-process isolation.
- Process table traversal is protected using wait_lock and individual process locks to avoid race conditions.
- Zombie processes are not counted in getnumchild() but are counted in getchildsyscount().
- All test programs were written to validate normal cases, edge cases, and concurrent execution scenarios.
- The test programs have a small file name due to xv6 limitations.


Disclaimer
-------
Gemini was used to help understand the xv6 source code

# OS Assignment - 2

Author: Karthikay Chandana (CS24BTECH11033)  
Date: 9th March


Overview
--------

This assignment extends the xv6 operating system by replacing the default round robin scheduler with a 4 Level System-Call-Aware Multi-Level Feedback Queue (SC-MLFQ). It builds upon process accounting features from PA1 to dynamically classify processes as interactive (I/O-bound) or CPU-bound based on their system call frequency.


Implemented System Calls
------------------------

1. int getlevel(void)   
   - Returns the current MLFQ priority level of the calling process.

2. int getmlfqinfo(int pid, struct mlfqinfo *info)
   - Retrieves detailed scheduling statistics of the process with the given PID.
   - Returns 0 on success and -1 if the PID is invalid or the process is not in a valid state.
   - Data tracked:
      - level: Current queue level.
      - ticks[4]: Total ticks consumed at each of the 4 priority levels.
      - times_scheduled: Total number of times the process was selected by the scheduler.
      - total_syscalls: Total system calls made.


Design Decisions and Assumptions
-----------------------

1. Queue Mechanics and Structure
   - Linked Lists: The 4 priority queues are implemented as doubly-linked lists directly within the struct proc (using next and prev pointers). This ensures O(1) enqueue and dequeue operations.
   - Quanta: Fixed time quanta are strictly enforced per level. Level 0 (2 ticks), Level 1 (4 ticks), Level 2 (8 ticks), and Level 3 (16 ticks).
   - Lock Ordering: To prevent deadlocks when moving processes between queues (e.g., during wakeup or yield), a strict lock acquisition order is enforced system-wide. The global mlfq_lock must always be acquired before any individual p->lock.

2. System-Call-Aware Rule
   - We update a process variable named previous_syscall_count after every time slice is completed for a process. Using this, as well as syscall_count, we infer whether the process is interactive or not.

3. Starvation Prevention (Global Boost)
   - To prevent CPU-bound processes in Level 3 from starving when higher queues are massive, a global priority boost is triggered every 128 timer ticks.
   - The timer interrupt handler flags needs_global_boost = 1, which makes the scheduler to sweep all queues, reset tick counters, and forcefully elevate every process in all levels to Level 0.
   - Only processes already in the MLFQ, i.e., RUNNABLE processes, are elevated to Level 0.

4. Others
   - The scheduler is a pre-emptive scheduler. After every hardware interrupt, it goes through the queue to find which process to run.
   - That hardware interrupt timer is exclusively run on CPU 0 to ensure no overcounting before global boosting.
   - Helper functions to enqueue and dequeue from the MLFQ has been implemented.
   - When a process is changed from RUNNABLE to RUNNING, it is dequeued from the queue and again enqueued after hardware interrupt / wakeup, etc.
   - Scheduled is always one more than total number of ticks across all levels as we do not count the last tick where the process exits.
   - Parameters like NLEVEL and GBOOST have been added to params.h to make it the prototype more robust.


Experimental Results
-----------------------

The SC-MLFQ was evaluated using custom userspace test programs designed to generate specific workloads.


CPU-Bound Processes
-----------------------

Observations
- Processes start execution in Level 0.
- After consuming their time quantum, they are demoted to lower levels.
- Eventually, most CPU-bound processes settle in Level 2 or Level 3.

Result
- This confirms that the scheduler correctly identifies CPU-bound processes and gradually moves them to lower priority queues.


Syscall-Heavy Processes
-----------------------

Observations
- These processes frequently perform system calls during their time slice.
- Since ΔS ≥ ΔT, the scheduler classifies them as interactive processes.
- They remain mostly in Level 0.

Result
- The system-call-aware scheduling rule successfully prevents demotion of interactive processes, allowing them to maintain higher scheduling priority.


Mixed Workloads
-----------------------

Observations
- Interactive processes consistently remained in higher queues (Level 0–1).
- CPU-bound processes gradually moved to lower queues (Level 2–3).
- Interactive processes received CPU access more quickly than CPU-bound processes.

Result
- The scheduler effectively distinguishes between interactive and CPU-intensive workloads, improving responsiveness for interactive tasks.


Global Priority Boost
-----------------------

Observations
- Over time, CPU-bound processes moved to Level 3.
- After 128 timer ticks, a global priority boost was triggered.
- All runnable processes were moved back to Level 0.

Result
- The global priority boost mechanism successfully prevents starvation by periodically restoring all processes to the highest priority queue.


Disclaimer
-------

Gemini was used to help understand the exact working of an MLFQ scheduler and xv6 source code

# OS Assignment - 3

Author: Karthikay Chandana (CS24BTECH11033)  
Date: 3rd April


## Overview

This assignment enhances the xv6 operating system by transitioning from eager memory allocation to a fully functional on-demand paging system. The implementation introduces a physical memory limit, a global frame table, an in-memory swap mechanism, and a scheduler-aware clock page replacement algorithm. Additionally, it provides detailed per-process memory statistics via a custom system call.

---

## Implemented Features

### 1. Global Frame Table

A centralized frame table maintains metadata for all physical frames allocated to user processes. Each entry stores:

* Allocation status (in-use/free)
* Owning process (`struct proc*`)
* Mapped virtual address
* Reference bit (used by the replacement algorithm)

---

### 2. On-Demand Paging

The kernel no longer allocates physical memory immediately during an `sbrk()` call:

* Virtual address space grows immediately
* Physical memory is allocated only on access
* Page faults trigger dynamic allocation through the kernel’s fault handler

---

### 3. Clock Page Replacement Algorithm

When physical memory is exhausted:

* A circular clock hand scans the frame table
* Pages with cleared reference bits (`PTE_A == 0`) are candidates for eviction
* Frequently accessed pages (Higher priority process pages) are protected

---

### 4. Scheduler-Aware Eviction

The replacement policy integrates with the SC-MLFQ scheduler:

* Pages belonging to lower-priority processes are preferred for eviction
* High-priority (interactive) processes retain their working sets
* Ensures better responsiveness under memory pressure
* Worst case, it requires 2 entire scans of the frame table

---

### 5. In-Memory Swap Space

Instead of disk-based swapping:

* A fixed-size kernel array is used:

  ```c
  swap_space[MAX_SWAP_PAGES][PGSIZE]
  ```
* Evicted pages are copied into this array
* Page Table Entries (PTEs) store swap slot indices using a custom flag

---

### 6. Kernel Memory Statistics

A new system call `getvmstats` that shows runtime memory metrics for a process:

* `page_faults`
* `pages_evicted`
* `pages_swapped_in`
* `pages_swapped_out`
* `resident_pages`

---

## Design Decisions

### PTE Overloading

* `PTE_V` is cleared for swapped pages to trigger faults
* Custom `PTE_S` bit indicates swapped-out pages
* Physical address field is reused to store swap slot index

---

### Clock Hand Strategy

* Iterates continuously over the frame table
* Clears accessed bits (`PTE_A`)
* Selects victims with `PTE_A == 0`
* Among candidates, prefers pages from lower-priority processes

---

### Swap Implementation

* Fully memory-based (no disk I/O)
* Globally synchronized kernel array

---

### Fork Handling

* Both resident and swapped pages are duplicated
* Swapped pages are copied into new swap slots
* Ensures complete isolation between parent and child

---

## Experimental Evaluation

### Experiment 1: Lazy Allocation

**Objective:**
Validate that physical memory is allocated only on access.

**Method:**

* Allocates 1000 pages using `sbrklazy()`
* Record stats before and after allocation
* Sequentially write to each page

**Results:**

* After `sbrklazy()`:
  * `resident_pages` remains unchanged
  * `page_faults = 0`
* During writes:
  * Each access triggers a page fault
  * `page_faults = 1000`
  * `resident_pages` increases accordingly

**Conclusion:**
Confirms correct implementation of on-demand paging.

---

### Experiment 2: Page Eviction

**Objective:**
Test eviction, swapping, and data integrity.

**Method:**

1. Allocate and fill 5000 pages with known data
2. Allocate additional memory (27000 pages) to exhaust physical frames
3. Re-access original pages and verify contents

**Results:**

* `pages_evicted` and `pages_swapped_out` increase under pressure
* Swapped pages are restored on access (`pages_swapped_in` increases)
* 0 data corruption detected

**Conclusion:**
Eviction, swapping, and restoration mechanisms are fully correct.

---

### Experiment 3: Scheduler-Aware Eviction

**Objective:**
Verify priority-based eviction behavior.

**Method:**

* Parent process assigned high priority (MLFQ Level 1)
* Child process demoted to low priority (MLFQ Level 3)
* Both allocate memory aggressively (16000 pages each)

**Results:**

* Low-priority child:
  * High `pages_swapped_out`
* High-priority parent:
  * Zero swapping

**Conclusion:**
Eviction policy correctly prioritizes high-priority processes, preserving their working sets.

---

## Disclaimer

Gemini was used to help understand the exact working of paging and xv6 source code

# OS Assignment - 4

Author: Karthikay Chandana (CS24BTECH11033)  
Date: 23rd April


## Implemented Features

### 1. Disk-backed Swap
Replaced the in-memory swap array from PA3 with disk I/O.
- `write_to_swap(pa)` — allocates a swap slot, calls `raid_write()` for each block of the page.
- `read_from_swap(slot, pa)` — reads blocks back via `raid_read()`, frees the slot.
- `fork_swap(slot, pa)` — reads blocks via `raid_read()` without freeing the slot (used during `fork` to duplicate swapped pages).

### 2. Disk Scheduling
- A linked list `req_queue` holds pending `disk_req` structures.
- `virtio_disk_rw()` appends to the queue. `disk_schedule()` dequeues one request and sends it to the virtio device.
- FCFS (policy 0): dequeues the head of the list (arrival order).
- SSTF (policy 1): among all pending requests at the same priority level, picks the one whose `blockno` is closest to `current_head_position`.
- Each request carries the issuing process's MLFQ level; lower level number = higher priority and is served first regardless of distance.
- Latency is simulated as `|head - blockno| + C` and tracked per-process in `mystats`.

### 3. RAID
Four virtual disks are carved out of the single physical disk image:

```
VIRTUAL_DISK_START = FSSIZE/5
VIRTUAL_DISK_SIZE  = FSSIZE/5
disk0 starts at FSSIZE/5 + 0*FSSIZE/5
disk1 starts at FSSIZE/5 + 1*FSSIZE/5
disk2 starts at FSSIZE/5 + 2*FSSIZE/5
disk3 starts at FSSIZE/5 + 3*FSSIZE/5
```

RAID 0 — Striping
- `disk  = logical_block % 4`
- `offset = logical_block / 4`

RAID 1 — Mirroring
- Two mirror pairs: `{disk0, disk1}` and `{disk2, disk3}`.
- `pair = logical_block % 2`
- primary = `pair * 2` and mirror = `primary + 1`
- Writes go to both disks. Reads come from the primary unless it has failed, in which case the mirror is used.

RAID 5 — Distributed Parity (XOR)
- `stripe = logical_block / 3`
- `parity_disk = stripe % 4`
- `data_disk = logical_block % 3` (incremented if it would collide with `parity_disk`)
- Write: `new_parity = old_parity XOR old_data XOR new_data` (read-modify-write on both data and parity blocks).
- Normal read reads directly from `data_disk`. Failed disk read XOR all three surviving disks in the stripe to reconstruct.

### 4. Statistics
Per-process `vmstats` now includes:
- `disk_reads`, `disk_writes` — incremented in `disk_schedule()`.
- `total_disk_latency`, `total_disk_requests`, `avg_disk_latency` — updated after each scheduled request.
- `page_faults`, `pages_evicted`, `pages_swapped_in`, `pages_swapped_out`, `resident_page` — updated in `vmfault()` and `uvmunmap()`.

---

## Design Decisions and Assumptions

- Swap unit = one page = `BLOCKS_PER_PAGE` disk blocks.
  `BLOCKS_PER_PAGE = PGSIZE / BSIZE = 4096 / 1024 = 4 blocks per page`.
  This is the granularity at which `raid_write` / `raid_read` are called.

- `disk_schedule()` is called both by `virtio_disk_rw()` and by `virtio_disk_intr()`.
  The interrupt handler calls `disk_schedule()` after completing a request, forming a pipelined submission loop.
  A spinlock (`vdisk_lock`) guards all descriptor and queue state.

- RAID 5 write failure handling.
  If the target data disk has failed, the write is skipped (data lost on that disk). The parity disk is still updated. On read, reconstruction via XOR restores the data. This matches real RAID 5 degraded-mode behaviour.

- No disk-fail in RAID 0.
  RAID 0 has no redundancy; a failed disk causes data loss. The test suite does not exercise RAID 0 with `setdiskfail`.

- Latency constant `C = 10`.
  Chosen empirically to produce observable differences between FCFS and SSTF without making tests unreasonably slow.

---

## Experimental Results

### Experiment 1: Swap Correctness

Setup: Single process, RAID 5, FCFS scheduling.  
Allocate 4096 pages (16 MB) lazily, write a unique byte to each page, read every page back.

Results:

All 4096 pages survived the swap round-trip with zero data corruption, confirming that RAID 5's parity write and XOR reconstruction are working correctly.

---

### Experiment 2: FCFS vs SSTF Latency

Setup: Single child process, RAID 0, 2048 pages allocated and accessed in a  
scattered (pseudo-random LCG) order to spread requests across non-contiguous block numbers.  
The same workload is repeated under FCFS and then under SSTF.

Results:

SSTF achieved lower average latency compared to FCFS on this workload. The improvement arises because SSTF selects the nearest pending request rather than the oldest, dramatically reducing total head movement when many requests are queued simultaneously.

---

### Experiment 3: Multi-process Swap Pressure

Setup: 6 child processes each lazily allocate 120 pagwa and dirty all of it simultaneously,
creating heavy competition for physical frames.

Observation: Each child reported non-zero `pages_evicted` and `pages_swapped_in`, confirming
that the page replacement mechanism correctly integrates with disk-backed swap.
All children exited with zero mismatches, confirming correct isolation of swap slots across
processes.

---

## Disclaimer

Gemini was used to help understand the exact working of paging and xv6 source code
