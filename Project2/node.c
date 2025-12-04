#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <string.h>
#include <sys/wait.h>

#define MAX_NODE 20
#define REQUEST 1
#define REPLY 2
#define PRINT 3

// Shared Memory Structure to hold all global variables
struct shared_memory {
    int N;  // Total number of nodes
    int me; // This node's ID
    int msg_q_id; // Message queue ID
    int mutex; // Mutex semaphore
    int wait_sem; // Wait semaphore
    int Request_Number; // Current Request Number
    int Highest_Request_Number; // Highest Request Number
    int Outstanding_Reply_Count; // Outstanding reply count
    int Requesting_Critical_Section; // Flag for requesting critical section
    int Reply_Deferred[MAX_NODE]; // Deferred reply flags
};

// Defined Message Queue Structure
struct msgque {
    long msg_type;
    int senderNodeID;
    int Request_Number;
    char msgContent[256];
};

// Semaphore P operation (wait)
void P(int sem_id) {
    struct sembuf op = {0, -1, 0};
    if (semop(sem_id, &op, 1) == -1) {
        perror("P failed");
    }
}

// Semaphore V operation (signal)
void V(int sem_id) {
    struct sembuf op = {0, 1, 0};
    if (semop(sem_id, &op, 1) == -1) {
        perror("V failed");
    }
}

// Send Message to a node
void Send_Message(int msg_type, int Request_Number, int senderNodeID, int destinationNodeID, const char *msgContent, struct shared_memory *shm) {
    struct msgque msg;
    msg.msg_type = destinationNodeID * 10 + msg_type;
    msg.senderNodeID = senderNodeID;
    msg.Request_Number = Request_Number;

    if (msgContent != NULL) {
        strncpy(msg.msgContent, msgContent, 256);
        msg.msgContent[256] = '\0';
    } else {
        msg.msgContent[0] = '\0';
    }

    if (msgsnd(shm->msg_q_id, &msg, 256, 0) == -1) {
        perror("Send_Message failed");
    }
    printf("%d Sent message to node %d\n", msg.senderNodeID, destinationNodeID);
}

// Critical Section Processing
void critical_section(struct shared_memory *shm) {
    struct msgque msg;
    msg.msg_type = PRINT;
    msg.senderNodeID = shm->me;

    sprintf(msg.msgContent, "########## START OUTPUT FOR NODE %d ###############", shm->me);
    msgsnd(shm->msg_q_id, &msg, 256, 0);

    sprintf(msg.msgContent, "%d this is line 1", shm->me);
    msgsnd(shm->msg_q_id, &msg, 256, 0);

    usleep(0.1 * 1e6);

    sprintf(msg.msgContent, "%d this is line 2", shm->me);
    msgsnd(shm->msg_q_id, &msg, 256, 0);

    sprintf(msg.msgContent, "--END OUTPUT FOR NODE %d --", shm->me);
    msgsnd(shm->msg_q_id, &msg, 256, 0);

    printf("Node %d: Critical Section Processing right now!\n", shm->me);
    printf("Node %d: Releasing Critical Section\n", shm->me);
}

// Send Request to Enter Critical Section
void Send_Request(struct shared_memory *shm) {
    while (1) {
        sleep(rand() % 3);

        P(shm->mutex);
        shm->Requesting_Critical_Section = true;
        shm->Request_Number = ++shm->Highest_Request_Number;
        V(shm->mutex);

        shm->Outstanding_Reply_Count = shm->N - 1;

        for (int j = 1; j <= shm->N; j++) {
            if (j != shm->me) {
                Send_Message(REQUEST, shm->Request_Number, shm->me, j, NULL, shm);
                printf("Node %d: Sending request to node %d with sequence number %d\n", shm->me, j, shm->Request_Number);
            }
        }

        while (shm->Outstanding_Reply_Count != 0) {
            P(shm->wait_sem);
        }

        critical_section(shm);

        shm->Requesting_Critical_Section = false;
        for (int j = 1; j <= shm->N; j++) {
            if (shm->Reply_Deferred[j]) {
                shm->Reply_Deferred[j] = false;
                Send_Message(REPLY, 0, shm->me, j, NULL, shm);
            }
        }
    }
}

