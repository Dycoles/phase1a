/**
 * Phase 4b, CSC 452, SP25
 * Authors: Dylan Coles and Jack Williams
 */


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
    int op;         // 0 for read op, 1 for write op
    char *buf;      // buffer
    int track;      // starting track number
    int sectors;    // number of sectors to read/write
    int firstBlock; // starting block number
    int blocks_so_far;  // how many blocks we have looked at so far
    int mboxID;     // mailbox for this request
    int result;     // returned result
    
} DiskRequest;

typedef struct {
    // keep track of processes requests
    DiskRequest queue[MAXPROC];
    DiskRequest *curRequest;

    int head;       // current index in queue
    int count;      // how many requests in queue
    int tracks;     // number of tracks in Disk
    int busy;       // 0 for not busy, 1 for busy
    int curTrack;   // current track
    USLOSS_DeviceRequest req;   // request for Disk
} DiskQueue;

DiskQueue diskQ[2];     // keep track of request queue for 2 disks
int diskSizeMbox[2];    // keep track of mailboxes for diskSize
int diskQsem[2];        // keep track of sems for disk

int readMbox[4];
int writeMbox[4];
int readReadyMbox[4];
int busySems[4];

int writeIndex[4];
char writeBuf[4][MAXLINE];
int writeLen[4];
TermBuf termBufs[4];

MinHeap sleepHeap;  // list of sleeping processes

int clockInterrupts = 0;    // keep track of clock interrupts

// Swap elements in the heap.
void swap(SleepProc* a, SleepProc* b) {
    SleepProc temp = *a;
    *a = *b;
    *b = temp;
}

// Helper function for insert.
void insertHelper(MinHeap* h, int index) {
    if (index == 0) {
        return;
    }
    int parent = (index - 1)/2;

    // Swap when child is smaller, then parent element:
    if (h->arr[parent].wakeTime > h->arr[index].wakeTime) {
        swap(&h->arr[parent], &h->arr[index]);
        insertHelper(h, parent);
    }
}

// Insert elements into the heap.
void insert(MinHeap* h, SleepProc proc) {
    if (h->size < MAXPROC) {
        h->arr[h->size] = proc;
        insertHelper(h, h->size);
        h->size++;
    }
}

// Heapify to sort elements in min heap order.
void heapify(MinHeap *h, int index) {
    int left = index * 2 + 1;
    int right = index * 2 + 2;
    int min = index;

    // Check if left or right child is at correct index:
    if (left >= h->size || left < 0) {
        left = -1;
    }
    if (right >= h->size || right < 0) {
        right = -1;
    }

    // Store left or right as min if any smaller than parent:
    if (left != -1 && h->arr[left].wakeTime < h->arr[index].wakeTime) {
        min = left;
    }
    if (right != -1 && h->arr[right].wakeTime < h->arr[min].wakeTime) {
        min = right;
    }

    // Swap the nodes:
    if (min != index) {
        swap(&h->arr[min], &h->arr[index]);
        heapify(h, min);
    }
}

// Remove smallest item and heapify.
void removeMin(MinHeap* h) {
    if (h->size == 0) {
        USLOSS_Console("Cannot remove from empty heap\n");
        USLOSS_Halt(1);
    }

    // Replace root with last element:
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
        // Invalid input, return -1:
        args->arg4 = (void *) -1;
        return;
    }

    lock();
    
    int mbox = MboxCreate(1, 0);
    int wakeTime = clockInterrupts + seconds * 10;

    if (sleepHeap.size < MAXPROC) {
        SleepProc proc;
        proc.wakeTime = wakeTime;
        proc.mboxID = mbox;
        insert(&sleepHeap, proc);
    } else {
        // Too many sleeping processes, return -1:
        args->arg4 = (void *) -1;
        unlock();
        return;
    }

    unlock();

    // Block until wake up, then release:
    MboxRecv(mbox, NULL, 0);
    MboxRelease(mbox);

    // Valid operation, so return 0:
    args->arg4 = (void *) 0;
}

