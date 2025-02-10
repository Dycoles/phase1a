#include <stdio.h>
// #include <phase1.h>

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
// USLOSS_Halt(0)
}


int spork(char *name, int (*startFunc)(void), void *arg, int stackSize, int priority) {
    return 1;
}

int join(int *status) {

}

void quit_phase_1a(int status, int switchToPid) {

}

int getpid(void) {

}

void dumpProcesses(void) {

}

void TEMP_switchTo(int newpid) {
// USLOSS_ContextSwitch
}

//void zap(int pid)

//void blockMe()

//int unblockProc(int pid)
