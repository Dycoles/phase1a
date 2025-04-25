#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <phase3_usermode.h>
#include <phase3_kernelInterfaces.h>
#include <phase4_usermode.h>

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

int userModeMBoxID;
int x;

typedef struct {
    int wakeTime;
    int mboxID;
} SleepProc;

typedef struct {
    int size;
    SleepProc arr[MAXPROC];
} MinHeap;

int readMbox[4];
int writeMbox[4];
int writeIndex[4];
char writeBuf[4][MAXLINE];
int writeLen[4];

// create a list of sleeping processes
MinHeap sleepHeap;

// keep track of clock interrupts
int clockInterrupts = 0;

// swap elements in the heap
void swap(SleepProc* a, SleepProc* b) {
    SleepProc temp = *a;
    *a = *b;
    *b = temp;
}

// helper function for insert
void insertHelper(MinHeap* h, int index) {
    if (index == 0) {
        return;
    }
    int parent = (index - 1)/2;
    // swap when child is smaller, then parent element
    if (h->arr[parent].wakeTime > h->arr[index].wakeTime) {
        swap(&h->arr[parent], &h->arr[index]);
        insertHelper(h, parent);
    }
}

// insert elements into the heap
void insert(MinHeap* h, SleepProc proc) {
    if (h->size < MAXPROC) {
        h->arr[h->size] = proc;
        insertHelper(h, h->size);
        h->size++;
    }
}

// heapify to sort elements in min heap order
void heapify(MinHeap *h, int index) {
    int left = index * 2 + 1;
    int right = index * 2 + 2;
    int min = index;
    // check if left or right child is at correct index
    if (left >= h->size || left < 0) {
        left = -1;
    }
    if (right >= h->size || right < 0) {
        right = -1;
    }
    // store left or right as min if any smaller than parent
    if (left != -1 && h->arr[left].wakeTime < h->arr[index].wakeTime) {
        min = left;
    }
    if (right != -1 && h->arr[right].wakeTime < h->arr[min].wakeTime) {
        min = right;
    }
    // swap the nodes
    if (min != index) {
        swap(&h->arr[min], &h->arr[index]);
        heapify(h, min);
    }
}

// remove smallest item and heapify 
void removeMin(MinHeap* h) {
    if (h->size == 0) {
        USLOSS_Console("Cannot remove from empty heap\n");
        USLOSS_Halt(1);
    }
    // replace root with last element
    h->arr[0] = h->arr[h->size - 1];
    h->size--;
    heapify(h, 0);
}

/*
 * The lock() function gains the lock for user mode operations.
 */
void lock() {
    // Acquire the user mode lock:
    MboxRecv(userModeMBoxID, NULL, 0);
}

/*
 * The unlock() function releases the lock for user mode operations.
 */
void unlock() {
    // Release the user mode lock:
    MboxSend(userModeMBoxID, NULL, 0);
}

void sleepSyscall(USLOSS_Sysargs *args) {
    int seconds = (int)(long)args->arg1;
    if (seconds < 0) {
        // invalid input, return -1
        args->arg4 = (void *) -1;
        return;
    }
    lock();
    // successfull input, perform operation
    int mbox = MboxCreate(1, 0);
    int wakeTime = clockInterrupts + seconds * 10;
    if (sleepHeap.size < MAXPROC) {
        SleepProc proc;
        proc.wakeTime = wakeTime;
        proc.mboxID = mbox;
        insert(&sleepHeap, proc);
    } else {
        // too many sleeping processes, return -1
        args->arg4 = (void *) -1;
        unlock();
        return;
    }
    unlock();
    // block until wake up
    MboxRecv(mbox, NULL, 0);
    // release after wake up
    MboxRelease(mbox);
    // valid operation, return 0;
    args->arg4 = (void *) 0;
}