void termReadSyscall(USLOSS_Sysargs *args) {
    // System Call Arguments:
    char *readBuffer = (char *) args->arg1; // arg1: buffer pointer
    int len = (int)(long) args->arg2;       // arg2: length of the buffer
    int unit = (int)(long) args->arg3;      // arg3: which terminal to read

    args->arg4 = 0; // default return value

    kernSemP(busySems[unit]);
    
    int cr_val = 0x0; // ’send char’ disable
    cr_val |= 0x2; // recv int enable
    cr_val |= 0x4; // xmit int enable
    cr_val |= (writeBuf[unit][0] << 8); // the character to send
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)cr_val);

    // Determine accurate number of chars input:
    int charsInput;
    if (len < MAXLINE) {
        charsInput = len;
    } else {
        charsInput = MAXLINE;
    }

    // Validate input:
    if (charsInput <= 0 || (unit < 0 || unit > 3)) {
        args->arg4 = -1;
        kernSemV(busySems[unit]);
        return;
    }

    MboxRecv(readReadyMbox[unit], NULL, 0);
    int i = 0;
    for (i; i < charsInput; i++) {
        // Read the char:
        if (MboxRecv(readMbox[unit], &readBuffer[i], sizeof(char)) < 0) {
            args->arg4 = (void *) -1;
            kernSemV(busySems[unit]);
            return;
        }
        
        if (readBuffer[i] == '\n') {    // if end of line
            i++;
            break;
        }
    }

    // Add a null terminator if necessary:
    if (i < len) {
        readBuffer[i] = '\0';
    } else {
        readBuffer[len] = '\0';
    }

    args->arg2 = (void *)(long) i;
    args->arg4 = (void *) 0;
    
    kernSemV(busySems[unit]);
}

void termWriteSyscall(USLOSS_Sysargs *args) {
    // System Call Arguments:
    char *buf = (char *) args->arg1;    // arg1: buffer pointer
    int len = (int)(long) args->arg2;   // arg2: length of the buffer
    int unit = (int)(long) args->arg3;  // arg3: which terminal to read
    
    // Determine accurate number of chars input:
    int charsInput;
    if (len < MAXLINE) {
        charsInput = len;
    } else {
        charsInput = MAXLINE;
    }

    // Validate input:
    if (charsInput <= 0 || (unit < 0 || unit > 3)) {
        args->arg4 = -1;
        return;
    }
    
    kernSemP(busySems[unit]);

    // Reset buffer values:
    strncpy(writeBuf[unit], buf, charsInput);
    writeLen[unit] = charsInput;
    writeIndex[unit] = 0;

    for (int i = 0; i < len; i++) {
        int thisChar;
        
        int cr_val = 0x1; // ’send char’ emable
        cr_val |= 0x0; // recv int disable
        cr_val |= 0x4; // xmit int enable
        cr_val |= (writeBuf[unit][i] << 8); // the character to send
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)cr_val);
        
        // Block until write is complete:
        MboxRecv(writeMbox[unit], &thisChar, 1);
    }

    // Reset old buffer values:
    writeIndex[unit] = 0;
    writeLen[unit] = 0;
    
    kernSemV(busySems[unit]);

    args->arg4 = (void *)0;
}

void diskSizeSyscall(USLOSS_Sysargs *args) {
    int unit = (int)(long) args->arg1;

    // Validate input:
    if (unit < 0 || unit > 1) {
        args->arg4 = (void *) -1;
        return;
    }
    
    lock();

    // Set req operations and registers:
    diskQ[unit].req.opr = USLOSS_DISK_TRACKS;
    diskQ[unit].req.reg1 = &diskQ[unit].tracks;
    diskQ[unit].req.reg2 = NULL;
    diskQ[unit].busy = 1;   // disk is now busy

    // Get device output:
    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &diskQ[unit].req);
    
    unlock();

    // Block until driver gives us track size:
    int trackNum;
    MboxRecv(diskSizeMbox[unit], &trackNum, sizeof(trackNum));

    // Return to user:
    args->arg1 = (void*)(long)512;
    args->arg2 = (void*)(long)16;
    args->arg3 = (void*)(long)trackNum;
    args->arg4 = (void*)(long) 0;
}

