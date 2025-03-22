// temp variables to prevent errors

#include <phase1.h>
#include <phase2.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

// TEMP VALUES TO ELIMINATE ERRORS; COMMENT OUT LATER
// USLOSS_PSR_CURRENT_MODE = 0;
// USLOSS_PSR_CURRENT_INT = 0;
// USLOSS_MIN_STACK = 0;

/*struct queue {
    void *head;
    void *tail;
    int size;
    // 0 for process queue, 1 for slot queue
    int queueType;
};*/

struct mailBox {
    int mailBoxId;
    int numSlots;
    int slotSize;
    int slotsUsed;
    // 0 for inactive, 1 for active
    int status;
    // queue for messages to be delivered
    struct mailSlot *slots;
    // queue for producers that are waiting for a alot
    struct shadowTable *blockedProducers;
    // queue for consumers waiting for a message
    struct shadowTable *blockedConsumers;
};

struct mailSlot {
    int mailBoxID;
    int messageSize;
    char message[MAX_MESSAGE+1];
    int pidProducedBy;
    // 0 for inactive, 1 for active
    int status;
    struct mailSlot *nextSlot;
};

struct shadowTable {
    // pid for processes
    int mailboxID;
    int pid;
    // 0 for blocked, 1 for ready
    int status;
    // message and message size for slots
    int messageSize;
    void *message;
    struct mailSlot *slotPointer;
    struct shadowTable *processPointer;
    struct shadowTable *nextInQueue;    // TODO may need nexts for producers and consumers
};

struct shadowTable *enqueueProcess(struct shadowTable *q, struct shadowTable *process) {
    process->nextInQueue = q;
    return process;
}

struct shadowTable *dequeueProcess(struct shadowTable *q) {
    if (q == NULL) {
        return NULL;
    } else if (q->nextInQueue == NULL) {
        return q;
    } else if (q->nextInQueue->nextInQueue == NULL) {
        struct shadowTable *dequeued = q->nextInQueue;
        q->nextInQueue = NULL;
        return dequeued;
    } else {
        return dequeueProcess(q->nextInQueue);
    }
 }

 struct mailSlot *enqueueSlot(struct mailSlot *q, struct mailSlot *slot) {
    slot->nextSlot = q;
    return slot;
}

struct mailSlot *dequeueSlot(struct mailSlot *q) {
    if (q == NULL) {
        return NULL;
    } else if (q->nextSlot == NULL) {
        return q;
    } else if (q->nextSlot->nextSlot == NULL) {
        struct mailSlot *dequeued = q->nextSlot;
        q->nextSlot = NULL;
        return dequeued;
    } else {
        return dequeueSlot(q->nextSlot);
    }
 }

 int outOfSlots(struct mailSlot *q, int slotsLeft) {
    //USLOSS_Console("OOS\t");
    if (slotsLeft == 0) {
        return 1;
    } else if (q == NULL) {
        return 0;
    } else {
        return outOfSlots(q->nextSlot, slotsLeft-1);
    }
 }

 int notInProcessQueue(struct shadowTable *q, struct shadowTable *toTest) {
    if (q == NULL) {
        return 1;
    } else if (toTest->pid == q->pid) {
        return 0;
    } else {
        return notInProcessQueue(q->nextInQueue, toTest);
    }
 }

// create mailbox array
static struct mailBox mail_box[MAXMBOX];
// create mailslot array
static struct mailSlot mail_slot[MAXSLOTS];
// create shadow process table
static struct shadowTable process_table[MAXPROC];
// create array of function pointers
void (*systemCallVec[MAXSYSCALLS])(USLOSS_Sysargs *args);
// create device array -> 0 is clock, 1 is disk1, 2 is disk2, 3 is term1, 4 is term2, 5 is term 3, 6 is term4
int device[7];

// constants
int mBoxUsed = 0;
int curMailBoxId;
int totalTime = 0;

int disableInterrupts() {
    //USLOSS_Console("Disable\n");
    // store psr for later
    int old_psr = USLOSS_PsrGet();

    // ensure we are in kernel mode
    if (USLOSS_PsrSet(old_psr & ~USLOSS_PSR_CURRENT_INT) != 0) {
        USLOSS_Console("ERROR: cannot disable interrupts in user mode\n");
        USLOSS_Halt(1);
    }

    return old_psr;
}

