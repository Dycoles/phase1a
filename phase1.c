#include <stdio.h>
#include <phase1.h>

struct process {
    int PID;
    int priority;
    char name[MAXNAME + 1];
};

int j = 0;

struct process process_table[MAXPROC];
    
int main() {
    return 1;
}

void phase1_init(void) {
    process_table[0].PID = 1;
    process_table[0].priority = 6;
    strcpy(process_table[0].name, "init");
    
    spork("testcase_main", (*testcase_main), NULL, sizeof(process_table), 3);

    int curProcess = 1;
    int status;
    while (1) {
        if (join(&status) == -2) {
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
    int slot = getpid() % MAXPROC;
    process_table[slot].PID = 1;
    process_table[slot].priority = priority;
    strcpy(process_table[0].name, name);
    // creates new processs as a child of the current process, assigns it a pid, and stores it in the process table
    // return error if the table is full
    return 1;
}

int join(int *status) {
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
}

void dumpProcesses(void) {
// prints debug info about process table
}

void TEMP_switchTo(int newpid) {
// USLOSS_ContextSwitch
}

//void zap(int pid)

//void blockMe()

//int unblockProc(int pid)
