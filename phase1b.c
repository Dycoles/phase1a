#include <stdio.h>
#include "phase1.h"
#include <string.h>
#include <assert.h>
#include <stdlib.h>

// TODO run queues?

struct process {
    int PID;
    int priority;
    char name[MAXNAME];
    int (*startFunc)(void*);
    void *arg;
    int stackSize;
    void *stack;
    // 0 = runnable, >0 = blocked
    int status;
    // collected for void dumpProcesses(void)
    int parentPid;
    // flag is 1 if in use, 0 if unused
    int in_use;
    // check if we have already quit the process
    int quit;
    int quitStatus;
    struct process *parent;
    struct process *next_sibling;
    struct process *first_child;
    struct process *next_process;
    USLOSS_Context state;
};

int currentPid = 1;
// process table
struct process process_table[MAXPROC];
// current process (set to NULL)
struct process *currentProcess = NULL;

int disableInterrupts() {
    int old_psr = USLOSS_PsrGet();
    if (USLOSS_PsrSet(old_psr & ~USLOSS_PSR_CURRENT_INT) != 0) {
        USLOSS_Console("ERROR: cannot disable interrupts in user mode\n");
        USLOSS_Halt(1);
    }
    return old_psr;
}

void enableInterrupts() {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: cannot enable interrupts in user mode\n");
    }
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);
}

void restoreInterrupts(int old_psr) {
    int x = USLOSS_PsrSet(old_psr); x++;
}

void wrapper(void) {
    // USLOSS_Console("Process name is: %s\n", currentProcess -> name);
    int result;
    int (*func)(void *) = currentProcess->startFunc;
    void *arg = currentProcess->arg;
    // enable interrupts before calling func
    enableInterrupts();
    result = func(arg);
    // USLOSS_Console("Result is: %d\n", result);
    // USLOSS_Console("Wrapper complete, starting quit\n");
    // if function returns, call quit
    quit_phase_1a(result, 1);
}

int testcaseWrapper(void *) {
    // USLOSS_Console("Test case wrapper started\n");
    if (testcase_main() == 0){
        USLOSS_Console("Phase 1A TEMPORARY HACK: testcase_main() returned, simulation will now halt.\n");
        USLOSS_Halt(0);
    }
    // USLOSS_Console("Test case wrapper returned 1\n");
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
    // USLOSS_Console("The result is %d\n", result);
    // USLOSS_Console("Phase 1A TEMPORARY HACK: init() manually switching to testcase_main() after using spork() to create it.\n");
    TEMP_switchTo(result);
    return 255;
}

void phase1_init(void) {
    int old_psr = disableInterrupts();
    // result of spork operation
    int result;
    // initialize all processes in process table
    for (int i = 0; i < MAXPROC; i++) {
        memset(process_table[i].name, 0, MAXNAME);

        process_table[i].next_process = NULL;
        process_table[i].first_child = NULL;
        process_table[i].next_sibling = NULL;
        process_table[i].parent = NULL;

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
    thisProcess -> stackSize = stackSize;
    thisProcess -> stack = malloc(stackSize);
    thisProcess -> in_use = 1;
    thisProcess -> status = 0;

    // set arg
    if (arg == NULL) {
        thisProcess -> arg = NULL;
    } else {
        thisProcess -> arg = arg;
    }

    // invalid allocation of stack
    if (thisProcess -> stack == NULL) {
        restoreInterrupts(old_psr);
        return -2;
    }
    currentPid++;

    thisProcess -> parentPid = currentProcess -> PID;
    // set parents and ensure there is space for children 
    thisProcess -> next_sibling = currentProcess->first_child;
    currentProcess->first_child = thisProcess;
    // Initialize context for process
    USLOSS_ContextInit(&(thisProcess->state), thisProcess->stack, thisProcess->stackSize, NULL, wrapper);
    // return PID of the child process
    restoreInterrupts(old_psr);
    return thisProcess -> PID;
}

int join(int *status) {
    int old_psr = disableInterrupts();
    // if argument is invalid, return -3
    if (status == NULL) {
        restoreInterrupts(old_psr);
        return -3;
    }
    // Altered for loop makes it run slower, but helps match output exactly
    for (int i = currentPid; i >= 0; i--) {
        int slot = i % MAXPROC;
        // if child exists, return PID of the child
        if (process_table[slot].parentPid == currentProcess->PID && process_table[slot].quit == 1 && process_table[slot].in_use == 1) {
            *status = process_table[slot].quitStatus;
            process_table[slot].in_use = 0;
            //free(process_table[i].stack);
            restoreInterrupts(old_psr);
            return process_table[slot].PID;
        }
    }
    // return -2 if the process does not have any children
    restoreInterrupts(old_psr);
    return -2;
}

void quit_phase_1a(int status, int switchToPid) {
    //USLOSS_Console("quitting\n");
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0 ) {
        USLOSS_Console("ERROR: Someone attempted to call quit_phase_1a while in user mode!\n");
        USLOSS_Halt(1);
    }
    int old_psr = disableInterrupts();
    //USLOSS_Console("Interrupts disabled quit\n");
    // If not all children are joined, give error:
    for (struct process *child = currentProcess->first_child; child != NULL; child = child->next_sibling) {
        if (child->in_use == 1) {
            USLOSS_Console("ERROR: Process pid %d called quit() while it still had children.\n", currentProcess->PID);
            USLOSS_Halt(1);
        }
    }

    //USLOSS_Console("Halting process quit\n");
    // Halt the current process:
    currentProcess->quit = 1;
    currentProcess->quitStatus = status;

    //free(currentProcess->stack);
    //currentProcess->in_use = 0;

    // Switch to the next process:
    //USLOSS_Console("Temp switch quit\n");
    TEMP_switchTo(switchToPid);
    //USLOSS_Console("Restore interrupts quit\n");
    restoreInterrupts(old_psr);
    assert(0);
}