void diskReadSyscall(USLOSS_Sysargs *args) {
    char *buf = (char *) args->arg1;
    int sectors = args->arg2;   // number of sectors to read
    int track = args->arg3;     // starting track number
    int block = args->arg4;     // starting block number
    int unit = args->arg5;      // which disk to access

    // Validate input:
    if (unit < 0 || unit > 1 || sectors <= 0 || track < 0 || block < 0 || block >= 16) {
        args->arg4 = (void *) -1;
        args->arg1 = USLOSS_DEV_ERROR;
        return;
    }
    
    lock();

    int index = (diskQ[unit].head + diskQ[unit].count) % MAXPROC;
    DiskRequest *request = &diskQ[unit].queue[index];

    // Fill out parameters of current request:
    request->op = 0;    // 0 for read op
    request->buf = buf;
    request->track = track;
    request->sectors = sectors;
    request->firstBlock = block;
    request->blocks_so_far = 0;
    request->mboxID = MboxCreate(1, sizeof(int));
    request->result = 0;
    diskQ[unit].count++;

    // If busy flag not set, start request:
    if (!diskQ[unit].busy) {
        startRequest(unit);
    }

    // Block until driver sends status register:
    int status;
    int retval = MboxRecv(request->mboxID, &status, sizeof(status));

    MboxRelease(request->mboxID);
    unlock();

    // Fill in return values:
    args->arg4 = (void *)(long) 0;
    args->arg1 = (void *)(long) status;
}

void diskWriteSyscall(USLOSS_Sysargs *args) {
    char *buf = (char *) args->arg1;
    int sectors = args->arg2;   // number of sectors to read
    int track = args->arg3;     // starting track number
    int block = args->arg4;     // starting block number
    int unit = args->arg5;      // which disk to access

    // Validate input:
    if (unit < 0 || unit > 1 || sectors <= 0 || track < 0 || block < 0 || block >= 16) {
        args->arg4 = (void *) -1;
        args->arg1 = USLOSS_DEV_ERROR;
        return;
    }
    
    lock();

    int index = (diskQ[unit].head + diskQ[unit].count) % MAXPROC;
    DiskRequest *request = &diskQ[unit].queue[index];

    // Fill out parameters of current request:
    request->op = 1; // 1 for write op
    request->buf = buf;
    request->track = track;
    request->sectors = sectors;
    request->firstBlock = block;
    request->blocks_so_far = 0;
    request->mboxID = MboxCreate(1, sizeof(int));
    request->result = 0;
    diskQ[unit].count++;

    // If busy flag not set, start request:
    if (!diskQ[unit].busy) {
        startRequest(unit);
    }

    // Block until driver sends status register:
    int status;
    int retval = MboxRecv(request->mboxID, &status, sizeof(status));
    
    MboxRelease(request->mboxID);
    unlock();

    // Fill in return values:
    args->arg4 = (void *)(long) 0;
    args->arg1 = (void *)(long) status;
}

int clockDriver(void *arg) {
    int status;

    // Use infinite loop which increments counter each time interrupt is received:
    while (1) {
        waitDevice(USLOSS_CLOCK_DEV, 0, &status);
        clockInterrupts++;
        lock();

        // Repeatedly loop through sleep queue to check if it is time to wake up the process:
        while (sleepHeap.size > 0 && sleepHeap.arr[0].wakeTime <= clockInterrupts) {
            MboxSend(sleepHeap.arr[0].mboxID, NULL, 0);
            removeMin(&sleepHeap);
        }

        unlock();
    }
    return 0;
}

