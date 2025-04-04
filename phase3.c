#include <phase3_kernelInterfaces.h>
#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase3_usermode.h>

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
    // acquire the lock
    // USLOSS_Console("Attempting to acquire lock\n");
    MboxRecv(userModeMBoxID, NULL, 0);
    // USLOSS_Console("Lock acquired\n");
}

void unlock() {
    // release the lock 
    // USLOSS_Console("Releasing lock\n");
    MboxSend(userModeMBoxID, NULL, 0);
    // USLOSS_Console("Lock released\n");
}

int userModeTrampoline(void *arg) {
    int old_psr = USLOSS_PsrGet();
    // set to user mode
    if (USLOSS_PsrSet(old_psr & ~USLOSS_PSR_CURRENT_MODE) != USLOSS_DEV_OK) {
        USLOSS_Console("%d\t", USLOSS_PsrGet());
        USLOSS_Console("1 ERROR: cannot disable interrupts in user mode\n");
        USLOSS_Halt(1);
    }
    trampolineFunc *trampArg = (trampolineFunc *)arg;

    int trampIndex = trampArg->trampID % MAXPROC;
    int result = trampolineFuncs[trampIndex].func(trampolineFuncs[trampIndex].arg);
    trampolineFuncs[trampIndex].trampID = -1;
    Terminate(result);
    // should never get here
    return result;
}

void spawnSyscall(USLOSS_Sysargs *args) {
    //int old_psr = lockUserModeMBox();
    lock();
    int thisTrampID;
    do {    // FIXME May loop infinitely
        thisTrampID = nextTrampID++;
    } while (trampolineFuncs[thisTrampID%MAXPROC].trampID >= 0);

    trampolineFuncs[thisTrampID%MAXPROC].trampID = thisTrampID;
    trampolineFuncs[thisTrampID%MAXPROC].func = (int(*)(void *))args->arg1;
    trampolineFuncs[thisTrampID%MAXPROC].arg = args->arg2;
    unlock();
    MboxSend(spawnTrampolineMBoxID, NULL, 0);
    int newPID = spork((char *)args->arg5, userModeTrampoline, (void *)&trampolineFuncs[thisTrampID],
        (int)(long)args->arg3, (int)(long)args->arg4);
    MboxRecv(spawnTrampolineMBoxID, NULL, 0);
    lock();
    args->arg1 = (void *)(long)newPID;
    if (newPID >= 0) {
        args->arg4 = (void *)0;
    } else {
        args->arg4 = (void *)-1;
    }
    unlock();
    //unlockUserModeMBox(old_psr);
}

void waitSyscall(USLOSS_Sysargs *args) {
    //int old_psr = lockUserModeMBox();
    lock();
    int status;
    unlock();
    int retVal = join(&status);
    lock();
    args->arg1 = (void *)(long)retVal;
    args->arg2 = (void *)(long)status;
    if (retVal == -2) {
        args->arg4 = (void *) -2;
    } else {
        args->arg4 = 0;
    }
    unlock();
    //unlockUserModeMBox(old_psr);
}

void terminateSyscall(USLOSS_Sysargs *args) {
    //int old_psr = lockUserModeMBox();
    lock();
    int quitStatus = (int)(long)args->arg1;
    int joinStatus;
    int retVal = 0;
    unlock();
    while (retVal != -2) {
        retVal = join(&joinStatus);
    }
    quit(quitStatus);
}

void semCreateSyscall(USLOSS_Sysargs *args) {
    //int old_psr = lockUserModeMBox();
    lock();
    // If out of semaphores or invalid input, return error:
    if (curSem >= MAXSEMS || args->arg1 < 0) {
        args->arg4 = (void *)-1;
    } else {
        semaphoreList[curSem].value = (int)(long)args->arg1;
        semaphoreList[curSem].numBlockedProcs = 0;
        semaphoreList[curSem].front = 0;
        semaphoreList[curSem].back = 0;
        args->arg1 = (void *)(long)curSem++;
        args->arg4 = 0;
    }
    //unlockUserModeMBox(old_psr);
    unlock();
}

void semPSyscall(USLOSS_Sysargs *args) {
    // arg 1 is ID of semaphor; validate ID
    lock();
    if (curSem >= MAXSEMS || args->arg1 < 0) {
        args->arg4 = (void *)-1;
        unlock();
    } else {
        int semID = (int)(long)args->arg1;
        // if counter is zero, block until nonzero
        int back = semaphoreList[semID].back;
        if (semaphoreList[semID].value == 0) {
            int mbox = MboxCreate(0, 0);
            // add process to block queue to keep track of which order processes should unblock later
            semaphoreList[semID].blockedMbox[back] = mbox;
            semaphoreList[semID].back = (back + 1) % MAXMBOX;
            semaphoreList[semID].numBlockedProcs++;
            // unlock before we block
            unlock();
            MboxSend(mbox, NULL, 0);
            MboxRelease(mbox);
            // lock after send unblocks
            lock();
        }
        // then decrement
        semaphoreList[semID].value--;
        args->arg4 = 0;
        // unlock after we decrement
        unlock();
    }
}

void semVSyscall(USLOSS_Sysargs *args) {
    lock();
    if (curSem >= MAXSEMS || args->arg1 < 0) {
        args->arg4 = (void *)-1;
        unlock();
    } else {
        int semID = (int)(long)args->arg1;
        int front = semaphoreList[semID].front;
        // increment counter
        semaphoreList[semID].value++;
        // if any P()s are blocked, unblock one
        if (semaphoreList[semID].numBlockedProcs > 0) {
            int mbox = semaphoreList[semID].blockedMbox[front];
            semaphoreList[semID].front = (front + 1) % MAXMBOX;
            unlock();
            MboxRecv(mbox, NULL, 0);
            lock();
            semaphoreList[semID].numBlockedProcs--;
        }
        args->arg4 = 0;
        unlock();
    }
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
    args->arg1 = (void *)(long)currentTime();
}

void getPidSyscall(USLOSS_Sysargs *args) {
    args->arg1 = (void *)(long)getpid();
}

void dumpProcessesSyscall() {
    dumpProcesses();
}

void phase3_init() {
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

    userModeMBoxID = MboxCreate(1, 0);
    // make lock available
    MboxSend(userModeMBoxID, NULL, 0);
    spawnTrampolineMBoxID = MboxCreate(1, 0);

    curSem = 0;
    for (int i = 0; i < MAXSEMS; i++) {
        semaphoreList[i].value = -1;
        semaphoreList[i].numBlockedProcs = -1;
        semaphoreList[i].front = -1;
        semaphoreList[i].back = -1;
    }

    for (int i = 0; i < MAXPROC; i++) {
        trampolineFuncs[i].trampID = -1;
        trampolineFuncs[i].func = NULL;
        trampolineFuncs[i].arg = NULL;
    }
}

void phase3_start_service_processes() {

}