void enableInterrupts() {
    //USLOSS_Console("Enable\n");
    // ensure we are in kernel mode
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: cannot enable interrupts in user mode\n");
    }

    // restore interrupts; x used to keep compiler happy
    int x = USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT); x++;
}

void restoreInterrupts(int old_psr) {
    //USLOSS_Console("Restore\n");
    int x = USLOSS_PsrSet(old_psr); x++;
}

// define nullsys
static void nullsys(USLOSS_Sysargs *args) {
    USLOSS_Console("nullsys(): Program called an unimplemented syscall.  syscall no: %d   PSR: 0x0%x\n", args->number, USLOSS_PsrGet());
    USLOSS_Halt(1);
}

static void clock_handler(int dev, void *arg) {
    //USLOSS_Console("TopOfClock\n");
    //dumpProcesses();
    int oldpsr = disableInterrupts();
    // ensure device is clock
    if (dev != USLOSS_CLOCK_DEV) {
        USLOSS_Console("Clock handler called by incorrect device\n");
        USLOSS_Halt(1);
    }
    // interrupt occurs every 20s, mailbox sent every 100s
    int curTime = currentTime();
    if (curTime >= totalTime + 100) {
        int status;
        totalTime = currentTime();
        MboxCondSend(device[0], &status, sizeof(int));
    }
    restoreInterrupts(oldpsr);
    //USLOSS_Console("Clock Over: ");
    //dumpProcesses();
}

static void disk_handler(int dev, void *arg) {
    //USLOSS_Console("TopOfDisk\n");
    int oldpsr = disableInterrupts();
    // ensure device is disk
    if (dev != USLOSS_DISK_DEV) {
        USLOSS_Console("Disk handler called by incorrect device\n");
        USLOSS_Halt(1);
    }
    int status;
    long unit = (int)(long)arg;
    USLOSS_DeviceInput(dev, unit, &status);
    MboxCondSend(device[1+unit], &status, sizeof(int));
    restoreInterrupts(oldpsr);
}

static void term_handler(int dev, void *arg) {
    //USLOSS_Console("TopOfTerm\n");
    int oldpsr = disableInterrupts();
    // ensure device is terminal
    if (dev != USLOSS_TERM_DEV) {
        USLOSS_Console("Terminal handler called by incorrect device\n");
        USLOSS_Halt(1);
    }
    int status;
    long unit = (int)(long)arg;
    USLOSS_DeviceInput(dev, unit, &status);
    MboxCondSend(device[3+unit], &status, sizeof(int));
    restoreInterrupts(oldpsr);
}

static void syscall_handler(int dev, void *arg) {
    //USLOSS_Console("Syscall\n");
    USLOSS_Sysargs *sys = (USLOSS_Sysargs*) arg;
    if (dev != USLOSS_SYSCALL_INT) {
        USLOSS_Console("Syscall called by incorrect device");
        USLOSS_Halt(1);
    }
    if (sys->number < 0 || sys->number >= MAXSYSCALLS) {
        USLOSS_Console("syscallHandler(): Invalid syscall number %d\n", sys->number);
        USLOSS_Halt(1);
    }
    nullsys(sys);
}

// int testcaseWrapper(void *) {
//     // Call testcase_main() and halt once it returns:
//     int retVal = testcase_main();
//     if (retVal == 0) {   // terminated normally
//         USLOSS_Halt(0);
//     } else {    // errors
//         USLOSS_Console("Some error was detected by the testcase.\n");
//         USLOSS_Halt(retVal);
//     }

//     // Should never get here, just making the compiler happy:
//     return 1;
// }