void termReadSyscall(USLOSS_Sysargs *args) {
    USLOSS_Console("In Read\n");
    char *readBuffer = (char *) args->arg1;
    // arg2: length of the buffer
    int len = (int)(long) args->arg2;
    // arg3: which terminal to read
    int unit = (int)(long) args->arg3;

    lock();
    args->arg4 = 0;

    int charsInput;
    if (len < MAXLINE) {
        charsInput = len;
    } else {
        charsInput = MAXLINE;
    }

    // Check for errors:
    if (charsInput <= 0 || (unit < 0 || unit > 3)) {
        args->arg4 = -1;
        unlock();
        return;
    }
    // enable terminal interrupts
    int cr_val = 0x0; // this turns on the ’send char’ bit (USLOSS spec page 9)
    cr_val |= 0x2; // recv int enable
    cr_val |= 0x4; // xmit int enable
    cr_val |= ('\0'<<8); // the character to send
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *) ((long) cr_val));

    int i = 0;
    for (i; i < charsInput; i++) {
        USLOSS_Console("Characters read: %d\n", i);
        if (MboxRecv(readMbox[unit], &readBuffer[i], sizeof(char)) < 0) {
            args->arg4 = (void *) -1;
            return;
        }
        if (readBuffer[i] == '\n') {
            i++;
            break;
        }
    }
    if (i < len) {
        readBuffer[i] = '\0';
    } else {
        readBuffer[len-1] = '\0';
    }
    args->arg2 = (void *)(long) i;
    args->arg4 = (void *) 0;
    
    unlock();
}

void termWriteSyscall(USLOSS_Sysargs *args) {
    USLOSS_Console("In Write\n");
    // System Call Arguments:
    // arg1: buffer pointer
    char *buf = (char *) args->arg1;
    // arg2: length of the buffer
    int len = (int)(long) args->arg2;
    // arg3: which terminal to read
    int unit = (int)(long) args->arg3;
    lock();
    // error checking
    int charsInput;
    if (len < MAXLINE) {
        charsInput = len;
    } else {
        charsInput = MAXLINE;
    }
    if (charsInput <= 0 || (unit < 0 || unit > 3)) {
        args->arg4 = -1;
        unlock();
        return;
    }
    strncpy(writeBuf[unit], buf, charsInput);
    writeLen[unit] = charsInput;
    writeIndex[unit] = 0;
    int cr_val = 0x0; // this turns on the ’send char’ bit (USLOSS spec page 9)
    cr_val |= 0x2; // recv int enable
    cr_val |= 0x4; // xmit int enable
    cr_val |= (writeBuf[unit][0] << 8); // the character to send
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)cr_val);
    unlock();

    // block until write is complete
    MboxRecv(writeMbox[unit], NULL, 0);
    args->arg4 = (void *)0;
}

// To implement in phase 4b
void diskSizeSyscall(USLOSS_Sysargs *args) {
    lock();
    if (x) {
        args->arg4 = (void *) -1;
    } else {
        // successful input, perform operation
        args->arg4 = (void *) 0;
    }
    unlock();
}

// To implement in phase 4b
void diskReadSyscall(USLOSS_Sysargs *args) {
    lock();
    if (x) {
        args->arg4 = (void *) -1;
    } else {
        // successful input, perform operation
        args->arg4 = (void *) 0;
    }
    unlock();
}

// To implement in phase 4b
void diskWriteSyscall(USLOSS_Sysargs *args) {
    lock();
    if (x) {
        args->arg4 = (void *) -1;
    } else {
        // successful input, perform operation
        args->arg4 = (void *) 0;
    }
    unlock();
}

int clockDriver(void *arg) {
    int status;
    // use infinite loop which increments counter each time interrupt is received
    while (1) {
        waitDevice(USLOSS_CLOCK_DEV, 0, &status);
        clockInterrupts++;
        lock();
        // repeatedly loop through sleep queue to check if it is time to wake up the process
        while (sleepHeap.size > 0 && sleepHeap.arr[0].wakeTime <= clockInterrupts) {
            MboxSend(sleepHeap.arr[0].mboxID, NULL, 0);
            removeMin(&sleepHeap);
        }
        unlock();
    }
    return 0;
}

void handle_one_terminal_interrupt(int unit, int status) {
    USLOSS_Console("Top of Handle One: %c, unit %d\n", USLOSS_TERM_STAT_CHAR(status), unit);
    // if recv is ready
    if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_READY) {
        // Read character from status
        char c = USLOSS_TERM_STAT_CHAR(status);
        // place character in buffer and wake up waiting process
        USLOSS_Console("Sending character\n");
        MboxSend(readMbox[unit], &c, sizeof(char));
    }
    // if xmit is ready
    if (USLOSS_TERM_STAT_XMIT(status) == USLOSS_DEV_READY) {
        //USLOSS_Console("Lower If Handle One\n");
        writeIndex[unit]++;
        // if a previous send has now completed a "write" op...
        if (writeIndex[unit] < writeLen[unit]) {
            // wake up a process
            int cr_val = 0;
            cr_val = 0x1; // this turns on the ’send char’ bit (USLOSS spec page 9)
            cr_val |= 0x2; // recv int enable
            cr_val |= 0x4; // xmit int enable
            cr_val |= (writeBuf[unit][writeIndex[unit]] << 8); // the character to send
            USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)cr_val);
        } else {
            // if some buffer is waiting to be flushed
            // send a single character
            MboxSend(writeMbox[unit], NULL, 0);
        }
    }
    //USLOSS_Console("End Of Handle One: %d\n\n", unit);
}

