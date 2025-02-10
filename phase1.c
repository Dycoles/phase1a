#include <stdio.h>
#include <phase1.h>

struct process {
    int PID;
    int priority;
    char[MAXNAME+1] name;
};

struct process[MAXPROC] processes;
    
int main() {
    return 1;
}

void phase1_init(void) {
    processes[0]->PID = 1;
    processes[0]->priority = 6;
    processes[0]->name = "init";

    spork("testcase_main", (*testcase_main), NULL, sizeof(process), 3);

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