void phase2_init(void) {
    //USLOSS_Console("Init\n");
    // set all of the elements of systemCallVec[] to nullsys
    int old_psr = disableInterrupts();
    for (int i = 0; i < MAXSYSCALLS; i++) {
        systemCallVec[i] = nullsys;
    }
    // set all elements of mail_box null or zero
    for (int i = 0; i < MAXMBOX; i++) {
        mail_box[i].mailBoxId = 0;
        mail_box[i].numSlots = 0;
        mail_box[i].slotSize = 0;
        mail_box[i].slotsUsed = 0;
        mail_box[i].status = 0;
        mail_box[i].slots = NULL;
        mail_box[i].blockedProducers = NULL;
        mail_box[i].blockedConsumers = NULL;
    }
    // set all elements of mail_slot null or zero
    for (int i = 0; i < MAXSLOTS; i++) {
        mail_slot[i].mailBoxID = 0;
        mail_slot[i].messageSize = 0;
        mail_slot[i].status = 0;
        mail_slot[i].pidProducedBy = -1;
    }
    // set all elements of process_table to null or zero
    for (int i = 0; i < MAXPROC; i++) {
        process_table[i].pid = 0;
        process_table[i].mailboxID = 0;
        process_table[i].status = 0;
        process_table[i].message = NULL;
        process_table[i].messageSize = 0;
        process_table[i].processPointer = NULL;
        process_table[i].slotPointer = NULL;
        process_table[i].nextInQueue = NULL;
    }
    for (int i = 0; i < 7; i++) {
        int result = MboxCreate(0, sizeof(int));
        device[i] = result;
    }
    // functions for terminals
    // USLOSS_DeviceInput(TERM_DEV, unit, &status)
    // USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, control)
    // functions for disks
    // USLOSS_DeviceInput(USLOSS_DISK_DEV, unit, &status)
    // USLOSS_Device_output(USLOSS_DISK_DEV, unit, request)
    USLOSS_IntVec[USLOSS_CLOCK_INT] = clock_handler;
    USLOSS_IntVec[USLOSS_DISK_INT] = disk_handler;
    USLOSS_IntVec[USLOSS_TERM_INT] = term_handler;
    USLOSS_IntVec[USLOSS_SYSCALL_INT] = syscall_handler;

    // handle init logic here
    restoreInterrupts(old_psr);
}

void phase2_start_service_processes(void) {
    //USLOSS_Console("SSP\n");
    /*int status;
    int result = spork("testcase_main", (*testcaseWrapper), NULL, USLOSS_MIN_STACK, 3);
    if (result < 0) {
        // print errors here then halt
        USLOSS_Console("Errors in spork returned < 0\n");
        USLOSS_Halt(1);
    }
    if (join(&status) != result) {
        USLOSS_Console("Join failed phase2_start_service_processes\n");
        USLOSS_Halt(1);
    }*/
 }

int MboxCreate(int numSlots, int slotSize) {
    //USLOSS_Console("Create\n");
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call MboxCreate while in user mode!\n");
        USLOSS_Halt(1);
    }
    int oldpsr = disableInterrupts();
    if (numSlots < 0 || slotSize < 0 || numSlots > MAXSLOTS || slotSize > MAX_MESSAGE || mBoxUsed >= MAXMBOX) {
        //USLOSS_Console("Invalid argument or no mailboxes available; cannot create mailbox\n");
        restoreInterrupts(oldpsr);
        return -1;
    }
    // find an unused mailbox in the array
    if (curMailBoxId >= MAXMBOX || mail_box[curMailBoxId].status == 1) {
        for (int i = 0; i < MAXMBOX; i++) {
            if (mail_box[i].status == 0) {
                curMailBoxId = i;
                break;
            }
        }
    }
    struct mailBox *thisBox = &mail_box[curMailBoxId];

    // initialize fields
    thisBox->mailBoxId = curMailBoxId;
    thisBox->numSlots = numSlots;
    thisBox->slotsUsed = 0;
    thisBox->slotSize = slotSize;
    // status = 1 (active)
    thisBox->status = 1;
    // initialize mailbox queues
    thisBox->blockedConsumers = NULL;
    thisBox->blockedProducers = NULL;
    thisBox->slots = NULL;
    // increment index for used mailboxes and current mailbox index
    mBoxUsed++;
    curMailBoxId++;
    //USLOSS_Console("Mailbox created\n");

    restoreInterrupts(oldpsr);
    // return mailbox id
    return thisBox->mailBoxId;
}