void handle_one_terminal_interrupt(int unit, int status) {
    if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY) { // if recv is ready
        // Read character from status:
        TermBuf thisBuf = termBufs[unit];

        // Place character in buffer and wake up waiting process:
        char c = USLOSS_TERM_STAT_CHAR(status);
        if (termBufs[unit].bufIs[termBufs[unit].whichBuf] >= MAXLINE || c == '\n') {
            // Line over, so send all the characters to read:
            MboxSend(readReadyMbox[unit], NULL, 0);
            
            for (int i = 0; i < termBufs[unit].bufIs[termBufs[unit].whichBuf]; i++) {
                MboxSend(readMbox[unit], &(termBufs[unit].bufs[i][termBufs[unit].whichBuf]), sizeof(char));
            }

            MboxSend(readMbox[unit], &c, sizeof(char));
            termBufs[unit].bufIs[termBufs[unit].whichBuf] = 0;
        } else {
            // If there is still room in the buffers, enqueue this char:
            if (termBufs[unit].whichBuf < 10) {    // input is discarded if all buffers are full
                termBufs[unit].bufs[termBufs[unit].bufIs[termBufs[unit].whichBuf]][termBufs[unit].whichBuf] = c;
                termBufs[unit].bufIs[termBufs[unit].whichBuf]++;
                if (termBufs[unit].bufIs[termBufs[unit].whichBuf] > MAXLINE) {
                    termBufs[unit].whichBuf++;
                }
            }
        }
    }
    
    if (USLOSS_TERM_STAT_XMIT(status) == USLOSS_DEV_READY && status>>8 != '\0') {   // if xmit is ready
        writeIndex[unit]++;

        // If a previous send has now completed a "write" op...
        if (writeIndex[unit] <= writeLen[unit]) {
            // Some buffer is waiting to be flushed. Send a single character:
            char charToSend = status>>8;
            MboxSend(writeMbox[unit], &charToSend, 1);
        } else {
            // Wake up a process:
            int cr_val = 0;
            cr_val = 0x0; // ’send char’ disable
            cr_val |= 0x2; // recv int enable
            cr_val |= 0x4; // xmit int enable
            cr_val |= (writeBuf[unit][writeIndex[unit]] << 8); // the character to send
            USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)cr_val);
        }
    }
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
    // Check to see if there are items in the queue:
    if (diskQ[unit].count == 0) {
        return;
    }

    // Use the C-SCAN Algorithm to find request.
    int index = -1;
    int curTrack = diskQ[unit].curTrack;

    // Get closest request in forward direction:
    for (int i = 0; i < diskQ[unit].count; i++) {
        int j = (diskQ[unit].head + i) % MAXPROC;
        int track = diskQ[unit].queue[j].track;
        if (track >= curTrack) {
            if (index == -1 || track < diskQ[unit].queue[index].track) {
                index = j;
            }
        }
    }

    // Loop to beginning of track and find request in forward direction if request not found:
    if (index == -1) {
        for (int i = 0; i < diskQ[unit].count; i++) {
            int j = (diskQ[unit].head + i) % MAXPROC;
            int track = diskQ[unit].queue[j].track;
            if (index == -1 || track < diskQ[unit].queue[index].track) {
                index = j;
            }
        }
    }
    
    diskQ[unit].head = index;
    diskQ[unit].curRequest = &diskQ[unit].queue[index]; // should be the index found in c-scan
    diskQ[unit].busy = 1;

    // Perform seek operation:
    diskQ[unit].req.opr = USLOSS_DISK_SEEK;
    diskQ[unit].req.reg1 = (void*)(long)diskQ[unit].curRequest->track;
    diskQ[unit].req.reg2 = NULL;
    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &diskQ[unit].req);
}

