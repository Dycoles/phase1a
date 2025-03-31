#ifndef _USYSCALL_H
#define _USYSCALL_H

#include "usloss.h"

/* System call codes */
#define USYSCALL_PROC_CREATE    1
#define USYSCALL_PROC_JOIN      2
#define USYSCALL_PROC_TERMINATE 3
#define USYSCALL_PROC_GETPID    4
#define USYSCALL_PROC_GETTIME   5
#define USYSCALL_SEM_CREATE     6
#define USYSCALL_SEM_P          7
#define USYSCALL_SEM_V          8
#define USYSCALL_SEM_FREE       9
#define USYSCALL_DISK_READ      10
#define USYSCALL_DISK_WRITE     11
#define USYSCALL_DISK_SIZE      12
#define USYSCALL_TERM_READ      13
#define USYSCALL_TERM_WRITE     14
#define USYSCALL_SPAWN          15
#define USYSCALL_WAIT           16
#define USYSCALL_GETTIMEOFDAY   17
#define USYSCALL_SEMCREATE      18
#define USYSCALL_SEMP           19
#define USYSCALL_SEMV           20
#define USYSCALL_DUMPPROCESSES  21

/* Maximum number of syscall arguments */
#define USYSCALL_MAX_ARGS       10

/* External declaration for system call vector */
extern void (*systemCallVec[MAXSYSCALLS])(USLOSS_Sysargs *args);

/* System call vector table entry */
typedef struct {
    int (*func)(void *);
    void *arg;
} USYSCALL_VectorEntry;

/* System call arguments structure */
typedef struct {
    int number;                    /* The system call number */
    int arg1;                      /* First argument */
    int arg2;                      /* Second argument */
    int arg3;                      /* Third argument */
    int arg4;                      /* Fourth argument */
    void *arg5;                    /* Fifth argument (pointer) */
} USYSCALL_Args;

/* System call function prototypes */
int USYSCALL_SystemCall(USYSCALL_Args *args);
int USYSCALL_RegisterHandler(int number, int (*func)(void *), void *arg);
int USYSCALL_ProcCreate(char *name, int (*func)(char *), char *arg, int stackSize, int priority, int *pid);
int USYSCALL_ProcJoin(int *pid, int *status);
void USYSCALL_ProcTerminate(int status);
int USYSCALL_GetPID(void);
int USYSCALL_GetTime(void);
int USYSCALL_SemCreate(int initialValue, int *handle);
int USYSCALL_SemP(int handle);
int USYSCALL_SemV(int handle);
int USYSCALL_SemFree(int handle);
int USYSCALL_DiskRead(int unit, void *buffer, int track, int first, int sectors);
int USYSCALL_DiskWrite(int unit, void *buffer, int track, int first, int sectors);
int USYSCALL_DiskSize(int unit, int *sector, int *track, int *disk);
int USYSCALL_TermRead(int unit, char *buffer, int bufferSize);
int USYSCALL_TermWrite(int unit, char *buffer, int bufferSize);
int USYSCALL_Spawn(char *name, int (*func)(char *), char *arg, int stackSize, int priority, int *pid);
int USYSCALL_Wait(int *pid, int *status);
int USYSCALL_GetTimeOfDay(void);
int USYSCALL_DumpProcesses(void);

#endif
