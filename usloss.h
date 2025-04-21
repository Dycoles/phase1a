#ifndef _USLOSS_H
#define _USLOSS_H

#include <stdint.h>

/* Common USLOSS constants */
#define USLOSS_MAX_PROCS    50
#define USLOSS_MAX_DEVICES  20
#define USLOSS_MAX_SLOTS    16
#define USLOSS_MAX_ARGS     10
#define MAXSYSCALLS         50  /* Maximum number of system calls */
#define USLOSS_MIN_STACK    0

/* Process states */
#define USLOSS_PROC_READY    1
#define USLOSS_PROC_RUNNING  2
#define USLOSS_PROC_BLOCKED  3
#define USLOSS_PROC_QUIT     4

/* Device types */
#define USLOSS_CLOCK_DEV     0
#define USLOSS_DISK_DEV      1
#define USLOSS_TERM_DEV      2
#define USLOSS_ALARM_DEV     3

/* Interrupt types */
#define USLOSS_CLOCK_INT     0
#define USLOSS_DISK_INT      1
#define USLOSS_TERM_INT      2
#define USLOSS_SYSCALL_INT   3
#define USLOSS_ILLEGAL_INT   4
#define USLOSS_MAX_INT       5

/* Status values */
#define USLOSS_DEV_READY     0
#define USLOSS_DEV_BUSY      1
#define USLOSS_DEV_ERROR     2
#define USLOSS_DEV_OK        3

/* Error codes */
#define USLOSS_ERR_OK        0
#define USLOSS_ERR_INVALID  -1
#define USLOSS_ERR_NOMEM    -2
#define USLOSS_ERR_NOTFOUND -3
#define USLOSS_ERR_TIMEOUT  -4

/* PSR-related constants */
#define USLOSS_PSR_CURRENT_INT 0x2
#define USLOSS_PSR_CURRENT_MODE 0x1

typedef unsigned int PSR;       /* Program Status Register */
typedef unsigned int Context;   /* Context of a process */
typedef void (*funcptr)(void*); /* Function pointer type */
typedef void (*interrupt_handler)(int dev, void *arg); /* Interrupt handler type */

/* System args structure for system calls */
typedef struct USLOSS_Sysargs {
    int number;
    void *arg1;
    void *arg2;
    void *arg3;
    void *arg4;
    void *arg5;
} USLOSS_Sysargs;

/* Device interrupt structure */
typedef struct {
    int status;
    int value;
} USLOSS_DeviceStatus;

/* Process control block structure */
typedef struct USLOSS_ProcStruct USLOSS_ProcStruct;
struct USLOSS_ProcStruct {
    int pid;                          /* Process ID */
    char name[32];                    /* Process name */
    int state;                        /* Process state */
    int priority;                     /* Process priority */
    Context context;                  /* Saved context */
    void *stack;                      /* Stack pointer */
    int stackSize;                    /* Stack size */
    funcptr startFunc;                /* Starting function */
    void *startArg;                   /* Argument to the starting function */
    USLOSS_ProcStruct *nextProc;      /* For implementing ready queue */
};

/* Interrupt vector table - array of interrupt handlers */
extern interrupt_handler USLOSS_IntVec[USLOSS_MAX_INT];

/* Function prototypes */
int USLOSS_Init(int numProcs, int numDevices);
int USLOSS_Spawn(char *name, funcptr func, void *arg, int stackSize, int priority, int *pid);
int USLOSS_Terminate(int status);
int USLOSS_GetCPUTime(void);
int USLOSS_GetPID(void);
int USLOSS_GetPSR(void);
void USLOSS_SetPSR(PSR psr);
int USLOSS_DeviceInput(int type, int unit, int *status);
int USLOSS_DeviceOutput(int type, int unit, void *arg);
int USLOSS_WaitDevice(int type, int unit, int *status);
int USLOSS_DiskSize(int unit, int *sector, int *track, int *disk);
int USLOSS_DiskRead(int unit, void *buffer, int track, int first, int sectors);
int USLOSS_DiskWrite(int unit, void *buffer, int track, int first, int sectors);
int USLOSS_ContextSwitch(USLOSS_ProcStruct *old, USLOSS_ProcStruct *new);
int USLOSS_ProcTable(USLOSS_ProcStruct *table, int max);
void USLOSS_Halt(int code);
void USLOSS_Console(char *format, ...);
void USLOSS_Trace(char *format, ...);

/* Interrupt management */
void USLOSS_EnableInterrupts(void);
void USLOSS_DisableInterrupts(void);
int USLOSS_InKernelMode(void);

/* System call management */
void USLOSS_Syscall(void *arg);

#endif