void handle_disk_interrupt(int unit, int status) {
    if (status == USLOSS_DEV_ERROR) {
        MboxSend(diskQ[unit].curRequest->mboxID, &status, sizeof(int));
        diskQ[unit].busy = 0;
        diskQ[unit].head = (diskQ[unit].head + 1) % MAXPROC;
        diskQ[unit].count--;
        startRequest(unit);
        return;
    }

    if (diskQ[unit].req.opr == USLOSS_DISK_TRACKS && diskQ[unit].busy == 1) {   // tracks size operation
        MboxSend(diskSizeMbox[unit], &diskQ[unit].tracks, sizeof(diskQ[unit].tracks));
        diskQ[unit].busy = 0;
    } else {    // read/write op
        if (diskQ[unit].req.opr == USLOSS_DISK_SEEK) {  // seek done, update track
            diskQ[unit].curTrack = diskQ[unit].curRequest->track;
            if (diskQ[unit].curRequest->op == 0) {  // read op
                diskQ[unit].req.opr = USLOSS_DISK_READ;
            } else {    // write op (op == 1)
                diskQ[unit].req.opr = USLOSS_DISK_WRITE;
            }

            // Send request:
            int block = diskQ[unit].curRequest->firstBlock + diskQ[unit].curRequest->blocks_so_far;
            char *buf = diskQ[unit].curRequest->buf + diskQ[unit].curRequest->blocks_so_far * 512;
            
            diskQ[unit].req.reg1 = block;
            diskQ[unit].req.reg2 = buf;
            USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &diskQ[unit].req);
        } else if (diskQ[unit].req.opr == USLOSS_DISK_READ || diskQ[unit].req.opr == USLOSS_DISK_WRITE) {   // perform read/write op
            diskQ[unit].curRequest->blocks_so_far++;

            int totalBlocks = diskQ[unit].curRequest->blocks_so_far + diskQ[unit].curRequest->firstBlock;

            // Check to see if it is time to switch tracks:
            if (totalBlocks >= 16) {
                int newTrack = diskQ[unit].curRequest->track + 1;
                diskQ[unit].curRequest->track = newTrack;
                diskQ[unit].curRequest->firstBlock = totalBlocks - 16;
                diskQ[unit].curRequest->blocks_so_far = 0;
                // perform seek to switch tracks
                diskQ[unit].req.opr = USLOSS_DISK_SEEK;
                diskQ[unit].req.reg1 = (void*)(long)newTrack;
                diskQ[unit].req.reg2 = NULL;
                USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &diskQ[unit].req);
                // go back to waitDevice
                return;
            }
            if (diskQ[unit].curRequest->blocks_so_far < diskQ[unit].curRequest->sectors) {  // schedule next sector
                if (diskQ[unit].curRequest->op == 0) {  // read op
                    diskQ[unit].req.opr = USLOSS_DISK_READ;
                } else {    // write op (op == 1)
                    diskQ[unit].req.opr = USLOSS_DISK_WRITE;
                }

                // Send request:
                int block = diskQ[unit].curRequest->firstBlock + diskQ[unit].curRequest->blocks_so_far;
                char *buf = diskQ[unit].curRequest->buf + diskQ[unit].curRequest->blocks_so_far * 512;
                
                diskQ[unit].req.reg1 = block;
                diskQ[unit].req.reg2 = buf;
                USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &diskQ[unit].req);
            } else {    // request complete, unblock and return result to syscall
                MboxSend(diskQ[unit].curRequest->mboxID, &status, sizeof(int));

                // Remove request from queue:
                diskQ[unit].busy = 0;
                diskQ[unit].head = (diskQ[unit].head + 1) % MAXPROC;
                diskQ[unit].count--;

                // Start next request:
                startRequest(unit);
            }
        }
    }
}

int diskDriver(void *arg) {
    int status;
    int unit = (int)(long)arg;    

    while(1) {
        waitDevice(USLOSS_DISK_DEV, unit, &status);

        kernSemP(diskQsem[unit]);
        handle_disk_interrupt(unit, status);
        kernSemV(diskQsem[unit]);
    }
}

