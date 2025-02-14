#include <stdio.h>
#include <phase1.h>
#include <string.h>
#include <usloss.h>
// temp variables to prevent errors
USLOSS_MIN_STACK = 50;

struct process {
    int PID;
    int priority;
    char name[MAXNAME + 1];
    struct process *parent;
    struct process *next_sibling;
    struct process *first_child;
    // flag is 1 if in use, 0 if unused
    int in_use;

    // commented out to get rid of errors for now; this is necessary code

    // USLOSS_Context context;
};

int j = 0;
int currentPID = 0;

struct process process_table[MAXPROC];
    
int main() {
    return 1;
}

void phase1_init(void) {
    memset(process_table, 0, sizeof(process_table));

    process_table[0].PID = 1;
    process_table[0].priority = 6;
    process_table[0].in_use = 1;
    strcpy(process_table[0].name, "init");
    
    spork("testcase_main", (*testcase_main), NULL, sizeof(process_table), 3);

    int curProcess = 1;
    int status;
    while (1) {
        if (join(&status) == -2) {
            // print out an error message here
            // call USLOSS_halt
            USLOSS_halt(-2);
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
    process_table[slot].PID = 1;
    process_table[slot].priority = priority;
    process_table[slot].in_use = 1;
    strcpy(process_table[0].name, name);

    // USLOSS_ContextInit(USLOSS_Context *context, void *stack, int stackSize, USLOSS_PTE *pageTable, void(*func)(void)) -> syntax for context init
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
return currentPID;
}

void dumpProcesses(void) {
// prints debug info about process table
}

void TEMP_switchTo(int newpid) {
    // USLOSS_ContextSwitch
    int oldPID = currentPID;
    currentPID = newpid;

    // USLOSS_ContextSwitch(USLOSS_Context *old_context, USLOSS_Context *new_context) -> syntax for context swtiching
    // USLOSS_ContextSwitch(process_table[oldPID].context, process_table[newpid].context); -> code for context switching, commented out for now
}

//void zap(int pid)

//void blockMe()

//int unblockProc(int pid)
