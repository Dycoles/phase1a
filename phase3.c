#include <phase3_kernelInterfaces.h>
#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
typedef struct {
    int value;
    int blockedMbox;
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

int lockUserModeMBox() {
    MboxSend(userModeMBoxID, NULL, 0);
    int old_psr = USLOSS_PsrGet();  // FIXME This may not be allowed
    // ensure we are in kernel mode
    if (USLOSS_PsrSet(old_psr & ~USLOSS_PSR_CURRENT_INT) != 0) {
        USLOSS_Console("ERROR: cannot disable interrupts in user mode\n");
        USLOSS_Halt(1);
    }
    return old_psr;
}

void unlockUserModeMBox(int old_psr) {
    MboxRecv(userModeMBoxID, NULL, 0);
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: cannot enable interrupts in user mode\n");
    }
    // restore interrupts; x used to keep compiler happy
    int x = USLOSS_PsrSet(old_psr); x++;   // FIXME This may not be allowed
}

int userModeTrampoline(void *arg) {
    // USLOSS_Console("In trampoline: %d\n", USLOSS_PsrGet());
    int old_psr = USLOSS_PsrGet();
    if (USLOSS_PsrSet(old_psr & ~USLOSS_PSR_CURRENT_MODE) != USLOSS_DEV_OK) {
        USLOSS_Console("%d\t", USLOSS_PsrGet());
        USLOSS_Console("1 ERROR: cannot disable interrupts in user mode\n");
        USLOSS_Halt(1);
    }

    trampolineFunc *trampArg = (trampolineFunc *)arg;

    int trampIndex = trampArg->trampID%MAXPROC;
    int result = trampolineFuncs[trampIndex].func(trampolineFuncs[trampIndex].arg);
    trampolineFuncs[trampIndex].trampID = -1;

    // USLOSS_Console("Result %d received from trampoline\n", result);
    Terminate(result);
    int x = USLOSS_PsrSet(old_psr); x++;
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("%d\t", USLOSS_PsrGet());
        USLOSS_Console("2 ERROR: cannot enable interrupts in user mode\n");
    }
    // USLOSS_Console("End Trampoline, return restult\n");
    return result;
}

void spawnSyscall(USLOSS_Sysargs *args) {   // FIXME Error with running in kernel mode when it shouldn't be
    // USLOSS_Console("Top of spawn: %s %lu\n", (char *)args->arg5, (unsigned long)trampolineFunc);
    //USLOSS_Console("Now in spawn: %d\n", USLOSS_PsrGet());
    //int old_psr = lockUserModeMBox();

    // FIXME An error with test23: spork doesn't immediately call the startfunc, and another spawn immediately after overwrites it
    MboxSend(spawnTrampolineMBoxID, NULL, 0);
    int thisTrampID;
    do {    // FIXME May loop infinitely
        thisTrampID = nextTrampID++;
    } while (trampolineFuncs[thisTrampID%MAXPROC].trampID >= 0);

    trampolineFuncs[thisTrampID%MAXPROC].trampID = thisTrampID;
    trampolineFuncs[thisTrampID%MAXPROC].func = (int(*)(void *))args->arg1;
    trampolineFuncs[thisTrampID%MAXPROC].arg = args->arg2;

    // USLOSS_Console("Sporking\n");
    int newPID = spork((char *)args->arg5, userModeTrampoline, (void *)&trampolineFuncs[thisTrampID], (int)args->arg3, (int)args->arg4);
    // USLOSS_Console("After Spork\n");
    MboxRecv(spawnTrampolineMBoxID, NULL, 0);
    // USLOSS_Console("After Receive\n");
    //USLOSS_Console("After mbox in spawn: %s\n", (char *)args->arg5);

    args->arg1 = (void *)newPID;
    if (newPID >= 0) {
        args->arg4 = (void *)0;
    } else {
        args->arg4 = (void *)-1;
    }

    //unlockUserModeMBox(old_psr);
    // USLOSS_Console("End of innerSpawn\n");
}

