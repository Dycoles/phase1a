/*
Phase 1b (phase1.c)
University of Arizona
CSC 452
02/21/2025
Authors: 
    Dylan Coles
    Jack Williams
*/
#include <stdio.h>
#include "phase1.h"
#include <string.h>
#include <assert.h>
#include <stdlib.h>

struct zapQueue {
    struct process *head;
};

struct process {
    int PID;
    int priority;
    char name[MAXNAME];
    int (*startFunc)(void*);
    void *arg;
    int stackSize;
    void *stack;

    // 0 = runnable, >0 = blocked (2 = joining, 3 = zapped)
    int status;

    // collected for void dumpProcesses(void)
    int parentPid;

    // flag is 1 if in use, 0 if unused
    int in_use;

    // check if we have already quit the process
    int quit;
    int quitStatus;
    int zapped;

    struct process *next_sibling;
    struct process *first_child;
    struct process *parent;
    struct process *next_in_queue;
    struct process *zapped_by;
    struct process *next_to_zap;
    USLOSS_Context state;
};

int currentPid = 1;

// process table
struct process process_table[MAXPROC];

// current process (set to NULL)
struct process *currentProcess = NULL;

// run queue
struct process *runQueues[6];

struct process *dequeue() {
    // Step through each queue, removing old processes, searching for the next one:
    for (int i = 1; i <= 6; i++) {
        // Search for the first valid process:
        while (runQueues[i] != NULL) {
            // Dequeue the first runnable process, skipping and removing blocked/quit ones:
            if (runQueues[i]->status == 0 && runQueues[i]->quit == 0) { // runnable
                struct process *dequeued = runQueues[i];
                runQueues[i] = runQueues[i]->next_in_queue;
                dequeued->next_in_queue = NULL;
                return dequeued;
            } else {
                runQueues[i] = runQueues[i]->next_in_queue;
            }
        }
    }

    // No enqueued unblocked processes. Shouldn't be able to get here, return NULL:
    return NULL;
}

int enqueue(struct process *toEnqueue) {
    if (toEnqueue == NULL) {
        return 0;
    }
    // If process is invalid, return false:
    if (toEnqueue->status > 0) {
        return 0;
    }
    
    // Add to the end of the correct priority queue:
    int priority = toEnqueue->priority;
    if (runQueues[priority] == NULL) {
        runQueues[priority] = toEnqueue;
        return 1;
    } else {
        struct process *queueTail;
        for (queueTail = runQueues[priority]; queueTail->next_in_queue != NULL; queueTail = queueTail->next_in_queue) {
            // Deliberately empty, just does the above increment
        }
        queueTail->next_in_queue = toEnqueue;
        return 1;
    }
}

int disableInterrupts() {
    // store psr for later
    int old_psr = USLOSS_PsrGet();

    // ensure we are in kernel mode
    if (USLOSS_PsrSet(old_psr & ~USLOSS_PSR_CURRENT_INT) != 0) {
        USLOSS_Console("ERROR: cannot disable interrupts in user mode\n");
        USLOSS_Halt(1);
    }

    return old_psr;
}

void enableInterrupts() {
    // ensure we are in kernel mode
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: cannot enable interrupts in user mode\n");
    }

    // restore interrupts; x used to keep compiler happy
    int x = USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT); x++;
}

void restoreInterrupts(int old_psr) {
    // restore interrupts; x used to keep compiler happy
    int x = USLOSS_PsrSet(old_psr); x++;
}

void wrapper(void) {
    int result;

    int (*func)(void *) = currentProcess->startFunc;
    void *arg = currentProcess->arg;

    // enable interrupts before calling func
    enableInterrupts();

    result = func(arg);

    // if function returns, call quit
    quit(result);
}

int testcaseWrapper(void *) {
    // Call testcase_main() and halt once it returns:
    int retVal = testcase_main();
    if (retVal == 0) {   // terminated normally
        USLOSS_Halt(0);
    } else {    // errors
        USLOSS_Console("Some error was detected by the testcase.\n");
        USLOSS_Halt(retVal);
    }

    // Should never get here, just making the compiler happy:
    return 1;
}

