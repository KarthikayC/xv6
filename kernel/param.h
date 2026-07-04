#define NLEVEL        4  // number of levels in mlfq
#define GBOOST      128  // ticks for global priority boost
#define NFRAMES    1024  // maximum number of frames
#define NPROC        64  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGBLOCKS    (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define FSSIZE    10192  // size of file system + disks in blocks
#define FSLIMIT    2000  // size of file system in blocks
#define MAXPATH     128   // maximum file path name
#define USERSTACK     1     // user stack pages