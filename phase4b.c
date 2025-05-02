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

typedef struct {
    char bufs[MAXLINE+1][10];
    int bufIs[10];
    int whichBuf;
} TermBuf;

typedef struct {
    // 0 for read op, 1 for write op
    int op;
    // buffer
    char *buf;
    // starting track number
    int track;
    // number of sectors to read/write
    int sectors;
    // starting block number
    int firstBlock;
    // how many blocks we have looked at so far
    int blocks_so_far;
    // mailbox for this request
    int mboxID;
    // returned result
    int result;
} DiskRequest;

typedef struct {
    // keep track of processes requests
    DiskRequest queue[MAXPROC];
    DiskRequest *curRequest;
    // current index in queue
    int head;
    // direction for elevator scan algorithm
    int dir;
    // how many requests in queue
    int count;
    // request for Disk
    USLOSS_DeviceRequest req;
    // number of tracks in Disk
    int tracks;
    // 0 for not busy, 1 for busy
    int busy;
} DiskQueue;

// keep track of request queue for 2 disks
DiskQueue diskQ[2];
// keep track of mailboxes for diskSize
int diskSizeMbox[2];

int readMbox[4];
int writeMbox[4];
int readReadyMbox[4];
int writeIndex[4];
char writeBuf[4][MAXLINE];
int writeLen[4];
int busySems[4];

// create a list of sleeping processes
MinHeap sleepHeap;

// keep track of clock interrupts
int clockInterrupts = 0;

TermBuf termBufs[4];

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
    // USLOSS_Console("In Read\n");
    char *readBuffer = (char *) args->arg1;
    // arg2: length of the buffer
    int len = (int)(long) args->arg2;
    // arg3: which terminal to read
    int unit = (int)(long) args->arg3;

    //lock();
    args->arg4 = 0;

    kernSemP(busySems[unit]);
    //int old_cr_val = 0;
    //USLOSS_DeviceInput(USLOSS_TERM_DEV, unit, &old_cr_val);
    int cr_val = 0x0; // this turns on the ’send char’ bit (USLOSS spec page 9)
    cr_val |= 0x2; // recv int enable
    cr_val |= 0x4; // xmit int enable
    cr_val |= (writeBuf[unit][0] << 8); // the character to send
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)cr_val);

    int charsInput;
    if (len < MAXLINE) {
        charsInput = len;
    } else {
        charsInput = MAXLINE;
    }

    // Check for errors:
    if (charsInput <= 0 || (unit < 0 || unit > 3)) {
        args->arg4 = -1;
        //unlock();
        kernSemV(busySems[unit]);
        return;
    }

    MboxRecv(readReadyMbox[unit], NULL, 0);
    int i = 0;
    for (i; i < charsInput; i++) {
        //USLOSS_Console("Read one: %d, %d, %d\n", unit, i, charsInput);
        if (MboxRecv(readMbox[unit], &readBuffer[i], sizeof(char)) < 0) {
            args->arg4 = (void *) -1;
            kernSemV(busySems[unit]);
            return;
        }
        //USLOSS_Console("Characters read: %d, %d\n", i, readBuffer[i]);
        if (readBuffer[i] == '\n') {
            i++;
            break;
        }
    }
    if (i < len) {
        readBuffer[i] = '\0';
    } else {
        readBuffer[len] = '\0';
    }
    args->arg2 = (void *)(long) i;
    args->arg4 = (void *) 0;
    //USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)old_cr_val);
    kernSemV(busySems[unit]);
    
    //unlock();
}