// Handle Incoming Request Messages
void Receive_Request(struct shared_memory *shm) {
    while (1) {
        struct msgque msg;
        msgrcv(shm->msg_q_id, &msg, 256, shm->me * 10 + REQUEST, 0);

        int k = msg.Request_Number;
        int i = msg.senderNodeID;
        int defer_it;

        if (k > shm->Highest_Request_Number) {
            shm->Highest_Request_Number = k;
        }

        P(shm->mutex);
        defer_it = (shm->Requesting_Critical_Section) &&
                   ((k > shm->Request_Number) || (k == shm->Request_Number && i > shm->me));
        V(shm->mutex);

        printf("Node %d: Received request from Node %d with sequence number %d\n", shm->me, i, k);
        printf("Node %d: Current highest request number: %d\n", shm->me, shm->Highest_Request_Number);

        if (defer_it) {
            shm->Reply_Deferred[i] = true;
            printf("Node %d: Deferring reply to Node %d (priority check)\n", shm->me, i);
        } else {
            Send_Message(REPLY, 0, shm->me, i, NULL, shm);
            printf("Node %d: Replying to Node %d\n", shm->me, i);
        }
    }
}

// Process Receive Reply
void Receive_Reply(struct shared_memory *shm) {
    while (1) {
        struct msgque msg;
        msgrcv(shm->msg_q_id, &msg, 256, shm->me * 10 + REPLY, 0);
        shm->Outstanding_Reply_Count = shm->Outstanding_Reply_Count - 1;
        V(shm->wait_sem);

        printf("Node %d: Received reply. Outstanding reply count now %d\n", shm->me, shm->Outstanding_Reply_Count);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <node_id> <total_nodes>\n", argv[0]);
        exit(1);
    }

    int shm_id = shmget(IPC_PRIVATE, sizeof(struct shared_memory), 0666 | IPC_CREAT);
    struct shared_memory *shm = (struct shared_memory *)shmat(shm_id, NULL, 0);

    shm->me = atoi(argv[1]);
    shm->N = atoi(argv[2]);

    if (shm->me < 1 || shm->me > shm->N) {
        fprintf(stderr, "Node %d must be between 1 and N\n", shm->me);
        exit(1);
    }

    srand(shm->me);

    key_t msg_key = ftok("messagequeue", 65);
    shm->msg_q_id = msgget(msg_key, 0666 | IPC_CREAT);

    shm->mutex = semget(IPC_PRIVATE, 1, 0666 | IPC_CREAT);
    shm->wait_sem = semget(IPC_PRIVATE, 1, 0666 | IPC_CREAT);

    semctl(shm->mutex, 0, SETVAL, 1);
    semctl(shm->wait_sem, 0, SETVAL, 1);

    shm->Request_Number = 0;
    shm->Highest_Request_Number = 0;
    shm->Outstanding_Reply_Count = 0;
    shm->Requesting_Critical_Section = false;
    memset(shm->Reply_Deferred, 0, sizeof(shm->Reply_Deferred));

    pid_t pid1 = fork();
    if (pid1 == 0) {
        Receive_Request(shm);
    } else {
        pid_t pid2 = fork();
        if (pid2 == 0) {
            Receive_Reply(shm);
        } else {
            Send_Request(shm);
        }
    }

    wait(NULL);
    wait(NULL);
    wait(NULL);

    // Cleanup
    /*shmdt(shm);
    semctl(shm->mutex, 0, IPC_RMID);
    semctl(shm->wait_sem, 0, IPC_RMID);
    msgctl(shm->msg_q_id, IPC_RMID, NULL);*/

    return 0;
}