int launchPhases() {
    phase2_start_service_processes();
    phase3_start_service_processes();
    phase4_start_service_processes();
    phase5_start_service_processes();

    int result = spork("testcase_main", (*testcaseWrapper), NULL, USLOSS_MIN_STACK, 3);
    if (result < 0) {
        // print errors here then halt
        USLOSS_Console("Errors in spork returned < 0\n");
        USLOSS_Halt(1);
    }
    
    dispatcher();
    return 255;
}

void phase1_init(void) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call phase1_init while in user mode!\n");
        USLOSS_Halt(1);
    }
    
    int old_psr = disableInterrupts();

    // initialize all processes in process table
    for (int i = 0; i < MAXPROC; i++) {
        memset(process_table[i].name, 0, MAXNAME);

        process_table[i].first_child = NULL;
        process_table[i].next_sibling = NULL;

        process_table[i].PID = 0;
        process_table[i].arg = NULL;
        process_table[i].in_use = 0;
        process_table[i].parentPid = 0;
        process_table[i].priority = -1;

        process_table[i].quit = 0;
        process_table[i].quitStatus = 0;
        process_table[i].stack = NULL;
        process_table[i].stackSize = 0;
        process_table[i].startFunc = NULL;
        process_table[i].status = 0;
        process_table[i].zapped = 0;
    }

    // create init process
    struct process *initProcess = &process_table[1];
    initProcess -> PID = 1;
    initProcess -> priority = 6;
    initProcess -> stackSize = USLOSS_MIN_STACK;
    initProcess -> stack = malloc(initProcess -> stackSize);
    strcpy(initProcess -> name, "init");
    initProcess->in_use = 1;
    initProcess -> startFunc = launchPhases;
    currentPid = initProcess -> PID;

    USLOSS_ContextInit(&(initProcess->state), initProcess->stack, initProcess->stackSize, NULL, wrapper);
    
    enqueue(initProcess);

    currentPid++;
    
    restoreInterrupts(old_psr);
}

int spork(char *name, int (*startFunc)(void *), void *arg, int stackSize, int priority) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call spork while in user mode!\n");
        USLOSS_Halt(1);
    }
    
    int old_psr = disableInterrupts();

    // if stack size smaller than USLOSS_MIN_STACK, return -2
    if (stackSize < USLOSS_MIN_STACK) {
        restoreInterrupts(old_psr);
        return -2;
    }

    // if name or startFunc is null, priority is out of range, or name is longer than MAXNAME, return -1
    if (name == NULL || startFunc == NULL || priority < 1 || priority > 5 || strlen(name) >= MAXNAME) {
        restoreInterrupts(old_psr);
        return -1;
    }

    // find an empty slot
    int slot = currentPid % MAXPROC;
    int attempts = 1;
    while (process_table[slot].in_use) {
        currentPid++;
        slot = currentPid % MAXPROC;

        // If cycled through all slots w/ no vacancies, return error:
        if (++attempts > MAXPROC) {
            restoreInterrupts(old_psr);
            return -1;
        }
    }

    // fill the table and assign it as a child of the previous process
    struct process *thisProcess = &process_table[slot];
    strcpy(thisProcess -> name, name);
    thisProcess -> startFunc = startFunc;
    thisProcess -> PID = currentPid;
    thisProcess -> priority = priority;
    thisProcess -> arg = arg;

    // Validate stack allocation:
    thisProcess -> stackSize = stackSize;
    thisProcess -> stack = malloc(stackSize);
    if (thisProcess -> stack == NULL) {
        restoreInterrupts(old_psr);
        return -2;
    }

    thisProcess -> in_use = 1;
    thisProcess -> status = 0;
    thisProcess -> quit = 0;
    thisProcess -> quitStatus = 0;
    thisProcess -> zapped = 0;

    // set parents and ensure there is space for children
    thisProcess -> parent = currentProcess;
    thisProcess -> parentPid = currentProcess -> PID;
    thisProcess -> next_sibling = currentProcess->first_child;
    currentProcess->first_child = thisProcess;

    currentPid++;
    
    // Initialize context for process
    USLOSS_ContextInit(&(thisProcess->state), thisProcess->stack, thisProcess->stackSize, NULL, wrapper);

    // Enqueue the new child, and run it if it has priority >= its parent's:
    enqueue(thisProcess);
    if (priority < currentProcess -> priority) {
        dispatcher();
    }

    // return PID of the child process
    restoreInterrupts(old_psr);
    return thisProcess -> PID;
}