void termWriteSyscall(USLOSS_Sysargs *args) {
    // System Call Arguments:
    // arg1: buffer pointer
    char *buf = (char *) args->arg1;
    // arg2: length of the buffer
    int len = (int)(long) args->arg2;
    // arg3: which terminal to read
    int unit = (int)(long) args->arg3;
    //USLOSS_Console("Top of Write: %d\n", unit);
    //lock();
    // error checking
    int charsInput;
    if (len < MAXLINE) {
        charsInput = len;
    } else {
        charsInput = MAXLINE;
    }
    if (charsInput <= 0 || (unit < 0 || unit > 3)) {
        args->arg4 = -1;
        //unlock();
        return;
    }
    kernSemP(busySems[unit]);
    strncpy(writeBuf[unit], buf, charsInput);
    writeLen[unit] = charsInput;
    writeIndex[unit] = 0;

    //int old_cr_val;
    //USLOSS_DeviceInput(USLOSS_TERM_DEV, unit, &old_cr_val);
        //unlock();
    //USLOSS_Console("Pre for in write: %d\n", old_cr_val);
    for (int i = 0; i < len; i++) {
        //USLOSS_Console("Write one: %d\n", unit);
        //USLOSS_Console("Top of for in write, %d: %s", unit, writeBuf[unit]);
        int thisChar;
        //USLOSS_Console("Top of for: %s", writeBuf[unit]);//%d of %d\n", i, len);
        int cr_val = 0x1; // this turns on the ’send char’ bit (USLOSS spec page 9)
        cr_val |= 0x0; // recv int enable
        cr_val |= 0x4; // xmit int enable
        cr_val |= (writeBuf[unit][i] << 8); // the character to send
        //USLOSS_Console("Pre ");
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)cr_val);
        //USLOSS_Console("Mid ");
        //USLOSS_Console("Pre recv in write: %c in %d\n", cr_val>>8, unit);
        // block until write is complete
        MboxRecv(writeMbox[unit], &thisChar, 1);
        //USLOSS_Console("\tPost recv in write: %c\n", thisChar);
        //USLOSS_Console("Post\n");
    }
    writeIndex[unit] = 0;
    //USLOSS_Console("After for\n");
    //USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)old_cr_val);
    kernSemV(busySems[unit]);

    args->arg4 = (void *)0;
    //USLOSS_Console("End of write: %x\n", old_cr_val);
}

// To implement in phase 4b
void diskSizeSyscall(USLOSS_Sysargs *args) {
    int unit = (int)(long) args->arg1;
    if (unit < 0 || unit > 1) {
        args->arg4 = (void *) -1;
        return;
    }
    // successful input, perform operation
    // set req operations and registers
    lock();
    diskQ[unit].req.opr = USLOSS_DISK_TRACKS;
    diskQ[unit].req.reg1 = &diskQ[unit].tracks;
    diskQ[unit].req.reg2 = NULL;
    // disk is now busy
    diskQ[unit].busy = 1;
    // get device output
    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &diskQ[unit].req);
    unlock();

    int trackNum;
    // block until driver gives us track size
    MboxRecv(diskSizeMbox[unit], &trackNum, sizeof(trackNum));
    // return to user
    args->arg1 = (void*)(long)512;
    args->arg2 = (void*)(long)16;
    args->arg3 = (void*)(long)trackNum;
    args->arg4 = (void*)(long) 0;
}

// To implement in phase 4b
void diskReadSyscall(USLOSS_Sysargs *args) {
    char *buf = (char *) args->arg1;
    // number of sectors to read
    int sectors = args->arg2;
    // starting track number
    int track = args->arg3;
    // starting block number
    int block = args->arg4;
    // which disk to access
    int unit = args->arg5;

    // get disk status register
    lock();
    if (unit < 0 || unit > 1 || sectors < 0 || track < 0 || block < 0) {
        args->arg4 = (void *) -1;
    }
    // successful input, perform operation
    DiskRequest *request;
    // fill out parameters of current request
    request->op = 0; // 0 for read op
    request->buf = buf;
    request->track = track;
    request->sectors = sectors;
    request->firstBlock = block;
    request->blocks_so_far = 0;
    request->mboxID = MboxCreate(1, sizeof(int));
    request->result = 0;
    // add request to disk queue
    lock();
    // add request to queue

    // if busy flag not set, start request
    if (!diskQ[unit].busy) {
        startRequest(unit);
    }
    unlock();
    // block until driver sends status register
    int status;
    int requestmbox;
    MboxRecv(requestmbox, &status, sizeof(status));
    // fill in return values
    args->arg4 = (void *) 0;
    args->arg1 = (void *) status;
    unlock();
}

