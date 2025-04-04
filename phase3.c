#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase3_usermode.h>
#include <phase3_kernelInterfaces.h>

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    int value;
    int numBlockedProcs;
    int blockedMbox[MAXMBOX];
    int front;
    int back;
} Semaphore;

typedef struct {
    int trampID;
    int(*func)(void *);
    void *arg;
} trampolineFunc;

int userModeMBoxID;
int spawnTrampolineMBoxID;
trampolineFunc trampolineFuncs[MAXPROC];
int nextTrampID = 0;
Semaphore semaphoreList[MAXSEMS];
int curSem = 0;

void lock() {
    // Acquire the user mode lock:
    MboxRecv(userModeMBoxID, NULL, 0);
}

void unlock() {
    // Release the user mode lock:
    MboxSend(userModeMBoxID, NULL, 0);
}

int userModeTrampoline(void *arg) {
    // Set to user mode:
    int old_psr = USLOSS_PsrGet();
    if (USLOSS_PsrSet(old_psr & ~USLOSS_PSR_CURRENT_MODE) != USLOSS_DEV_OK) {
        USLOSS_Console("ERROR: Could not enable user mode\n");
        USLOSS_Halt(1);
    }

    // Parse the trampoline struct and call the function in user mode:
    trampolineFunc *trampArg = (trampolineFunc *)arg;
    int trampIndex = trampArg->trampID % MAXPROC;
    int result = trampolineFuncs[trampIndex].func(trampolineFuncs[trampIndex].arg);

    // Set the struct to unused and terminate:
    trampolineFuncs[trampIndex].trampID = -1;
    Terminate(result);

    // Should never get here:
    assert(0);
}

void spawnSyscall(USLOSS_Sysargs *args) {
    lock();

    // Find an available trampoline func struct:
    int thisTrampID;
    do {    // FIXME May loop infinitely
        thisTrampID = nextTrampID++;
    } while (trampolineFuncs[thisTrampID%MAXPROC].trampID >= 0);

    // Set up the trampoline struct:
    trampolineFuncs[thisTrampID%MAXPROC].trampID = thisTrampID;
    trampolineFuncs[thisTrampID%MAXPROC].func = (int(*)(void *))args->arg1;
    trampolineFuncs[thisTrampID%MAXPROC].arg = args->arg2;

    // Call spork with the appropriate args:
    unlock();
    MboxSend(spawnTrampolineMBoxID, NULL, 0);
    int newPID = spork((char *)args->arg5, userModeTrampoline, (void *)&trampolineFuncs[thisTrampID],
        (int)(long)args->arg3, (int)(long)args->arg4);
    MboxRecv(spawnTrampolineMBoxID, NULL, 0);
    lock();

    // Put the return values in the args struct:
    args->arg1 = (void *)(long)newPID;
    if (newPID >= 0) {
        args->arg4 = (void *)0;
    } else {
        args->arg4 = (void *)-1;
    }
    unlock();
}

void waitSyscall(USLOSS_Sysargs *args) {
    lock();

    int status;

    // Call join, saving its return values:
    unlock();
    int retVal = join(&status);
    lock();

    // Put return values into the args struct:
    args->arg1 = (void *)(long)retVal;
    args->arg2 = (void *)(long)status;
    if (retVal == -2) {
        args->arg4 = (void *) -2;
    } else {
        args->arg4 = 0;
    }

    unlock();
}

void terminateSyscall(USLOSS_Sysargs *args) {
    lock();

    int quitStatus = (int)(long)args->arg1;
    int joinStatus;
    int retVal = 0;

    // Join until no children remain:
    unlock();
    while (retVal != -2) {
        retVal = join(&joinStatus);
    }

    // Quit:
    quit(quitStatus);
}

void semCreateSyscall(USLOSS_Sysargs *args) {
    lock();

    // If out of semaphores or invalid input, return error:
    if (curSem >= MAXSEMS || args->arg1 < 0) {
        args->arg4 = (void *)-1;
    } else {
        // Set up the new semaphore:
        semaphoreList[curSem].value = (int)(long)args->arg1;
        semaphoreList[curSem].numBlockedProcs = 0;
        semaphoreList[curSem].front = 0;
        semaphoreList[curSem].back = 0;

        // Set the return values:
        args->arg1 = (void *)(long)curSem++;
        args->arg4 = 0;
    }
    
    unlock();
}

