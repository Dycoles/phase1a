
#include "phase1.h"
#include "phase2.h"
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

// TEMP VALUES TO ELIMINATE ERRORS; COMMENT OUT LATER
//USLOSS_PSR_CURRENT_MODE = 0;
//USLOSS_PSR_CURRENT_INT = 0;

struct Node {
    // pid for process queue
    int pid;
    // message pointer and message size for slot queue
    void *message;
    int messageSize;
    struct Node *next;
};

struct queue {
    struct Node *head;
    struct Node *tail;
    int size;
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
};

struct shadowTable {
    int pid;
    // 0 for blocked, 1 for ready
    int status;
    //struct shadowTable *nextInQueue;
};

void enqueue(struct queue *q, int pid, void *message, int messageSize) {
    struct Node *node = malloc(sizeof(struct Node));
    // initialize fields for queue
    node->pid = pid;
    node->message = message;
    node->messageSize = messageSize;
    node->next = NULL;

    if (q->tail == NULL) {
        q->head = node;
        q->tail = node;
    } else {
        q->tail->next = node;
        q->tail = node;
    }
    q->size++;
}

int dequeue(struct queue *q, int pid, void **message, int messageSize) {
    if (q->head == NULL) {
        // cannot dequeue empty queue
        return -1;
    }
    struct Node *temp = q -> head;
    /*if (pid != NULL) {
        pid = temp->pid;
    }
    if (message != NULL) {
        message = temp -> message;
    }
    if (messageSize != NULL) {
        messageSize = temp -> messageSize;
    }*/
    q->head = q->head->next;
    if (q->head == NULL) {
        q->tail = NULL;
    }
    // free dequeued element
    free(temp);
    q->size--;
    return 0;
}

void initQueue(struct queue *q) {
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
}

// create mailbox array
static struct mailBox mail_box[MAXMBOX];
// create mailslot array
static struct mailSlot mail_slot[MAXSLOTS];
// create shadow process table
static struct shadowTable process_table[MAXPROC];
// create array of function pointers
void (*systemCallVec[MAXSYSCALLS])(USLOSS_Sysargs *args);

// constants
int mBoxUsed = 0;
int curMailBoxId;

// define nullsys
static void nullsys() {
    printf("Error: Invalid syscall\n");
    USLOSS_Halt(1);
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

void phase2_init(void) {
    // set all of the elements of systemCallVec[] to nullsys
    disableInterrupts();
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

    // handle init logic here
    //enableInterrupts(); TODO Shouldn't be commented out, but doesn't work otherwise
}

void phase2_start_service_processes(void) {
    // handle startr service processes here
    disableInterrupts();
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
    USLOSS_Console("FIXME: Queues initialized wrong\n");
    initQueue(thisBox->blockedConsumers);
    initQueue(thisBox->blockedProducers);
    initQueue(thisBox->slots);
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
        thisBox -> slotSize--;
    }

    // unblock producers and consumers

    // release the mailbox

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
    // check to see if the system has run out of global mailbox slots
    if (0) {
        return -2;
    }

    // handle send logic here
    // check to see if the mailbox was released before the send could happen
    if (0) {
        return -1;
    }
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
    // handle recieve logic here
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

// conditional send and recieve, change condition parameter to 1 to indicate what the helper should do
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