int join(int *status) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call join while in user mode!\n");
        USLOSS_Halt(1);
    }
    
    int old_psr = disableInterrupts();

    // if argument is invalid, return -3
    if (status == NULL) {
        restoreInterrupts(old_psr);
        return -3;
    }

    // Block the current process:
    currentProcess->status = 2;

    // Loop until a child is dead, calling the dispatcher after each call:
    while (1) {
        int hasChildren = 0;

        for (int i = currentPid; i >= 0; i--) {
            int slot = i % MAXPROC;

            // If child exists:
            if (process_table[slot].parentPid == currentProcess->PID && process_table[slot].in_use == 1) {
                hasChildren = 1;

                // If child is dead, return its PID:
                if (process_table[slot].quit == 1) {
                    *status = process_table[slot].quitStatus;

                    // free the stack
                    if (process_table[slot].stack != NULL) {
                        free(process_table[slot].stack);
                        process_table[slot].stack = NULL;
                    }

                    process_table[slot].in_use = 0;
                    currentProcess->status = 0;

                    restoreInterrupts(old_psr);
                    return process_table[slot].PID;
                }
            }
        }

        if (!hasChildren) {
            // Process has no children, so return -2:
            currentProcess->status = 0;
            restoreInterrupts(old_psr);
            return -2;
        }

        dispatcher();
    }
}

void quit(int status) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0 ) {
        USLOSS_Console("ERROR: Someone attempted to call quit while in user mode!\n");
        USLOSS_Halt(1);
    }
    int old_psr = disableInterrupts();
    
    // If not all children are joined, give error:
    for (struct process *child = currentProcess->first_child; child != NULL; child = child->next_sibling) {
        if (child->in_use == 1) {
            USLOSS_Console("ERROR: Process pid %d called quit() while it still had children.\n", currentProcess->PID);
            USLOSS_Halt(1);
        }
    }

    // Halt the current process (and wake the parent if joining):
    currentProcess->quit = 1;
    currentProcess->quitStatus = status;
    if (currentProcess->parent->status == 2) {
        currentProcess->parent->status = 0;
        enqueue(currentProcess->parent);
    }

    // Wake any zapping processes:
    currentProcess->zapped = 0;
    struct process *zapper = currentProcess->zapped_by;
    while (zapper != NULL) {
        zapper->status = 0;
        enqueue(zapper);

        struct process *nextZapper = zapper->next_to_zap;
        zapper->next_to_zap = NULL;
        zapper = nextZapper;
    }

    // Switch to the next process:
    dispatcher();
    
    restoreInterrupts(old_psr);
    assert(0);
}

int getpid(void) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call getPID while in user mode!\n");
        USLOSS_Halt(1);
    }
    
    // returns the PID of cur process
    return currentProcess->PID;
}

