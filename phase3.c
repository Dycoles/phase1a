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

int userModeMBoxID;
int spawnTrampolineMBoxID;
int(*trampolineFunc)(void *);
int semaphores[MAXSEMS];
int curSem;

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
    int old_psr = USLOSS_PsrGet();
    if (USLOSS_PsrSet(old_psr & ~1) != 0) {
        USLOSS_Console("ERROR: cannot disable interrupts in user mode\n");
        USLOSS_Halt(1);
    }

    int result = trampolineFunc(arg);

    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: cannot enable interrupts in user mode\n");
    }
    int x = USLOSS_PsrSet(old_psr); x++;

    return result;
}

void spawnSyscall(USLOSS_Sysargs *args) {   // FIXME Error with running in kernel mode when it shouldn't be
    //USLOSS_Console("Now in spawn\n");
    int old_psr = lockUserModeMBox();

    MboxSend(spawnTrampolineMBoxID, NULL, 0);
    trampolineFunc = (int(*)(void *))args->arg1;
    int newPID = spork((char *)args->arg5, userModeTrampoline, args->arg2, (int)args->arg3, (int)args->arg4);
    MboxRecv(spawnTrampolineMBoxID, NULL, 0);

    args->arg1 = (void *)newPID;
    if (newPID >= 0) {
        args->arg4 = (void *)0;
    } else {
        args->arg4 = (void *)-1;
    }

    unlockUserModeMBox(old_psr);
    //USLOSS_Console("End of innerSpawn\n");
}

int waitSyscall(USLOSS_Sysargs *args) {
    //USLOSS_Console("Now in wait\n");
    int old_psr = lockUserModeMBox();

    int status;
    int retVal = join(&status);

    args->arg1 = (void *) retVal;
    args->arg2 = (void *) status;
    if (retVal == -2) {
        args->arg4 = (void *) -2;
    } else {
        args->arg4 = (void *) 0;
    }

    unlockUserModeMBox(old_psr);
    //USLOSS_Console("End of innerWait\n");
}

void terminateSyscall(USLOSS_Sysargs *args) {
    //USLOSS_Console("Now in terminate\n");
    int old_psr = lockUserModeMBox();
    USLOSS_Halt(0); // FIXME Should wait for all children before terminating
}

void semCreateSyscall(USLOSS_Sysargs *args) {
    //USLOSS_Console("Now in create\n");
    int old_psr = lockUserModeMBox();
    
    // If out of semaphores or invalid input, return error:
    if (curSem >= MAXSEMS || args->arg1 < 0) {
        args->arg4 = -1;
    } else {
        semaphores[curSem] = args->arg1;
        args->arg1 = curSem++;
        args->arg4 = 0;
    }

    unlockUserModeMBox(old_psr);
}

void semPSyscall(USLOSS_Sysargs *args) {
    //USLOSS_Console("Now in P\n");
}

void semVSyscall(USLOSS_Sysargs *args) {
    //USLOSS_Console("Now in V\n");
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




void phase3_init() {
    systemCallVec[SYS_SPAWN] = (void (*)(USLOSS_Sysargs *))spawnSyscall;
    systemCallVec[SYS_WAIT] = (void (*)(USLOSS_Sysargs *))waitSyscall;
    systemCallVec[SYS_TERMINATE] = (void (*)(USLOSS_Sysargs *))terminateSyscall;
    systemCallVec[SYS_GETTIMEOFDAY] = (void (*)(USLOSS_Sysargs *))currentTime;
    systemCallVec[SYS_GETPID] = (void (*)(USLOSS_Sysargs *))getpid;
    systemCallVec[SYS_SEMCREATE] = (void (*)(USLOSS_Sysargs *))semCreateSyscall;
    systemCallVec[SYS_SEMP] = (void (*)(USLOSS_Sysargs *))semPSyscall;
    systemCallVec[SYS_SEMV] = (void (*)(USLOSS_Sysargs *))semVSyscall;
    //systemCallVec[SYS_SEMFREE] = (void (*)(USLOSS_Sysargs *));
    // TODO Maybe add kern sem funs?
    systemCallVec[SYS_DUMPPROCESSES] = (void (*)(USLOSS_Sysargs *))dumpProcesses;

    userModeMBoxID = MboxCreate(1, 0);
    spawnTrampolineMBoxID = MboxCreate(1, 0);
    //waitingMBoxID = MboxCreate(1, 0);
    //terminatingMBoxID = MboxCreate(1, 0);

    curSem = 0;
    for (int i = 0; i < MAXSEMS; i++) {
        semaphores[i] = -1;
    }
}

void phase3_start_service_processes() {

}
