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
    int semID;
    int result = kernSemCreate((int)(long)args->arg1, &semID);
    if (result == 0) {
        args->arg1 = (void *)(long)semID;
        args->arg4 = 0;
    } else {
        args->arg4 = (void *)-1;
    }
}

void semPSyscall(USLOSS_Sysargs *args) {
    int result = kernSemP((int)(long)args->arg1);
    if (result == 0) {
        args->arg4 = 0;
    } else {
        args->arg4 = (void *)-1;
    }
}

void semVSyscall(USLOSS_Sysargs *args) {
    int result = kernSemV((int)(long)args->arg1);
    if (result == 0) {
        args->arg4 = 0;
    } else {
        args->arg4 = (void *)-1;
    }
}

// FIXME Kern funs added so it compiles. Unsure what needs to be done for these.
int kernSemCreate(int value, int *semaphore) {
    lock();
    // If out of semaphores or invalid input, return error:
    if (curSem >= MAXSEMS || value < 0) {
        unlock();
        return -1;
    }
    // Set up the new semaphore:
    semaphoreList[curSem].value = value;
    semaphoreList[curSem].numBlockedProcs = 0;
    semaphoreList[curSem].front = 0;
    semaphoreList[curSem].back = 0;

    *semaphore = curSem++; 
    unlock();
    return 0;
}

int kernSemP(int semaphore) {
    lock();
    // Validate the semaphore ID (arg1):
    if (semaphore < 0 || semaphore >= curSem) {
        unlock();
        return -1;
    }
    // Get the semaphore to P(). If counter is zero, block until non-zero:
    if (semaphoreList[semaphore].value == 0) {
        int mbox = MboxCreate(0, 0);
        int back = semaphoreList[semaphore].back;
        // Add process to block queue to keep track of the order in which processes should unblock later:
        semaphoreList[semaphore].blockedMbox[back] = mbox;
        semaphoreList[semaphore].back = (back + 1) % MAXMBOX;
        semaphoreList[semaphore].numBlockedProcs++;
        // release the lock
        unlock(); 
        // Block until this semaphore gets V()ed:
        MboxSend(mbox, NULL, 0);
        // release the mailbox so we don't take up space
        MboxRelease(mbox); 
        // reacquire the lock
        lock(); 
    }
    // Decrement the semaphore value:
    semaphoreList[semaphore].value--;

    unlock();
    return 0;
}

int kernSemV(int semaphore) {
    lock();
    // If invalid input, return error:
    if (semaphore < 0 || semaphore >= curSem) {
        unlock();
        return -1;
    }
    // increment value
    semaphoreList[semaphore].value++;
    // if any P()s are blocked, unblock one
    if (semaphoreList[semaphore].numBlockedProcs > 0) {
        // Get the next semaphore and increment it:
        int front = semaphoreList[semaphore].front;
        // get the mailbox
        int mbox = semaphoreList[semaphore].blockedMbox[front];
        semaphoreList[semaphore].front = (front + 1) % MAXMBOX;
        semaphoreList[semaphore].numBlockedProcs--;

        unlock();
        // wake up blocked process
        MboxRecv(mbox, NULL, 0);
        lock();
    }

    unlock();
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
