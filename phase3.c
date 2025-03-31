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

void spawnSyscall(USLOSS_Sysargs *args) {   // FIXME Error with running in kernel mode when it shouldn't be
    //USLOSS_Console("Now in spawn\n");
    int old_psr = lockUserModeMBox();

    int newPID = spork((char *)args->arg5, (int(*)(void *))args->arg1, args->arg2, (int)args->arg3, (int)args->arg4);

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

int semCreateSyscall(USLOSS_Sysargs *args) {
    //USLOSS_Console("Now in create\n");
}

int semPSyscall(USLOSS_Sysargs *args) {
    //USLOSS_Console("Now in P\n");
}

int semVSyscall(USLOSS_Sysargs *args) {
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
    //waitingMBoxID = MboxCreate(1, 0);
    //terminatingMBoxID = MboxCreate(1, 0);
}

void phase3_start_service_processes() {

}