int MboxRelease(int mailboxID) {
    //USLOSS_Console("Release\n");
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call MboxRelease() while in user mode!\n");
        USLOSS_Halt(1);
    }
    int oldpsr = disableInterrupts();
    // if mailbox id is invalid, return -1 and print an error 
    if (mailboxID < 0 || mailboxID >= MAXMBOX) {
        USLOSS_Console("Invalid mailbox ID\n");
        restoreInterrupts(oldpsr);
        return -1;
    }
    // get mailbox
    struct mailBox *thisBox = &mail_box[mailboxID % MAXMBOX];
    // if ID is not used on a mailbox that is currently in use return -1
    if (thisBox == NULL || thisBox -> status == 0) {
        USLOSS_Console("Cannot release invalid or inactive mailbox\n");
        restoreInterrupts(oldpsr);
        return -1;
    }
    // Free all slots consumed by the mailbox
    while (thisBox->slots != NULL) {
        //USLOSS_Console("Dequ1\n");
        // empty consumed slots here
        struct mailSlot *slot = dequeueSlot(thisBox->slots);
        if (slot == thisBox->slots) {
            thisBox->slots = NULL;
        }

        if (slot != NULL) {
            slot->mailBoxID = -1;
            slot->status = 0;
        }
        thisBox -> slotSize--;
    }
    // unblock producers and consumers
    while (thisBox->blockedConsumers != NULL) {
        //USLOSS_Console("Dequ2\n");
        struct shadowTable *consumer = dequeueProcess(thisBox->blockedConsumers);
        if (consumer == thisBox->blockedConsumers) {
            thisBox->blockedConsumers = NULL;
        }

        if (consumer != NULL) {
            unblockProc(consumer->pid);
        }
    }
    while (thisBox->blockedProducers != NULL) {
        //USLOSS_Console("Dequ3\n");
        struct shadowTable *producer = dequeueProcess(thisBox->blockedProducers);
        if (producer == thisBox->blockedProducers) {
            thisBox->blockedProducers = NULL;
        }
        
        if (producer != NULL) {
            unblockProc(producer->pid);
        }
    }
    // release the mailbox
    thisBox->mailBoxId = -1;
    thisBox->numSlots = 0;
    thisBox->slotSize = 0;
    thisBox->slotsUsed = 0;
    thisBox->status = 0;
    mBoxUsed--;
    // unblock producers and consumers
    restoreInterrupts(oldpsr);
    // return 0, success
    // USLOSS_Console("Mailbox release complete\n");
    return 0;
}

