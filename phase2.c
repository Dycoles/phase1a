// temp variables to prevent errors

#include <phase1.h>
#include <phase2.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

// TEMP VALUES TO ELIMINATE ERRORS; COMMENT OUT LATER
USLOSS_PSR_CURRENT_MODE = 0;
USLOSS_PSR_CURRENT_INT = 0;

struct queue {
    void *head;
    void *tail;
    int size;
    // 0 for process queue, 1 for slot queue
    int queueType;
};

struct mailBox {
    int mailBoxId;
    int numSlots;
    int slotSize;
    int slotsUsed;
    // 0 for inactive, 1 for active
    int status;
    // queue for messages to be delivered
    struct queue *slots;
    // queue for producers that are waiting for a alot
    struct queue *blockedProducers;
    // queue for consumers waiting for a message
    struct queue *blockedConsumers;
};

struct mailSlot {
    int mailBoxID;
    int messageSize;
    char message[MAX_MESSAGE];
    // 0 for inactive, 1 for active
    int status;
    struct mailSlot *nextSlot;
};

struct shadowTable {
    // pid for processes
    int pid;
    // 0 for blocked, 1 for ready
    int status;
    // message and message size for slots
    int messageSize;
    void *message;
    struct mailSlot *slotPointer;
    struct shadowTable *processPointer;
};

void enqueue(struct queue *q, void *process) {
    if (q->head == NULL && q->tail == NULL) {
        q->head = process;
        q->tail = process;
    } else {
        // check if processqueue
        if (q->queueType == 0) {
            // check if slot queue
            ((struct shadowTable *)(q->tail))->processPointer = process;
        } else if (q->queueType == 1) {
            ((struct mailSlot *)(q->tail))->nextSlot = process;
        }
        q->tail = process;
    }
    q->size++;
}

void *dequeue(struct queue *q) {
   void *temp = q -> head;
    if (q->head == NULL) {
        // cannot dequeue empty queue
        return NULL;
    }
    if (q->head == q->tail) {
        q->head = NULL;
        q->tail = NULL;
    } else {
        if (q->queueType == 0) {
            // increment using process pointer
            q->head = ((struct shadowTable *)(q->head))->processPointer;
        } else if (q->queueType == 1) {
            q->head = ((struct mailSlot *)q->head)->nextSlot;
        }
        
    } 
    // return dequeued element
    q->size--;
    return temp;
}

void initQueue(struct queue *q, int type) {
    USLOSS_Console("Top of initqueue\n");
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
    q->queueType = type;
    USLOSS_Console("Bottom of initqueue\n");
}

// create mailbox array
static struct mailBox mail_box[MAXMBOX];
// create mailslot array
static struct mailSlot mail_slot[MAXSLOTS];
// create shadow process table
static struct shadowTable process_table[MAXPROC];
// create array of function pointers
void (*systemCallVec[MAXSYSCALLS])(USLOSS_Sysargs *args);
// create device array
int device[7];

// constants
int mBoxUsed = 0;
int curMailBoxId;

// define nullsys
static void nullsys() {
    printf("Error: Invalid syscall\n");
    USLOSS_Halt(1);
}

static void clock_handler() {

}

static void disk_handler() {

}

static void syscall_handler() {

}

static void term_handler() {

}

int disableInterrupts() {
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
    // ensure we are in kernel mode
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: cannot enable interrupts in user mode\n");
    }

    // restore interrupts; x used to keep compiler happy
    int x = USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT); x++;
}

void restoreInterrupts(int old_psr) {
    int x = USLOSS_PsrSet(old_psr); x++;
}

void phase2_init(void) {
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
    }
    // set all elements of mail_slot null or zero
    for (int i = 0; i < MAXSLOTS; i++) {
        mail_slot[i].mailBoxID = 0;
        mail_slot[i].messageSize = 0;
        mail_slot[i].status = 0;
    }
    // set all elements of process_table to null or zero
    for (int i = 0; i < MAXPROC; i++) {
        process_table[i].pid = 0;
        process_table[i].status = 0;
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
    // handle start service processes here
    int status;
    int result = spork("testcase_main", (*testcaseWrapper), NULL, USLOSS_MIN_STACK, 3);
    if (result < 0) {
        // print errors here then halt
        USLOSS_Console("Errors in spork returned < 0\n");
        USLOSS_Halt(1);
    }
    if (join(&status) != result) {
        USOLSS_Console("Join failed phase2_start_service_processes\n");
        USLOSS_Halt(1);
    }
 }