// To implement in phase 4b
void diskWriteSyscall(USLOSS_Sysargs *args) {
    char *buf = (char *) args->arg1;
    // number of sectors to read
    int sectors = args->arg2;
    // starting track number
    int track = args->arg3;
    // starting block number
    int block = args->arg4;
    // which disk to access
    int unit = args->arg5;

    // get disk status register
    lock();
    if (unit < 0 || unit > 1 || sectors < 0 || track < 0 || block < 0) {
        args->arg4 = (void *) -1;
    }
    // successful input, perform operation
    DiskRequest *request;
    // fill out parameters of current request
    request->op = 1; // 1 for write op
    request->buf = buf;
    request->track = track;
    request->sectors = sectors;
    request->firstBlock = block;
    request->blocks_so_far = 0;
    request->mboxID = MboxCreate(1, sizeof(int));
    request->result = 0;
    // add request to disk queue
    lock();
    // add request to queue

    // if busy flag not set, start request
    if (!diskQ[unit].busy) {
        startRequest(unit);
    }
    unlock();
    // block until driver sends status register
    int status;
    int requestmbox;
    MboxRecv(requestmbox, &status, sizeof(status));
    // fill in return values
    args->arg4 = (void *) 0;
    args->arg1 = (void *) status;
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
    //USLOSS_Console("Top of handle one: %d, %c", unit, status>>8);
    // if recv is ready
    if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY) {
        // Read character from status
        TermBuf thisBuf = termBufs[unit];
        // place character in buffer and wake up waiting process
        char c = USLOSS_TERM_STAT_CHAR(status);
        if (termBufs[unit].bufIs[termBufs[unit].whichBuf] >= MAXLINE || c == '\n') {
            //USLOSS_Console("Send in handle one: %d\n", c);
            MboxSend(readReadyMbox[unit], NULL, 0);
            //USLOSS_Console("Send 2 in handle one: %d\n", termBufs[unit].bufIs[termBufs[unit].whichBuf]);
            for (int i = 0; i < termBufs[unit].bufIs[termBufs[unit].whichBuf]; i++) {
                //USLOSS_Console("Top of for in send in handle one: %d < %d\n", i, termBufs[unit].bufIs[termBufs[unit].whichBuf]);
                MboxSend(readMbox[unit], &(termBufs[unit].bufs[i][termBufs[unit].whichBuf]), sizeof(char));
            }
            MboxSend(readMbox[unit], &c, sizeof(char));
            termBufs[unit].bufIs[termBufs[unit].whichBuf] = 0;
        } else {
            //USLOSS_Console("Overwrite in handle one, %d ", termBufs[unit].bufIs[termBufs[unit].whichBuf]);
            if (termBufs[unit].whichBuf < 10) {    // input is discarded if all buffers are full
                termBufs[unit].bufs[termBufs[unit].bufIs[termBufs[unit].whichBuf]][termBufs[unit].whichBuf] = c;
                termBufs[unit].bufIs[termBufs[unit].whichBuf]++;
                if (termBufs[unit].bufIs[termBufs[unit].whichBuf] > MAXLINE) {
                    termBufs[unit].whichBuf++; // TODO mod
                }
            }
            //USLOSS_Console("then %d\n", termBufs[unit].bufIs[termBufs[unit].whichBuf]);
        }
    }
    // if xmit is ready
    if (USLOSS_TERM_STAT_XMIT(status) == USLOSS_DEV_READY && status>>8 != '\0') {
        writeIndex[unit]++;
        // if a previous send has now completed a "write" op...
        if (writeIndex[unit] <= writeLen[unit]) {
            // if some buffer is waiting to be flushed
            // send a single character
            char charToSend = status>>8;
            MboxSend(writeMbox[unit], &charToSend, 1);
        } else {
            // wake up a process
            int cr_val = 0;
            cr_val = 0x0; // this turns on the ’send char’ bit (USLOSS spec page 9)
            cr_val |= 0x2; // recv int enable
            cr_val |= 0x4; // xmit int enable
            cr_val |= (writeBuf[unit][writeIndex[unit]] << 8); // the character to send
            USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)cr_val);
        }
    }
    //USLOSS_Console("End Of Handle One: %d\n\n", unit);
}

int terminalDriver(void *arg) {
    int status;
    int unit = (int)(long)arg;
    while (1) {
        waitDevice(USLOSS_TERM_DEV, unit, &status);
        handle_one_terminal_interrupt(unit, status);
    }
}

void startRequest(int unit) {
    if (diskQ[unit].count == 0) {
        return;
    }
    diskQ[unit].curRequest = &diskQ[unit].queue[diskQ[unit].head];
    diskQ[unit].busy = 1;
    // perform seek operation
    diskQ[unit].req.opr = USLOSS_DISK_SEEK;
    diskQ[unit].req.reg1 = (void*)(long)diskQ[unit].curRequest->track;
    diskQ[unit].req.reg2 = NULL;
    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &diskQ[unit].req);
}

