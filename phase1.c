#include <stdio.h>
#include <phase1.h>
#include <string.h>
#include <usloss.h>
// temp variables to prevent errors
USLOSS_MIN_STACK = 50;

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

    // USLOSS_Context context;
};

int currentPid = 1;
struct process process_table[MAXPROC];

void phase1_init(void) {
    // result of spork operation
    int result;
    // initialize all processes in process table
    memset(process_table, 0, sizeof(process_table));

    process_table[0].PID = currentPid;
    process_table[0].priority = 6;
    process_table[0].in_use = 1;
    strcpy(process_table[0].name, "init");

    currentPid++;
    
    result = spork("testcase_main", (*testcase_main), NULL, sizeof(process_table), 3);
    if (result < 0) {
        // print errors here then halt
        USLOSS_Halt(1);
    }
    // context swtich to init process
    TEMP_switchTo(result);

    int curProcess = 1;
    int status;
    while (1) {
        if (join(&status) == -2) {
            // print out an error message here
            // call USLOSS_halt
            USLOSS_halt(1);
            return; // unsure if this is how to quit
        }
        curProcess = (curProcess+1)%MAXPROC;
    }
// should create testcase_main
// set up process table
// once testcase_main created, call ULOSS_ContextSwitch()
// USLOSS_Halt(0) if testcase_main ever returns
// if any other function returns, print an error and terminiate
}


int spork(char *name, int (*startFunc)(void), void *arg, int stackSize, int priority) {
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
    // if no empty slots in the process table return -1; -> This part may be incorrect. May need to change how we do our struct/queue
    int slot = getpid() % MAXPROC;
    if (process_table[slot].in_use) {
        return -1;
    }

    // fill the table and assign it as a child of the previous process -> This part is probably incorrect, may need to change how we do our struct/queue
    strcpy(process_table[0].name, name);
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
    
    // USLOSS_ContextInit(USLOSS_Context *context, void *stack, int stackSize, USLOSS_PTE *pageTable, void(*func)(void)) -> syntax for context init

    // set parents and ensure there is space for children (code here) 

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
        if (process_table[i].parentPid == currentPid && process_table[i].quit == 1) {
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
// if error Usloss halt (works like exit in UNIX)
// ends the currrent process but keeps its entry in the process table until the parent calls join
// if parent waiting, wakes up
}

int getpid(void) {
    // returns the PID of cur process
    return currentPid;
}

void dumpProcesses(void) {
    // prints debug info about process table -> should be easiest function, just need to access USLOSS console and print info
    int x;
    for (int i = 0; i < MAXPROC; i++) {
        printf("Name: %s", process_table[i].name, "\n");
        printf("PID: %d", process_table[i].PID, "\n");
        printf("Parent PID: %d", process_table[i].parentPid, "\n");
        printf("Priority: %d", process_table[i].priority, "\n");
        if (process_table[i].status == 0) {
            printf("Running! \n");
        } else {
            printf("Blocked! \n");
        }
    }
}

void TEMP_switchTo(int newpid) {
    // USLOSS_ContextSwitch
    int oldPID = currentPid;
    currentPid = newpid;

    // USLOSS_ContextSwitch(USLOSS_Context *old_context, USLOSS_Context *new_context) -> syntax for context swtiching in case we need it later
    // USLOSS_ContextSwitch(process_table[oldPID].context, process_table[newpid].context); -> code for context switching, commented out for now
}

//void zap(int pid)

//void blockMe()

//int unblockProc(int pid)