int MboxCreate(int numSlots, int slotSize) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call MboxCreate while in user mode!\n");
        USLOSS_Halt(1);
    }
    disableInterrupts();
    if (numSlots < 0 || slotSize < 0 || numSlots > MAXSLOTS || slotSize > MAX_MESSAGE || mBoxUsed >= MAXMBOX) {
        USLOSS_Console("Invalid argument or no mailboxes available; cannot create mailbox\n");
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
    initQueue(thisBox->blockedConsumers, 0);
    initQueue(thisBox->blockedProducers, 0);
    initQueue(thisBox->slots, 1);
    // increment index for used mailboxes and current mailbox index
    mBoxUsed++;
    curMailBoxId++;
    USLOSS_Console("Mailbox created\n");

    enableInterrupts();
    // return mailbox id
    return thisBox->mailBoxId;
}

int MboxRelease(int mailboxID) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call MboxRelease() while in user mode!\n");
        USLOSS_Halt(1);
    }
    disableInterrupts();
    // if mailbox id is invalid, return -1 and print an error 
    if (mailboxID < 0 || mailboxID >= MAXMBOX) {
        USLOSS_Console("Invalid mailbox ID\n");
        return -1;
    }
    // get mailbox
    struct mailBox *thisBox = &mail_box[mailboxID];
    // if ID is not used on a mailbox that is currently in use return -1
    if (thisBox == NULL || thisBox -> status == 0) {
        USLOSS_Console("Cannot release invalid or inactive mailbox\n");
        return -1;
    }
    // Free all slots consumed by the mailbox
    while (thisBox->slotSize > 0) {
        // empty consumed slots here
        struct mailSlot *slot = dequeue(thisBox->slots);
        if (slot != NULL) {
            slot->mailBoxID = -1;
            slot->status = 0;
        }
        thisBox -> slotSize--;
    }
    // unblock producers and consumers
    while (thisBox->blockedConsumers->size>0) {
        struct shadowTable *consumer = dequeue(thisBox->blockedConsumers);
        if (consumer != NULL) {
            unblockProc(consumer->pid);
        }
    }
    while (thisBox->blockedProducers->size>0) {
        struct shadowTable *producer = dequeue(thisBox->blockedProducers);
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

    enableInterrupts();
    // return 0, success
    USLOSS_Console("Mailbox release complete\n");
    return 0;
}

static int send(int mailboxID, void *message, int messageSize, int condition) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call send() while in user mode!\n");
        USLOSS_Halt(1);
    }
    disableInterrupts();
    // check for invalid id
    if (mailboxID < 0 || mailboxID >= MAXMBOX) {
        USLOSS_Console("Invalid mailbox ID for send, return -1\n");
        enableInterrupts();
        return -1;
    }
    // get mailbox
    struct mailBox *thisBox = &mail_box[mailboxID];

    // check for invalid arguments
    if (thisBox->status == 0 || thisBox -> slotSize < messageSize || messageSize < 0) {
        USLOSS_Console("Invalid argument for send, return -1\n");
        return -1;
    }
    // create a slot then check if we have run out of slots
    if (thisBox->slots->size == thisBox->slotsUsed) {
        // the system has run out of global mailbox slots, message cannot be queued
        return -2;
    }
    // check if there are consumers waiting in the consumer queue
    // if yes, deliver message directly to the first consumer in the queue, wake it up, and remove it from the queue
    // if no consumers are waiting, check if there's space in the mailbox's slots
        // if there is space, queue the message in a mail slot
        // if no space, add sender to producer queue and block it
    // for waking up producers, only wake up one producer at a time when a slot becomes available
    // the producer that was first in the queue should be woken first
    // have the producer write its message to the newly available slot
    enableInterrupts();
    return 0;
}

static int receive(int mailboxID, void *message, int maxMessageSize, int condition) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call receive() while in user mode!\n");
        USLOSS_Halt(1);
    }
    disableInterrupts();
    // check for invalid id
    if (mailboxID < 0 || mailboxID >= MAXMBOX) {
        USLOSS_Console("Invalid mailbox ID for send, return -1\n");
        enableInterrupts();
        return -1;
    }
    // get mailbox
    struct mailBox *thisBox = &mail_box[mailboxID];

    // check for invalid arguments
    if (thisBox->status == 0) {
        USLOSS_Console("Invalid argument for receive, return -1\n");
        return -1;
    }
    int messageReceivedSize = 0;
    // handle receive logic here
    // check to see if the mailbox was released before the receive could happen
    if (0) {
        return -1;
    }
    enableInterrupts();
    return messageReceivedSize;
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

void waitDevice(int type, int unity, int *status) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call waitDevice() while in user mode!\n");
        USLOSS_Halt(1);
    }
    disableInterrupts();
    enableInterrupts();
}
