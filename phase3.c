#include <phase3_kernelInterfaces.h>
#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase3_usermode.c>

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

int phase3_init() {

}

int phase3_start_service_processes() {

}

int Spawn(char *name, int (*func)(void *), void *arg, 
int stack_size, int priority, int *pid) {
    int result = trampoline();
}

int Wait(int *pid, int *status) {

}

void Terminate(int status) {

}

int SemCreate(int value, int *semaphore) {

}

int SemP(int semaphore) {

}

int SemV(int semaphore) {

}

int kernSemCreate(int value, int *semaphore) {
    
}

int kernSemP(int semaphore) {

}

int kernSemV(int semaphore) {

}

void GetTimeofDay(int *tod) {

}

void GetPID(int *pid) {

}

void DumpProcesses() {

}
