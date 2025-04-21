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
int x;

typedef struct {
    int wakeTime;
    int mboxID;
} SleepProc;

// create a list of sleeping processes
SleepProc sleepList[MAXPROC];
int sleepCount;

// keep track of clock interrupts
int clockInterrupts = 0;

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
    int seconds = (int)(long)args->arg1;
    if (seconds < 0) {
        // invalid input, return -1
        args->arg4 = (void *) -1;
        return;
    }
    lock();
    // successfull input, perform operation
    int mbox = MboxCreate(1, 0);
    int wakeTime = clockInterrupts + seconds * 10;
    if (sleepCount < MAXPROC) {
        sleepList[sleepCount].wakeTime = wakeTime;
        sleepList[sleepCount].mboxID = mbox;
        sleepCount++;
    } else {
        // too many sleeping processes, return -1
        args->arg4 = (void *) -1;
        unlock();
        return;
    }
    unlock();
    // block until wake up
    MboxRecv(mbox, NULL, 0);
    // release after wake up
    MboxRelease(mbox);
    // valid operation, return 0;
    args->arg4 = (void *) 0;
}


void termReadSyscall(USLOSS_Sysargs *args) {
    lock();
    args->arg4 = 0;

    int charsInput;
    if (args->arg2 < MAXLINE) {
        charsInput = args->arg2;
    } else {
        charsInput = MAXLINE;
    }

    char *readBuffer = args->arg1;
    int i;
    for (int i = 0; i < charsInput; i++) {
        int DSRContents;
        int readStatus = USLOSS_DeviceInput(USLOSS_TERM_DEV, args->arg3, &DSRContents);
        if (readStatus != USLOSS_DEV_OK) {
            USLOSS_Console("Error in read\n");
            args->arg4 = -1;
            break;
        }
        char thisChar = (char)(DSRContents << 8);
        readBuffer[i] = thisChar;
        USLOSS_Console("End of read loop: %d\n", args->arg3);
    }
    args->arg2 = i;

    // Check for illegal values:
    //if (x) {
    //    args->arg4 = (void *) -1;
    //} else {
    //    // successful input, perform operation
    //    args->arg4 = (void *) 0;
    //}
    
    unlock();
}

void termWriteSyscall(USLOSS_Sysargs *args) {
    lock();
    if (x) {
        args->arg4 = (void *) -1;
    } else {
        // successful input, perform operation
        args->arg4 = (void *) 0;
    }
    unlock();
}

// To implement in phase 4b
void diskSizeSyscall(USLOSS_Sysargs *args) {
    lock();
    if (x) {
        args->arg4 = (void *) -1;
    } else {
        // successful input, perform operation
        args->arg4 = (void *) 0;
    }
    unlock();
}

// To implement in phase 4b
void diskReadSyscall(USLOSS_Sysargs *args) {
    lock();
    if (x) {
        args->arg4 = (void *) -1;
    } else {
        // successful input, perform operation
        args->arg4 = (void *) 0;
    }
    unlock();
}

// To implement in phase 4b
void diskWriteSyscall(USLOSS_Sysargs *args) {
    lock();
    if (x) {
        args->arg4 = (void *) -1;
    } else {
        // successful input, perform operation
        args->arg4 = (void *) 0;
    }
    unlock();
}

int clockDriver(void *arg) {
    int status;
    // use infinite loop which increments counter each time interrupt is received
    while (1) {
        waitDevice(USLOSS_CLOCK_DEV, 0, &status);
        clockInterrupts++;
        lock();
        // repeatedly loop through sleep queue to check if it is time to wake up the process
        for (int i = 0; i < sleepCount; i++) {
            if (sleepList[i].wakeTime <= clockInterrupts) {
                // wake up the process
                MboxSend(sleepList[i].mboxID, NULL, 0);
                // remove the process from the queue and shift the items
                for (int j = i; j < sleepCount - 1; j++) {
                    sleepList[j] = sleepList[j+1];
                }
                sleepCount--;
                i--;
            }
        }
        unlock();
    }
    return 0;
}

int terminalDriver(void *arg) {
    waitDevice(USLOSS_TERM_DEV, 0, NULL);
    return 0;
}

int diskDriver(void *arg) {
    waitDevice(USLOSS_DISK_DEV, 0, NULL);
    return 0;
}

void phase4_init() {
    // phase4a syscalls
    systemCallVec[SYS_SLEEP] = (void (*)(USLOSS_Sysargs *))sleepSyscall;
    systemCallVec[SYS_TERMREAD] = (void (*)(USLOSS_Sysargs *))termReadSyscall;
    systemCallVec[SYS_TERMWRITE] = (void (*)(USLOSS_Sysargs *))termWriteSyscall;
    // phase4b syscalls
    systemCallVec[SYS_DISKSIZE] = (void (*)(USLOSS_Sysargs *))diskSizeSyscall;
    systemCallVec[SYS_DISKREAD] = (void (*)(USLOSS_Sysargs *))diskReadSyscall;
    systemCallVec[SYS_DISKWRITE] = (void (*)(USLOSS_Sysargs *))diskWriteSyscall;
    
    // Create the mailboxes used in this program:
    userModeMBoxID = MboxCreate(1, 0);
    // make user mode lock available to start with
    MboxSend(userModeMBoxID, NULL, 0);  
}

void phase4_start_service_processes() {
    // start the clock driver
    int clockResult = spork("ClockDriver", clockDriver, NULL, USLOSS_MIN_STACK, 2);
    if (clockResult < 0) {
        USLOSS_Console("Failed to start clock driver\n");
        USLOSS_Halt(1);
    }
    // start the term driver
    int terminalResult = spork("TerminalDriver", terminalDriver, NULL, USLOSS_MIN_STACK, 2);
    if (terminalResult < 0) {
        USLOSS_Console("Failed to start terminal driver\n");
        USLOSS_Halt(1);
    }
    // start the disk driver
}