void phase4_init() {
    int cr_val = 0x0; // ’send char’ disable
    cr_val |= 0x0; // recv int disable
    cr_val |= 0x0; // xmit int disable
    cr_val |= ('\0'<<8); // the character to send (NULL to start with)
    if (USLOSS_DeviceOutput(USLOSS_TERM_DEV, 0, (void*)(long)cr_val) != USLOSS_DEV_OK)USLOSS_Console("ERROR in phase4_init\n");
    if (USLOSS_DeviceOutput(USLOSS_TERM_DEV, 1, (void*)(long)cr_val) != USLOSS_DEV_OK)USLOSS_Console("ERROR in phase4_init\n");
    if (USLOSS_DeviceOutput(USLOSS_TERM_DEV, 2, (void*)(long)cr_val) != USLOSS_DEV_OK)USLOSS_Console("ERROR in phase4_init\n");
    if (USLOSS_DeviceOutput(USLOSS_TERM_DEV, 3, (void*)(long)cr_val) != USLOSS_DEV_OK)USLOSS_Console("ERROR in phase4_init\n");
    
    // phase4a syscalls:
    systemCallVec[SYS_SLEEP] = (void (*)(USLOSS_Sysargs *))sleepSyscall;
    systemCallVec[SYS_TERMREAD] = (void (*)(USLOSS_Sysargs *))termReadSyscall;
    systemCallVec[SYS_TERMWRITE] = (void (*)(USLOSS_Sysargs *))termWriteSyscall;

    // phase4b syscalls:
    systemCallVec[SYS_DISKSIZE] = (void (*)(USLOSS_Sysargs *))diskSizeSyscall;
    systemCallVec[SYS_DISKREAD] = (void (*)(USLOSS_Sysargs *))diskReadSyscall;
    systemCallVec[SYS_DISKWRITE] = (void (*)(USLOSS_Sysargs *))diskWriteSyscall;
    
    // Create the mailboxes and semaphores used in this program:
    userModeMBoxID = MboxCreate(1, 0);
    for (int i = 0; i < 4; i++) {
        readMbox[i] = MboxCreate((MAXLINE+1)*10, sizeof(char));
        writeMbox[i] = MboxCreate(1, 1);
        readReadyMbox[i] = MboxCreate(10, 0);
        writeIndex[i] = 0;
        kernSemCreate(1, &(busySems[i]));
    }

    // Initialize the terminal buffers:
    for (int i = 0; i < 4; i++) {
        termBufs[i].whichBuf = 0;
        for (int j = 0; j < 10; j++) {
            termBufs[i].bufIs[j] = 0;
           termBufs[i].bufs[j][MAXLINE] = '\0';
        }
    }

    // Set up the disk data:
    for (int i = 0; i < 2; i++) {
        diskSizeMbox[i] = MboxCreate(1, sizeof(int));
        diskQ[i].busy = 0;
        diskQ[i].curTrack = 0;
        diskQ[i].count = 0;
        diskQ[i].head = 0;
        diskQsem[i] = 0;
        kernSemCreate(1, &diskQsem);
    }

    // Make user mode lock available to start with:
    MboxSend(userModeMBoxID, NULL, 0);

    // Initialize heap size to 0:
    sleepHeap.size = 0;
}

void phase4_start_service_processes() {
    // Start the clock driver:
    int clockResult = spork("ClockDriver", clockDriver, NULL, USLOSS_MIN_STACK, 2);
    if (clockResult < 0) {
        USLOSS_Console("Failed to start clock driver\n");
        USLOSS_Halt(1);
    }

    // Start the term driver for the 4 terminals:
    for (int i = 0; i < 4; i++) {
        char name[16];
        sprintf(name, "TerminalDriver%d", i);
        int terminalResult = spork(name, terminalDriver, i, USLOSS_MIN_STACK, 2);
        if (terminalResult < 0) {
            USLOSS_Console("Failed to start terminal driver\n");
            USLOSS_Halt(1);
        }
    }

    // Start the disk driver for the 2 disks:
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
