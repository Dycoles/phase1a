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

    // commented out to get rid of errors for now; this is necessary code
    USLOSS_Context state;
};

int currentPid = 1;
// process table
struct process process_table[MAXPROC];
// current process (set to NULL)
struct process *currentProcess = NULL;

void wrapper(void) {
    int (*func)(void *) = currentProcess->startFunc;
    void *arg = currentProcess->arg;
    func(arg);
}

void phase1_init(void) {
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
        process_table[i].parentPid = -1;
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
    strcpy(initProcess -> name, "init");
    initProcess->in_use = 1;
    currentProcess = initProcess;
    currentPid = initProcess -> PID;
    
    currentPid++;
    USLOSS_Console("%lu\n", (unsigned long) currentProcess->startFunc);
    // ERROR HERE: USLOSS_ContextInit(&(process_table[0].state), process_table[0].stack, process_table[0].stackSize, NULL, *wrapper);

    phase2_start_service_processes();
    phase3_start_service_processes();
    phase4_start_service_processes();
    phase5_start_service_processes();

    // start testcase_main, should have priority of 3
    result = spork("testcase_main", (*testcase_main), NULL, USLOSS_MIN_STACK, 3);
    if (result < 0) {
        // print errors here then halt
        USLOSS_Halt(1);
    }

    USLOSS_ContextInit(&(process_table[1].state), process_table[1].stack, process_table[1].stackSize, NULL, *wrapper);
    USLOSS_Console("%luee\n", (unsigned long) currentProcess->startFunc);
    USLOSS_ContextSwitch(&(process_table[0].state), &(process_table[1].state));
    // clean up with join
    int status;
    while (1) {
        if (join(&status) == -2) {
            // print out an error message here
            // call USLOSS_halt
            USLOSS_Halt(1);
            return; // unsure if this is how to quit
        }
    }
// should create testcase_main
// set up process table
// once testcase_main created, call ULOSS_ContextSwitch()
// USLOSS_Halt(0) if testcase_main ever returns
// if any other function returns, print an error and terminiate
}


int spork(char *name, int (*startFunc)(void *), void *arg, int stackSize, int priority) {
    // check to ensure we are running in kernel mode (Talked about it in class for testcase 8)
    if (USLOSS_PsrGet() == 0) {
        USLOSS_Halt(1);
    }
    // if stack size smaller than USLOSS_MIN_STACK, return -2
    if (stackSize < USLOSS_MIN_STACK) {
        return -2;
    }
    // if name or startFunc is null, priority is out of range, or name is longer than MAXNAME, return -1
    if (name == NULL || startFunc == NULL || priority < 1 || priority > 5 || strlen(name) >= MAXNAME) {
        return -1;
    }
    // find an empty slot
    int slot = -1;
    for (int i = 0; i < MAXPROC; i++) {
        if (!(process_table[i].in_use)) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        // no slots available
        return -1;
    }

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
        return -2;
    }
    currentPid++;
    // Initialize context for process -> May need to write a wrapper for startFunc and arg
    USLOSS_ContextInit(&(process_table[slot].state), process_table[slot].stack, process_table[slot].stackSize, NULL, *wrapper);

    // set parents and ensure there is space for children 
    process_table[slot].parentPid = currentProcess -> PID;
    if (currentProcess -> first_child == NULL) {
        currentProcess -> first_child = &process_table[slot];
    } else {
        // search for empty sibling if children already exist
        struct process *child = currentProcess->first_child;
        while (child -> next_sibling != NULL) {
            child = child -> next_sibling;
        }
        child -> next_sibling = &process_table[slot];
    }
    // return PID of the child process
    return process_table[slot].PID;
}

int join(int *status) {
    // if argument is invalid, return -3
    if (status == NULL) {
        return -3;
    }
    for (int i = 0; i < MAXPROC; i++) {
        // if child exists, return PID of the child
        if (process_table[i].parentPid == currentProcess->PID && process_table[i].quit == 1) {
            *status = process_table[i].quitStatus;
            process_table[i].in_use = 0;
            free(process_table[i].stack);
            return process_table[i].PID;
        }
    }
    // return -2 if the process does not have any children
    return -2;

    // waits for child (works like wait in Unix)
    // process pauses until one of its children quits
    // receives child's exit status
}

void quit_phase_1a(int status, int switchToPid) {
    // If not all children are joined, give error:
    if (currentProcess->first_child != NULL) {
        USLOSS_Console("Error: Process quit before joining with all children.");
        USLOSS_Halt(1);
    }

    // Halt the current process:
    currentProcess->quit = 1;
    currentProcess->quitStatus = status;

    // Switch to the next process:
    TEMP_switchTo(switchToPid);
    
    // if error Usloss halt (works like exit in UNIX)
    // ends the currrent process but keeps its entry in the process table until the parent calls join
    // if parent waiting, wakes up
    assert(0);
}

int getpid(void) {
    // returns the PID of cur process
    return currentPid;
}

void dumpProcesses(void) {
    // prints debug info about process table -> should be easiest function, just need to access USLOSS console and print info
    for (int i = 0; i < MAXPROC; i++) {
        printf("Name: %s\n", process_table[i].name);
        printf("PID: %d\n", process_table[i].PID);
        printf("Parent PID: %d\n", process_table[i].parentPid);
        printf("Priority: %d\n", process_table[i].priority);
        if (process_table[i].status == 0) {
            printf("Running! \n");
        } else {
            printf("Blocked! \n");
        }
    }
}

void TEMP_switchTo(int newpid) {
    // USLOSS_ContextSwitch
    int oldPID = currentProcess -> PID;
    currentProcess = &process_table[newpid];

    // USLOSS_ContextSwitch(USLOSS_Context *old_context, USLOSS_Context *new_context) -> syntax for context swtiching in case we need it later
    USLOSS_ContextSwitch(&(process_table[oldPID].state), &(process_table[newpid].state));
}

//void zap(int pid)

//void blockMe()

//int unblockProc(int pid)