void handle_disk_interrupt(int unit, int status) {
    int statReg;
    USLOSS_DeviceInput(USLOSS_DISK_DEV, unit, &statReg);
    if (diskQ[unit].req.opr == USLOSS_DISK_TRACKS && diskQ[unit].busy == 1) {
        // we are doing a tracks size operation
        MboxSend(diskSizeMbox[unit], &diskQ[unit].tracks, sizeof(diskQ[unit].tracks));
        diskQ[unit].busy = 0;
    } else {
        // we are doing a read/write operation
    }
}

int diskDriver(void *arg) {
    int status;
    int unit = (int)(long)arg;
    while(1) {
        waitDevice(USLOSS_DISK_DEV, unit, &status);
        // whatToDo(Prev op)
        lock();
        handle_disk_interrupt(unit, status);
        unlock();
        // whatNext
    }
}

void phase4_init() {
    int cr_val = 0x0; // this turns on the ’send char’ bit (USLOSS spec page 9)
    cr_val |= 0x0; // recv int enable
    cr_val |= 0x0; // xmit int enable
    cr_val |= ('\0'<<8); // the character to send
    // USLOSS_Console("%x\n", cr_val);
    if (USLOSS_DeviceOutput(USLOSS_TERM_DEV, 0, (void*)(long)cr_val) != USLOSS_DEV_OK)USLOSS_Console("Bad\n");
    if (USLOSS_DeviceOutput(USLOSS_TERM_DEV, 1, (void*)(long)cr_val) != USLOSS_DEV_OK)USLOSS_Console("Bad\n");
    if (USLOSS_DeviceOutput(USLOSS_TERM_DEV, 2, (void*)(long)cr_val) != USLOSS_DEV_OK)USLOSS_Console("Bad\n");
    if (USLOSS_DeviceOutput(USLOSS_TERM_DEV, 3, (void*)(long)cr_val) != USLOSS_DEV_OK)USLOSS_Console("Bad\n");
    
    // USLOSS_Console("%x\n", cr_val);

    // phase4a syscalls
    systemCallVec[SYS_SLEEP] = (void (*)(USLOSS_Sysargs *))sleepSyscall;
    systemCallVec[SYS_TERMREAD] = (void (*)(USLOSS_Sysargs *))termReadSyscall;
    systemCallVec[SYS_TERMWRITE] = (void (*)(USLOSS_Sysargs *))termWriteSyscall;
    // phase4b syscalls
    systemCallVec[SYS_DISKSIZE] = (void (*)(USLOSS_Sysargs *))diskSizeSyscall;
    systemCallVec[SYS_DISKREAD] = (void (*)(USLOSS_Sysargs *))diskReadSyscall;
    systemCallVec[SYS_DISKWRITE] = (void (*)(USLOSS_Sysargs *))diskWriteSyscall;
    
    // Create the mailboxes used in this program:
    userModeMBoxID = MboxCreate(1, 0);
    for (int i = 0; i < 4; i++) {
        readMbox[i] = MboxCreate((MAXLINE+1)*10, sizeof(char));
        writeMbox[i] = MboxCreate(1, 1);
        readReadyMbox[i] = MboxCreate(10, 0);
        writeIndex[i] = 0;
        kernSemCreate(1, &(busySems[i]));
    }

    for (int i = 0; i < 4; i++) {
        termBufs[i].whichBuf = 0;
        for (int j = 0; j < 10; j++) {
            termBufs[i].bufIs[j] = 0;
           termBufs[i].bufs[j][MAXLINE] = '\0';
        }
    }
    for (int i = 0; i < 2; i++) {
        diskSizeMbox[i] = MboxCreate(1, sizeof(int));
        diskQ[i].busy = 0;
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
    // start the disk driver for the 2 disks -> phase4b code
    for (int i = 0; i < 2; i++) {
        char name[16];
        sprintf(name, "DiskDriver%d", i);
        int diskResult = spork("DiskDriver", diskDriver, i, USLOSS_MIN_STACK, 2);
        if (diskResult < 0) {
            USLOSS_Console("Failed to start disk driver%d\n", i);
            USLOSS_Halt(1);
        }
    }
}
