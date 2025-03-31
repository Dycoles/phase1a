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

int spawnSyscall(char *name, int (*func)(void *), void *arg, 
int stack_size, int priority, int *pid) {
    //USLOSS_Console("Now in spawn\n");
    //int result = trampoline();
}

int waitSyscall(int *pid, int *status) {
    //USLOSS_Console("Now in wait\n");
}

void terminateSyscall(int status) {
    //USLOSS_Console("Now in terminate\n");
    USLOSS_Halt(0); // FIXME Should wait for all children before terminating
}

int semCreateSyscall(int value, int *semaphore) {
    //USLOSS_Console("Now in create\n");
}

int semPSyscall(int semaphore) {
    //USLOSS_Console("Now in P\n");
}

int semVSyscall(int semaphore) {
    //USLOSS_Console("Now in V\n");
}

int kernSemCreate(int value, int *semaphore) {
    //USLOSS_Console("Now in kern create\n");
}

int kernSemP(int semaphore) {
    //USLOSS_Console("Now in kern P\n");
}

int kernSemV(int semaphore) {
    //USLOSS_Console("Now in kern V\n");
}

/*void getTimeofDaySyscall(int *tod) {

}

void getPIDSyscall(int *pid) {
    
}

void dumpProcessesSyscall() {

}*/



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
}

void phase3_start_service_processes() {

}