void waitSyscall(USLOSS_Sysargs *args) {
    // USLOSS_Console("Now in wait: %d\n", USLOSS_PsrGet());
    //int old_psr = lockUserModeMBox();

    int status;
    int retVal = join(&status);
    //MboxSend(spawnTrampolineMBoxID, NULL, 0);
    //trampolineFunc = join;
    //USLOSS_Console("Before Join\n");
    //int retVal = userModeTrampoline(&status);
    //USLOSS_Console("After Join\n");
    //MboxRecv(spawnTrampolineMBoxID, NULL, 0);

    args->arg1 = (void *) retVal;
    args->arg2 = (void *) status;
    if (retVal == -2) {
        args->arg4 = (void *) -2;
    } else {
        args->arg4 = (void *) 0;
    }

    //unlockUserModeMBox(old_psr);
    // USLOSS_Console("End of innerWait\n");
}

void terminateSyscall(USLOSS_Sysargs *args) {
    // USLOSS_Console("Now in terminate\n");
    //int old_psr = lockUserModeMBox();

    int quitStatus = args->arg1;
    int joinStatus;
    int retVal;
    //USLOSS_Console("Before Terminate Loop: %d\n", USLOSS_PsrGet());
    while (retVal != -2) {
        //USLOSS_Console("In Terminate Loop: %d\n", joinStatus);
        
        retVal = join(&joinStatus);
        //USLOSS_Console("%d\n", retVal);
        //waitSyscall(args);
    }
    // USLOSS_Console("In Post-Loop Terminate\n");
    quit(quitStatus);
}

void semCreateSyscall(USLOSS_Sysargs *args) {
    //USLOSS_Console("Now in create\n");
    //int old_psr = lockUserModeMBox();
    
    // If out of semaphores or invalid input, return error:
    if (curSem >= MAXSEMS || args->arg1 < 0) {
        args->arg4 = -1;
    } else {
        semaphoreList[curSem].value = args->arg1;
        semaphoreList[curSem].blockedMbox = MboxCreate(0, 0);
        args->arg1 = curSem++;
        args->arg4 = 0;
    }
    //unlockUserModeMBox(old_psr);
}

void semPSyscall(USLOSS_Sysargs *args) {
    //USLOSS_Console("Now in P\n");
    // arg 1 is ID of semaphor; validate ID
    if (curSem >= MAXSEMS || args->arg1 < 0) {
        args->arg4 = -1;
    } else {
        args->arg4 = 0;
        int semID = (int)(long)args->arg1;
        // if counter is zero, block until nonzero
        if (semaphoreList[semID].value == 0) {
            // USLOSS_Console("SemP Blocking until nonzero\n");
            MboxSend(semaphoreList[semID].blockedMbox, NULL, 0);
        }
        // then decrement
        // USLOSS_Console("SemP Decrementing semaphore list\n");
        semaphoreList[semID].value--;
    }
}

void semVSyscall(USLOSS_Sysargs *args) {
    //USLOSS_Console("Now in V\n");
    if (curSem >= MAXSEMS || args->arg1 < 0) {
        args->arg4 = -1;
    } else {
        args->arg4 = 0;
        int semID = (int)(long)args->arg1;
        // increment counter
        // USLOSS_Console("SemV increment counter\n");
        semaphoreList[semID].value++;
        // if any P()s are blocked, unblock one
        if (semaphoreList[semID].value == 1) {
            MboxRecv(semaphoreList[semID].blockedMbox, NULL, 0);
        }
    }
}

// FIXME Kern funs added so it compiles. Unsure what needs to be done for these.
int kernSemCreate(int value, int *semaphore) {
    //USLOSS_Console("Now in kern create\n");
}
int kernSemP(int semaphore) {
    //USLOSS_Console("Now in kern P\n");
}
int kernSemV(int semaphore) {
    //USLOSS_Console("Now in kern V\n");
}

void getTimeOfDaySyscall(USLOSS_Sysargs *args) {
    args->arg1 = currentTime();
}

void getPidSyscall(USLOSS_Sysargs *args) {
    args->arg1 = getpid();
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
    spawnTrampolineMBoxID = MboxCreate(1, 0);
    //waitingMBoxID = MboxCreate(1, 0);
    //terminatingMBoxID = MboxCreate(1, 0);

    curSem = 0;
    for (int i = 0; i < MAXSEMS; i++) {
        semaphoreList[i].value = -1;
        semaphoreList[i].blockedMbox = -1;
    }

    for (int i = 0; i < MAXPROC; i++) {
        trampolineFuncs[i].trampID = -1;
        trampolineFuncs[i].func = NULL;
        trampolineFuncs[i].arg = NULL;
    }
}

void phase3_start_service_processes() {

}