int terminalDriver(void *arg) {
    int status = 0;
    int unit = (int)(long)arg;
    while (1) {
        //USLOSS_Console("In Terminal Driver 1: %d. Unit: %d\n", getpid(), unit);
        waitDevice(USLOSS_TERM_DEV, unit, &status);
        //USLOSS_Console("In Terminal Driver 2: %d\n", getpid());
        handle_one_terminal_interrupt(unit, status);
        //USLOSS_Console("In Terminal Driver 3: %d\n", getpid());
    }
}

int diskDriver(void *arg) {
    int status;
    waitDevice(USLOSS_DISK_DEV, 0, &status);
    return 0;
}

void phase4_init() {
    int cr_val = 0x0; // this turns on the ’send char’ bit (USLOSS spec page 9)
    cr_val |= 0x2; // recv int enable
    cr_val |= 0x4; // xmit int enable
    cr_val |= ('\0'<<8); // the character to send
    USLOSS_Console("%x\n", cr_val);
    if (USLOSS_DeviceOutput(USLOSS_TERM_DEV, 0, (void*)(long)cr_val) != USLOSS_DEV_OK)USLOSS_Console("Bad\n");
    if (USLOSS_DeviceOutput(USLOSS_TERM_DEV, 1, (void*)(long)cr_val) != USLOSS_DEV_OK)USLOSS_Console("Bad\n");
    if (USLOSS_DeviceOutput(USLOSS_TERM_DEV, 2, (void*)(long)cr_val) != USLOSS_DEV_OK)USLOSS_Console("Bad\n");
    if (USLOSS_DeviceOutput(USLOSS_TERM_DEV, 3, (void*)(long)cr_val) != USLOSS_DEV_OK)USLOSS_Console("Bad\n");
    
    USLOSS_Console("%x\n", cr_val);

    // phase4a syscalls
    systemCallVec[SYS_SLEEP] = (void (*)(USLOSS_Sysargs *))sleepSyscall;
    systemCallVec[SYS_TERMREAD] = (void (*)(USLOSS_Sysargs *))termReadSyscall;
    // systemCallVec[SYS_TERMWRITE] = (void (*)(USLOSS_Sysargs *))termWriteSyscall;
    // phase4b syscalls
    systemCallVec[SYS_DISKSIZE] = (void (*)(USLOSS_Sysargs *))diskSizeSyscall;
    systemCallVec[SYS_DISKREAD] = (void (*)(USLOSS_Sysargs *))diskReadSyscall;
    systemCallVec[SYS_DISKWRITE] = (void (*)(USLOSS_Sysargs *))diskWriteSyscall;
    
    // Create the mailboxes used in this program:
    userModeMBoxID = MboxCreate(1, 0);
    for (int i = 0; i < 4; i++) {
        readMbox[i] = MboxCreate(MAXLINE, sizeof(char));
        writeMbox[i] = MboxCreate(1, 0);
        writeIndex[i] = 0;
    }
    // make user mode lock available to start with
    MboxSend(userModeMBoxID, NULL, 0);  
    // initialize heap size to 0
    sleepHeap.size = 0;
}

void phase4_start_service_processes() {
    // start the clock driver
    int clockResult = spork("ClockDriver", clockDriver, NULL, USLOSS_MIN_STACK, 2);
    if (clockResult < 0) {
        USLOSS_Console("Failed to start clock driver\n");
        USLOSS_Halt(1);
    }
    // start the term driver for the 4 terminals
    for (int i = 0; i < 4; i++) {
        char name[16];
        sprintf(name, "TerminalDriver%d", i);
        int terminalResult = spork(name, terminalDriver, i, USLOSS_MIN_STACK, 2);
        if (terminalResult < 0) {
            USLOSS_Console("Failed to start terminal driver\n");
            USLOSS_Halt(1);
        }
    }

    // start the disk driver -> phase4b code
}