int getpid(void) {
    // returns the PID of cur process
    return currentProcess->PID;
}

void dumpProcesses(void) {
    int old_psr = disableInterrupts();
    // prints debug info about process table -> should be easiest function, just need to access USLOSS console and print info
    //printf("**************** Calling dumpProcesses() *******************\n");
    printf("PID  PPID  NAME              PRIORITY  STATE\n");
    for (int i = 0; i < MAXPROC; i++) {
        if (process_table[i].in_use) {
            printf("%3i  %4i  %-17s %d         ",
                process_table[i].PID, process_table[i].parentPid, process_table[i].name,
                process_table[i].priority);
            // Determine what to print for the running state:
            if (process_table[i].PID == currentProcess->PID) {
                printf("Running\n");
            } else if (process_table[i].quit) {    // Unsure if PID is correct here
                printf("Terminated(%d)\n", process_table[i].quitStatus);
            } else if (process_table[i].status == 0) {
                printf("Runnable\n");
            } else {
                printf("Blocked\n");
            }
            
            /*printf("Name: %s\n", process_table[i].name);
            printf("PID: %d\n", process_table[i].PID);
            printf("Parent PID: %d\n", process_table[i].parentPid);
            printf("Priority: %d\n", process_table[i].priority);
            if (process_table[i].status == 0) {
                printf("Running! \n");
            } else {
                printf("Blocked! \n");
            }*/
        }
    }
    restoreInterrupts(old_psr);
}

void TEMP_switchTo(int newpid) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0 ) {
        USLOSS_Console("ERROR: Someone attempted to call spork while in user mode!\n");
        USLOSS_Halt(1);
    }
    // USLOSS_Console("%d\n", currentProcess -> PID);
    // USLOSS_Console("%d\n", newpid);
    int old_psr = disableInterrupts();
    struct process *newProcess = NULL;
    for (int i = 0; i < MAXPROC; i++) {
        if (process_table[i].PID == newpid && process_table[i].in_use) {
            newProcess = &process_table[i];
            break;
        }
    }
    // Ensure the process exists
    if (newProcess == NULL) {
        USLOSS_Console("Error: Process %d not found!\n", newpid);
        USLOSS_Halt(1);
    }
    if (currentProcess == NULL) {
        // USLOSS_Console("cur process is null\n");
    }
    
    struct process *oldProcess = currentProcess;
    currentProcess = newProcess;
    // USLOSS_Console("%s\n", oldProcess->name);
    // USLOSS_Console("%s\n", newProcess->name);
    if (oldProcess == NULL) {
        USLOSS_ContextSwitch(NULL, &newProcess->state);
    } else {
        USLOSS_ContextSwitch(&oldProcess->state, &newProcess->state);
    }
    restoreInterrupts(old_psr);
}

//void zap(int pid)

//void blockMe()

//int unblockProc(int pid)