static int send(int mailboxID, void *message, int messageSize, int condition) {
    //USLOSS_Console("Top of send: %d\n", mailboxID);
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call send() while in user mode!\n");
        USLOSS_Halt(1);
    }
    int oldpsr = disableInterrupts();
    // check for invalid id
    if (mailboxID < 0 || mailboxID >= MAXMBOX) {
        USLOSS_Console("Invalid mailbox ID for send, return -1\n");
        restoreInterrupts(oldpsr);
        return -1;
    }
    // get mailbox
    struct mailBox *thisBox = &mail_box[mailboxID % MAXMBOX];

    // check for invalid arguments
    if (thisBox->status == 0 || thisBox -> slotSize < messageSize || messageSize < 0) {
        // USLOSS_Console("Invalid argument for send, return -1\n");
        restoreInterrupts(oldpsr);
        return -1;
    }
    
    // Check to see if any consumers are waiting:
    if (thisBox->blockedConsumers == NULL && outOfSlots(thisBox->slots, thisBox->numSlots)) {
        //USLOSS_Console("sen\n");
        if (condition == 1) {   // conditional send
            if (thisBox->blockedProducers == &process_table[getpid() % MAXPROC]) {
                restoreInterrupts(oldpsr);
                return -2;
            }
            thisBox->blockedProducers = enqueueProcess(thisBox->blockedProducers, &process_table[getpid() % MAXPROC]);
            restoreInterrupts(oldpsr);
            return -2;
        } else {
            if (getpid() != process_table[getpid()%MAXPROC].pid) {
                process_table[getpid()%MAXPROC].pid = getpid();
                // USLOSS_Console("%d new pid\n", getpid());
            }
            // store process info before blocking
            process_table[getpid()%MAXPROC].message = message;
            process_table[getpid()%MAXPROC].messageSize = messageSize;
            process_table[getpid()%MAXPROC].mailboxID = mailboxID;
            if (notInProcessQueue(thisBox->blockedProducers, &process_table[getpid() % MAXPROC])) {
                thisBox->blockedProducers = enqueueProcess(thisBox->blockedProducers, &process_table[getpid() % MAXPROC]);    // FIXME unsure which producer to add
            }
            // status is disabled
            process_table[getpid()%MAXPROC].status = -1;
            restoreInterrupts(oldpsr);
            blockMe();
            oldpsr = disableInterrupts();
            
            int result = process_table[getpid()%MAXPROC].status;
            // mailbox was released, we should return -1
            if (result == -1 || thisBox->status == 0) {
                restoreInterrupts(oldpsr);
                return -1;
            }
            
            // message was delivered successfully
            restoreInterrupts(oldpsr);
            return 0;
        }
    }
    
    // Slots available
    if (thisBox->blockedConsumers != NULL) {
        struct shadowTable *consumer = dequeueProcess(thisBox->blockedConsumers);
        if (consumer == thisBox->blockedConsumers) {
            thisBox->blockedConsumers = NULL;
        }
        // copy message to consumer's buffer
        if (message != NULL && consumer->message != NULL) {
            memcpy(consumer->message, message, messageSize);
        }
        
        consumer->messageSize = messageSize;
        consumer->status = 0;
        
        // wake up the consumer
        unblockProc(consumer->pid);
    } else {
        // Add message to a new slot:
        struct mailSlot *newSlot = NULL;
        for (int i = 0; i < MAXSLOTS; i++) {
            if (mail_slot[i].status == 0) {
                newSlot = &mail_slot[i];
                newSlot->status = 1;
                if (message == NULL) {
                    memset(newSlot->message, 0, thisBox->slotSize);
                } else {
                    memcpy(newSlot->message, message, messageSize);
                }
                
                newSlot->messageSize = messageSize;
                newSlot->mailBoxID = mailboxID;
                newSlot->pidProducedBy = getpid();
                newSlot->nextSlot = NULL;
                break;
            }
        }

        if (newSlot == NULL) {
            restoreInterrupts(oldpsr);
            return -2;
        }

        thisBox->slots = enqueueSlot(thisBox->slots, newSlot);
    }
    // check if there are consumers waiting in the consumer queue
    // if yes, deliver message directly to the first consumer in the queue, wake it up, and remove it from the queue
    // if no consumers are waiting, check if there's space in the mailbox's slots
        // if there is space, queue the message in a mail slot
        // if no space, add sender to producer queue and block it
    // for waking up producers, only wake up one producer at a time when a slot becomes available
    // the producer that was first in the queue should be woken first
    // have the producer write its message to the newly available slot
    restoreInterrupts(oldpsr);
    return 0;
}

