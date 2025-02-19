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
    if (USLOSS_PsrSet(USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_INT) != 0) {
        USLOSS_Console("ERROR: USLOSS_PsrSet failed in disableInterrupts\n");
        USLOSS_Halt(1);
    }
    return old_psr;
}

void restoreInterrupts(int old_psr) {
    int x = USLOSS_PsrSet(old_psr); x++;
}

void wrapper(void) {
    int result;
    int (*func)(void *) = currentProcess->startFunc;
    void *arg = currentProcess->arg;

    result = func(arg);
    quit_phase_1a(result, 1);
}

int testcaseWrapper(void *) {
    int old_psr = disableInterrupts();
    if (testcase_main() == 0){
        USLOSS_Console("Phase 1A TEMPORARY HACK: testcase_main() returned, simulation will now halt.\n");
        USLOSS_Halt(0);
    }
    restoreInterrupts(old_psr);
    return 1;
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

        process_table[i].PID = -1;
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
    struct process *initProcess = &process_table[0];
    initProcess -> PID = 1;
    initProcess -> priority = 6;
    initProcess -> stackSize = USLOSS_MIN_STACK;
    initProcess -> stack = malloc(initProcess -> stackSize);
    strcpy(initProcess -> name, "init");
    initProcess->in_use = 1;
    currentProcess = initProcess;
    currentPid = initProcess -> PID;
    USLOSS_ContextInit(&(initProcess->state), initProcess->stack, initProcess->stackSize, NULL, wrapper);
    USLOSS_Console("Phase 1A TEMPORARY HACK: init() manually switching to PID 1.\n");
    TEMP_switchTo(currentPid);
    currentPid++;;

    phase2_start_service_processes();
    phase3_start_service_processes();
    phase4_start_service_processes();
    phase5_start_service_processes();

    // start testcase_main, should have priority of 3
    
    result = spork("testcase_main", (*testcaseWrapper), NULL, USLOSS_MIN_STACK, 3);
    if (result < 0) {
        // print errors here then halt
        USLOSS_Halt(1);
    }
    USLOSS_Console("Phase 1A TEMPORARY HACK: init() manually switching to testcase_main() after using spork() to create it.\n");
    TEMP_switchTo(result);
    // clean up with join
    int status;
    while (1) {
        if (join(&status) == -2) {
            // print out an error message here
            // call USLOSS_halt
            USLOSS_Halt(1);
            restoreInterrupts(old_psr);
            return; // unsure if this is how to quit
        }
    }
    restoreInterrupts(old_psr);
}


int spork(char *name, int (*startFunc)(void *), void *arg, int stackSize, int priority) {
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

    // fill the table and assign it as a child of the previous process
    strcpy(process_table[slot].name, name);
    process_table[slot].startFunc = startFunc;
    process_table[slot].PID = currentPid;
    process_table[slot].priority = priority;
    process_table[slot].stackSize = stackSize;
    process_table[slot].stack = malloc(stackSize);
    process_table[slot].in_use = 1;
    process_table[slot].status = 0;

    // set arg
    if (arg == NULL) {
        process_table[slot].arg = NULL;
    } else {
        process_table[slot].arg = arg;
    }

    // invalid allocation of stack
    if (process_table[slot].stack == NULL) {
        restoreInterrupts(old_psr);
        return -2;
    }
    currentPid++;

    process_table[slot].parentPid = currentProcess -> PID;
    // set parents and ensure there is space for children 
    process_table[slot].next_sibling = currentProcess->first_child;
    currentProcess->first_child = &process_table[slot];
    // Unsure if the above is better than below or not, it doesn't seem to affect things
    /*if (currentProcess -> first_child == NULL) {
        currentProcess -> first_child = &process_table[slot];
    } else {
        // search for empty sibling if children already exist
        struct process *child = currentProcess->first_child;
        while (child -> next_sibling != NULL) {
            child = child -> next_sibling;
        }
        child -> next_sibling = &process_table[slot];
    }*/
    
    // Initialize context for process
    USLOSS_ContextInit(&(process_table[slot].state), process_table[slot].stack, process_table[slot].stackSize, NULL, wrapper);
    // return PID of the child process
    restoreInterrupts(old_psr);
    return process_table[slot].PID;
}

int join(int *status) {
    int old_psr = disableInterrupts();
    // if argument is invalid, return -3
    if (status == NULL) {
        restoreInterrupts(old_psr);
        return -3;
    }
    // Altered for loop makes it run slower, but helps match output exactly
    for (int i = MAXPROC; i >= 0; i--) {
        // if child exists, return PID of the child
        if (process_table[i].parentPid == currentProcess->PID && process_table[i].quit == 1 && process_table[i].in_use == 1) {
            *status = process_table[i].quitStatus;
            process_table[i].in_use = 0;
            //free(process_table[i].stack);
            restoreInterrupts(old_psr);
            return process_table[i].PID;
        }
    }
    // return -2 if the process does not have any children
    restoreInterrupts(old_psr);
    return -2;
}

void quit_phase_1a(int status, int switchToPid) {
    //USLOSS_Console("quitting\n");
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0 ) {
        USLOSS_Console("ERROR: Someone attempted to call spork while in user mode!\n");
        // USLOSS_Halt(1);
    }
    int old_psr = disableInterrupts();
    //USLOSS_Console("Interrupts disabled quit\n");
    // If not all children are joined, give error:
    for (struct process *child = currentProcess->first_child; child != NULL; child = child->next_sibling) {
        if (child->quit != 1) {
            USLOSS_Console("Error: Process quit before joining with all children.");
            // USLOSS_Halt(1);
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
                printf("Terminated(%d)\n", process_table[i].PID);
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
     
     struct process *oldProcess = currentProcess;
     currentProcess = newProcess;
     USLOSS_ContextSwitch(&oldProcess->state, &newProcess->state);
     restoreInterrupts(old_psr);
}

//void zap(int pid)

//void blockMe()

//int unblockProc(int pid)
