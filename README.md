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