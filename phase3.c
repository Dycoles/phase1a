/*
 * Phase 3 (phase3.c)
 * University of Arizona
 * CSC 452
 * 04/04/2025
 * Authors: 
 *     Dylan Coles
 *     Jack Williams
*/

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

/*
 * The Semaphore struct tracks the value of a semaphore, along with its
 * queue of blocked processes if any try to P() when the value is 0.
 */
typedef struct {
    int value;
    int numBlockedProcs;
    int blockedMbox[MAXMBOX];
    int front;
    int back;
} Semaphore;

/*
 * The trampolineFunc struct wraps a user-mode function in a trampoline,
 * allowing it to be called in user mode as it should.
 */
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

/*
 * The userModeTrampoline() function accepts a trampolineFunc pointer and
 * runs its function in user mode. A pointer to this function is passed
 * to spork() to create a process that runs a user-mode function.
 */
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

/*
 * The spawnSyscall() function represents the spawn syscall called in user mode.
 * It calls spork on a user-mode process in user mode, creating a new process.
 */
void spawnSyscall(USLOSS_Sysargs *args) {
    lock();

    // Find an available trampoline func struct:
    int thisTrampID;
    int attemptsMade = 0;
    do {
        thisTrampID = nextTrampID++;

        // If out of trampolineFuncs, return error:
        attemptsMade++;
        if (attemptsMade > MAXPROC) {
            USLOSS_Console("ERROR: No available slots for trampoline function. Should never see this.");
            args->arg1 = NULL;
            args->arg4 = (void *) -1;
            unlock();
            return;
        }
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

/*
 * The waitSyscall() function represents the wait syscall called in user mode.
 * It calls join on a user-mode process in user mode, waiting until it terminates.
 */
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

/*
 * The terminateSyscall() function represents the terminate syscall called in user mode.
 * It calls quit on a user-mode process in user mode once all its children have joined.
 */
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

/*
 * The semCreateSyscall() function represents the semCreate syscall called in user mode.
 * It creates a new semaphore with a default value, returning its ID.
 */
void semCreateSyscall(USLOSS_Sysargs *args) {
    int semID;

    // Call the kernel-mode semCreate:
    int result = kernSemCreate((int)(long)args->arg1, &semID);

    // Set its return values:
    if (result == 0) {
        args->arg1 = (void *)(long)semID;
        args->arg4 = 0;
    } else {
        args->arg4 = (void *)-1;
    }
}

/*
 * The semPSyscall() function represents the semP syscall called in user mode.
 * It decrements the accepted semaphore ID; if its value is 0, it blocks until
 * the value is non-zero.
 */
void semPSyscall(USLOSS_Sysargs *args) {
    // Call the kernel-mode semP:
    int result = kernSemP((int)(long)args->arg1);

    // Set its return values:
    if (result == 0) {
        args->arg4 = 0;
    } else {
        args->arg4 = (void *)-1;
    }
}

/*
 * The semVSyscall() function represents the semV syscall called in user mode.
 * It increments the accepted semaphore ID, unblocking a waiting process
 * if there are any.
 */
void semVSyscall(USLOSS_Sysargs *args) {
    // Call the kernel-mode semV:
    int result = kernSemV((int)(long)args->arg1);

    // Set its return values:
    if (result == 0) {
        args->arg4 = 0;
    } else {
        args->arg4 = (void *)-1;
    }
}

/*
 * The kernSemCreate() function represents a kernel-mode version of semCreate,
 * which creates a new semaphore and returns its ID.
 */
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

/*
 * The kernSemP() function represents a kernel-mode version of semP,
 * which decrements a semaphore.
 */
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

        // Block until this semaphore gets V()ed, then release the new mailbox:
        unlock(); 
        MboxSend(mbox, NULL, 0);
        MboxRelease(mbox); 
        lock(); 
    }
    // Decrement the semaphore value:
    semaphoreList[semaphore].value--;

    unlock();
    return 0;
}

/*
 * The kernSemV() function represents a kernel-mode version of semV,
 * which increments a semaphore.
 */
int kernSemV(int semaphore) {
    lock();

    // If invalid input, return error:
    if (semaphore < 0 || semaphore >= curSem) {
        unlock();
        return -1;
    }

    // Increment the semaphore value:
    semaphoreList[semaphore].value++;

    // If any P()s are blocked, unblock one:
    if (semaphoreList[semaphore].numBlockedProcs > 0) {
        // Get the next semaphore and increment it:
        int front = semaphoreList[semaphore].front;

        // Get the mailbox:
        int mbox = semaphoreList[semaphore].blockedMbox[front];
        semaphoreList[semaphore].front = (front + 1) % MAXMBOX;
        semaphoreList[semaphore].numBlockedProcs--;

        // Wake up the blocked process:
        unlock();
        MboxRecv(mbox, NULL, 0);
        lock();
    }

    unlock();
    return 0;
}

/*
 * The getTimeOfDaySyscall() function represents the getTimeOfDay syscall called in user mode.
 * It returns the current time.
 */
void getTimeOfDaySyscall(USLOSS_Sysargs *args) {
    // Return the current time of day:
    args->arg1 = (void *)(long)currentTime();
}

/*
 * The getPidSyscall() function represents the getPid syscall called in user mode.
 * It returns the PID of the currently running process.
 */
void getPidSyscall(USLOSS_Sysargs *args) {
    // Return the current process' PID:
    args->arg1 = (void *)(long)getpid();
}

/*
 * The dumpProcessesyscall() function represents the dumpProcesses syscall called in user mode.
 * It prints basic information about every active process.
 */
void dumpProcessesSyscall() {
    // Print process information:
    dumpProcesses();
}

/*
 * The phase3_init() function initializes data for the phase3 file.
 */
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

/*
 * The phase3_start_service_processes() function allows any phase-specific start
 * services to be initialized. Currently left blank.
 */
void phase3_start_service_processes() {
    // Deliberately left blank.
}