void semPSyscall(USLOSS_Sysargs *args) {
    lock();

    // Validate the semaphore ID (arg1):
    if (curSem >= MAXSEMS || args->arg1 < 0) {
        args->arg4 = (void *)-1;
    } else {
        int semID = (int)(long)args->arg1;

        // Get the semaphore to P(). If counter is zero, block until non-zero:
        int back = semaphoreList[semID].back;
        if (semaphoreList[semID].value == 0) {
            int mbox = MboxCreate(0, 0);    // Mailbox to block on

            // Add process to block queue to keep track of the order in which processes should unblock later:
            semaphoreList[semID].blockedMbox[back] = mbox;
            semaphoreList[semID].back = (back + 1) % MAXMBOX;
            semaphoreList[semID].numBlockedProcs++;

            // Block until this semaphore gets V()ed:
            unlock();   // unlock before we block
            MboxSend(mbox, NULL, 0);
            MboxRelease(mbox);
            lock(); // re-lock after send unblocks
        }

        // Decrement the semaphore value:
        semaphoreList[semID].value--;

        args->arg4 = 0; // return val
    }

    unlock();
}

void semVSyscall(USLOSS_Sysargs *args) {
    lock();

    // If invalid input, return error:
    if (curSem >= MAXSEMS || args->arg1 < 0) {
        args->arg4 = (void *)-1;
    } else {
        int semID = (int)(long)args->arg1;

        // Get the next semaphore and increment it:
        int front = semaphoreList[semID].front;
        semaphoreList[semID].value++;

        // if any P()s are blocked, unblock one
        if (semaphoreList[semID].numBlockedProcs > 0) {
            // Get the appropriate mailbox and receive on it:
            int mbox = semaphoreList[semID].blockedMbox[front];
            semaphoreList[semID].front = (front + 1) % MAXMBOX;

            unlock();
            MboxRecv(mbox, NULL, 0);
            lock();

            semaphoreList[semID].numBlockedProcs--;
        }

        args->arg4 = 0; // return value
    }

    unlock();
}

// FIXME Kern funs added so it compiles. Unsure what needs to be done for these.
int kernSemCreate(int value, int *semaphore) {
    /*if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call kernSemCreate while in user mode!\n");
        USLOSS_Halt(1);
    }*/
   return 0;
}
int kernSemP(int semaphore) {
    return 0;
}
int kernSemV(int semaphore) {
    return 0;
}

void getTimeOfDaySyscall(USLOSS_Sysargs *args) {
    // Return the current time of day:
    args->arg1 = (void *)(long)currentTime();
}

void getPidSyscall(USLOSS_Sysargs *args) {
    // Return the current process' PID:
    args->arg1 = (void *)(long)getpid();
}

void dumpProcessesSyscall() {
    // Print process information:
    dumpProcesses();
}

void phase3_init() {
    // Initialize every function in the syscall vec:
    systemCallVec[SYS_SPAWN] = (void (*)(USLOSS_Sysargs *))spawnSyscall;
    systemCallVec[SYS_WAIT] = (void (*)(USLOSS_Sysargs *))waitSyscall;
    systemCallVec[SYS_TERMINATE] = (void (*)(USLOSS_Sysargs *))terminateSyscall;
    systemCallVec[SYS_GETTIMEOFDAY] = (void (*)(USLOSS_Sysargs *))getTimeOfDaySyscall;
    systemCallVec[SYS_GETPID] = (void (*)(USLOSS_Sysargs *))getPidSyscall;
    systemCallVec[SYS_SEMCREATE] = (void (*)(USLOSS_Sysargs *))semCreateSyscall;
    systemCallVec[SYS_SEMP] = (void (*)(USLOSS_Sysargs *))semPSyscall;
    systemCallVec[SYS_SEMV] = (void (*)(USLOSS_Sysargs *))semVSyscall;
    //systemCallVec[SYS_SEMFREE] = (void (*)(USLOSS_Sysargs *));
    // TODO Maybe add kern sem funs?
    systemCallVec[SYS_DUMPPROCESSES] = (void (*)(USLOSS_Sysargs *))dumpProcessesSyscall;

    // Create the mailboxes used in this program:
    userModeMBoxID = MboxCreate(1, 0);
    MboxSend(userModeMBoxID, NULL, 0);  // make user mode lock available to start with
    spawnTrampolineMBoxID = MboxCreate(1, 0);

    // Initialize the list of semaphores:
    curSem = 0;
    for (int i = 0; i < MAXSEMS; i++) {
        semaphoreList[i].value = -1;
        semaphoreList[i].numBlockedProcs = -1;
        semaphoreList[i].front = -1;
        semaphoreList[i].back = -1;
    }

    // Initialize the list of trampoline functions:
    for (int i = 0; i < MAXPROC; i++) {
        trampolineFuncs[i].trampID = -1;
        trampolineFuncs[i].func = NULL;
        trampolineFuncs[i].arg = NULL;
    }
}

void phase3_start_service_processes() {
    // Deliberately left blank.
    // TODO Should it be deliberately left blank?
}