void dumpProcesses(void) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call dumpProcesses while in user mode!\n");
        USLOSS_Halt(1);
    }
    
    int old_psr = disableInterrupts();

    // Print information about each living process:
    printf("PID  PPID  NAME              PRIORITY  STATE\n");
    for (int i = 0; i < MAXPROC; i++) {
        if (process_table[i].in_use) {
            printf("%3i  %4i  %-17s %d         ",
                process_table[i].PID, process_table[i].parentPid, process_table[i].name,
                process_table[i].priority);
            
            // Determine what to print for the running state:
            if (process_table[i].PID == currentProcess->PID) {
                printf("Running\n");
            } else if (process_table[i].quit) {
                printf("Terminated(%d)\n", process_table[i].quitStatus);
            } else if (process_table[i].status == 0) {
                printf("Runnable\n");
            } else if (process_table[i].status == 2) {
                printf("Blocked(waiting for child to quit)\n");
            } else if (process_table[i].status == 3) {
                printf("Blocked(waiting for zap target to quit)\n");
            } else {
                printf("Blocked(%d)\n", 3);
            }
        }
    }

    restoreInterrupts(old_psr);
}

void dispatcher() {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0 ) {
        USLOSS_Console("ERROR: Someone attempted to call the dispatcher while in user mode!\n");
        USLOSS_Halt(1);
    }
    int old_psr = disableInterrupts();
    
    // context switch to the highest priority process and run it  
    struct process *oldProcess = currentProcess;
    enqueue(oldProcess);
    struct process *newProcess = dequeue();
    currentProcess = newProcess;

    // Ensure the process exists:
    if (newProcess == NULL) {
        USLOSS_Console("Error: Process not found!\n");
        USLOSS_Halt(1);
    }

    // Make the switch:
    if (oldProcess == NULL) {
        USLOSS_ContextSwitch(NULL, &newProcess->state);
    } else {
        USLOSS_ContextSwitch(&oldProcess->state, &newProcess->state);
    }

    restoreInterrupts(old_psr);
}

void zap(int pid) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call zap while in user mode!\n");
        USLOSS_Halt(1);
    }
    
    struct process *toZap = &process_table[pid % MAXPROC];

    // Can't zap init:
    if (pid == 1) {
        USLOSS_Console("ERROR: Attempt to zap() init.\n");
        USLOSS_Halt(1);
    }
    // Current process can't zap itself:
    if (pid == currentProcess -> PID) {
        USLOSS_Console("ERROR: Attempt to zap() itself.\n");
        USLOSS_Halt(1);
    }
    // Can't zap a process that has quit:
    if (toZap->quit == 1) {
        USLOSS_Console("ERROR: Attempt to zap() a process that is already in the process of dying.\n");
        USLOSS_Halt(1);
    }
    // If process in the correct slot is wrong (and thus the process to zap does not exist):
    if (toZap->PID != pid) {
        USLOSS_Console("ERROR: Attempt to zap() a non-existent process.\n");
        USLOSS_Halt(1);
    }

    // currentProcess zaps toZap:
    toZap->zapped = 1;
    currentProcess->next_to_zap = toZap->zapped_by;
    toZap->zapped_by = currentProcess;

    // Block the current process while waiting for toZap to quit:
    currentProcess->status = 3;

    // Once toZap completes, currentProcess is unblocked and runs again
    while (toZap->zapped) {
        dispatcher();
    }
    currentProcess->status = 0;
}

void blockMe() {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call blockMe while in user mode!\n");
        USLOSS_Halt(1);
    }
    
    if (currentProcess -> zapped == 1) {
        quit(currentProcess -> status);
    }

    // block the current process in the process table (>=1 is blocked)
    currentProcess -> status = 1;

    // call the dispatcher to change the current process
    dispatcher();
}

int unblockProc(int pid) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call unblockProc while in user mode!\n");
        USLOSS_Halt(1);
    }
    
    // If the process was not blocked or does not exist, return -2:
    if (process_table[pid % MAXPROC].PID == 0) {
        USLOSS_Console("The process does not exist\n");
        return -2;
    }
    if (process_table[pid % MAXPROC].status == 0) {
        USLOSS_Console("The process was not blocked\n");
        return -2;
    }

    // unblock the process
    process_table[pid % MAXPROC].status = 0;
    enqueue(&process_table[pid % MAXPROC]);

    // call the dispatcher
    dispatcher();

    return 0;
}