static int receive(int mailboxID, void *message, int maxMessageSize, int condition) {
    //USLOSS_Console("Top of recv\n");
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call receive() while in user mode!\n");
        USLOSS_Halt(1);
    }
    //int oldpsr = disableInterrupts();
    // check for invalid id
    if (mailboxID < 0 || mailboxID >= MAXMBOX) {
        USLOSS_Console("Invalid mailbox ID for send, return -1\n");
        //restoreInterrupts(oldpsr);
        return -1;
    }
    // get mailbox
    struct mailBox *thisBox = &mail_box[mailboxID];

    // check for invalid arguments
    if (thisBox->status == 0) {
        // USLOSS_Console("Invalid argument for receive, return -1\n");
        //restoreInterrupts(oldpsr);
        return -1;
    }
    
    // handle receive logic here
    // If mailbox was released, return error:
    if (thisBox->status == 0) {
        //restoreInterrupts(oldpsr);
        return -1;
    }
    
    // Receive message:
    if (thisBox->slots == NULL) {
        USLOSS_Console("recv %d\n", USLOSS_PsrGet());
        //dumpProcesses();
        if (condition == 1) {   // conditional receive
            thisBox->blockedConsumers = enqueueProcess(thisBox->blockedConsumers, &process_table[getpid() % MAXPROC]);
            //restoreInterrupts(oldpsr);
            return -2;
        } else {
            if (getpid() != process_table[getpid()%MAXPROC].pid) {
                process_table[getpid()%MAXPROC].pid = getpid();
                USLOSS_Console("%d old pid\n", getpid());
            }
            if (notInProcessQueue(thisBox->blockedConsumers, &process_table[getpid() % MAXPROC])) {
                thisBox->blockedConsumers = enqueueProcess(thisBox->blockedConsumers, &process_table[getpid() % MAXPROC]);    // FIXME unsure which consumer to add
            }
            dumpProcesses();
            blockMe();
        }
    }
    
    // A message is now available, so receive it:
    struct mailSlot *slot = dequeueSlot(thisBox->slots);
    if (slot == thisBox->slots) {
        thisBox->slots = NULL;
    }
    if (slot->message == NULL) {
        strcpy(message, "");
        USLOSS_Console("Unblocking %d\n", slot->pidProducedBy);
        dumpProcesses();
        unblockProc(slot->pidProducedBy);
        dumpProcesses();
        //restoreInterrupts(oldpsr);
        return 0;
    } else {
        // check for insufficient buffer size
        if (strlen(slot->message) > maxMessageSize) {
            char oldPartition = slot->message[maxMessageSize-1];    // TODO -2?
            slot->message[maxMessageSize-1] = '\0';
            if (message != NULL) strcpy(message, slot->message);
            slot->message[maxMessageSize-1] = oldPartition;
            USLOSS_Console("Unblocking %d\n", slot->pidProducedBy);
            dumpProcesses();
            unblockProc(slot->pidProducedBy);
            dumpProcesses();
            //restoreInterrupts(oldpsr);
            return maxMessageSize;
        } else {
            if (message != NULL) strcpy(message, slot->message);
            //restoreInterrupts(oldpsr);
            USLOSS_Console("Unblocking %d\n", slot->pidProducedBy);
            dumpProcesses();
            unblockProc(slot->pidProducedBy);
            dumpProcesses();
            if (message == NULL || strlen(message)==0) {
                return 0;
            } else {
                return strlen(message)+1;
            }
        }
    }
    //USLOSS_Console("PANIC!\n");
}

int MboxSend(int mailboxID, void *message, int messageSize) {
    return send(mailboxID, message, messageSize, 0);
}

int MboxRecv(int mailboxID, void *message, int maxMessageSize) {
    return receive(mailboxID, message, maxMessageSize, 0);
}

// conditional send and receive, change condition parameter to 1 to indicate what the helper should do
int MboxCondSend(int mailboxID, void *message, int messageSize) {
    return send(mailboxID, message, messageSize, 1);
}

int MboxCondRecv(int mailboxID, void *message, int maxMessageSize) {
    return receive(mailboxID, message, maxMessageSize, 1);
}

void waitDevice(int type, int unit, int *status) {
    //USLOSS_Console("Top of wait\n");
    int oldpsr = disableInterrupts();
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call waitDevice() while in user mode!\n");
        USLOSS_Halt(1);
    }
    int thisDev;
    if (type == USLOSS_CLOCK_DEV) {
        thisDev = 0;
    } else if(type == USLOSS_DISK_DEV) {
        thisDev = 1;
    } else if(type == USLOSS_TERM_DEV) {
        thisDev = 3;
    } else {
        USLOSS_Console("Invalid type for wait device\n");
        USLOSS_Halt(1);
    }

    if (thisDev == 0 && unit != 0) {
        USLOSS_Console("Invalid unit for clock device\n");
        USLOSS_Halt(1);
    } else if (thisDev == 1 && (unit < 0 || unit > 1)) {
        USLOSS_Console("Invalid unit for disk device\n");
        USLOSS_Halt(1);
    } else if (thisDev == 3 && (unit < 0 || unit > 3)) {
        USLOSS_Console("Invalid unit for terminal device\n");
        USLOSS_Halt(1);
    }
    MboxRecv(device[thisDev+unit], status, sizeof(int));
    restoreInterrupts(oldpsr);
}
