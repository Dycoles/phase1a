#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <phase3_usermode.h>
#include <phase3_kernelInterfaces.h>
#include <phase4_usermode.h>

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

int userModeMBoxID;

/*
 * The lock() function gains the lock for user mode operations.
 */
void lock() {
    // Acquire the user mode lock:
    MboxRecv(userModeMBoxID, NULL, 0);
}

/*
 * The unlock() function releases the lock for user mode operations.
 */
void unlock() {
    // Release the user mode lock:
    MboxSend(userModeMBoxID, NULL, 0);
}

void sleepSyscall(USLOSS_Sysargs *args) {
    lock();
    int seconds = (int)(long)args->arg1;
    if (seconds < 0) {
        args->arg4 = (void *) -1;
    } else {
        // successfull input, perform operation

        args->arg4 = (void *) 0;
    }
    unlock();
}

void termReadSyscall(USLOSS_Sysargs *args) {
    lock();
    if () {
        args->arg4 = (void *) -1;
    } else {
        // successful input, perform operation
        args->arg4 = (void *) 0;
    }
    unlock();
}

void termWriteSyscall(USLOSS_Sysargs *args) {
    lock();
    if () {
        args->arg4 = (void *) -1;
    } else {
        // successful input, perform operation
        args->arg4 = (void *) 0;
    }
    unlock();
}

void diskSizeSyscall(USLOSS_Sysargs *args) {
    lock();
    if () {
        args->arg4 = (void *) -1;
    } else {
        // successful input, perform operation
        args->arg4 = (void *) 0;
    }
    unlock();
}

void diskReadSyscall(USLOSS_Sysargs *arg) {
    lock();
    if () {
        args->arg4 = (void *) -1;
    } else {
        // successful input, perform operation
        args->arg4 = (void *) 0;
    }
    unlock();
}

void diskWriteSyscall(USLOSS_Sysargs *arg) {
    lock();
    if () {
        args->arg4 = (void *) -1;
    } else {
        // successful input, perform operation
        args->arg4 = (void *) 0;
    }
    unlock();
}

void phase4_init() {
    systemCallVec[SYS_SLEEP] = (void (*)(USLOSS_Sysargs *))sleepSyscall;
    systemCallVec[SYS_TERMREAD] = (void (*)(USLOSS_Sysargs *))termReadSyscall;
    systemCallVec[SYS_TERMWRITE] = (void (*)(USLOSS_Sysargs *))termWriteSyscall;
    systemCallVec[SYS_DISKSIZE] = (void (*)(USLOSS_Sysargs *))diskSizeSyscall;
    systemCallVec[SYS_DISKREAD] = (void (*)(USLOSS_Sysargs *))diskReadSyscall;
    systemCallVec[SYS_DISKWRITE] = (void (*)(USLOSS_Sysargs *))diskWriteSyscall;
    
    // Create the mailboxes used in this program:
    userModeMBoxID = MboxCreate(1, 0);
    // make user mode lock available to start with
    MboxSend(userModeMBoxID, NULL, 0);  
}

void phase4_start_service_processes() {

}
