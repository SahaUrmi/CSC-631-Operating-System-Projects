//
// Created by Urmi Saha on 12/10/24.
//

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
#include <errno.h>

#define MAX_NODE 20
#define MAX_TEXT 1024
#define REQUEST 1
#define REPLY 2
#define PRINT 3
#define PRINT_MTYPE 2024

// Message Queue Structure
typedef struct msgbuf {
    long mtype; // Message type for routing (mtype based on destination)
    int sender_id;
    int message_type; // Logical purpose of the message (REQUEST/REPLY/PRINT)
    int sequence_number;
    char mtext[MAX_TEXT];
} Message;

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
void Send_Message(int mtype, int message_type, int sender_id, int dest_node_id, int sequence_number, const char *mtext, struct shared_memory *shm) {
    Message msg;
    msg.mtype = dest_node_id; // Destination node's mtype
    msg.sender_id = sender_id;
    msg.message_type = message_type;
    msg.sequence_number = sequence_number;

    if (mtext != NULL) {
        strncpy(msg.mtext, mtext, MAX_TEXT);
        msg.mtext[MAX_TEXT - 1] = '\0';
    } else {
        msg.mtext[0] = '\0';
    }

    if (msgsnd(shm->msg_q_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
        perror("Send_Message failed");
    }
    printf("%d Sent message to node %d with sequence number %d\n", msg.sender_id, dest_node_id, sequence_number);
}

// Critical Section Processing
void critical_section(struct shared_memory *shm) {
    Message msg;
    msg.mtype = PRINT_MTYPE;  // For printing in critical section
    msg.sender_id = shm->me;

    // Send start message for printing in critical section
    sprintf(msg.mtext, "########## START OUTPUT FOR NODE %d ###############", shm->me);
    msgsnd(shm->msg_q_id, &msg, sizeof(msg) - sizeof(long), 0);

    sprintf(msg.mtext, "%d this is line 1", shm->me);
    msgsnd(shm->msg_q_id, &msg, sizeof(msg) - sizeof(long), 0);

    usleep(0.1 * 1e6);  // Simulate some processing time

    sprintf(msg.mtext, "%d this is line 2", shm->me);
    msgsnd(shm->msg_q_id, &msg, sizeof(msg) - sizeof(long), 0);

    sprintf(msg.mtext, "--END OUTPUT FOR NODE %d --", shm->me);
    msgsnd(shm->msg_q_id, &msg, sizeof(msg) - sizeof(long), 0);

    printf("Node %d: Critical Section Processing right now!\n", shm->me);
    printf("Node %d: Releasing Critical Section\n", shm->me);
}

// Send Request to Enter Critical Section
void Send_Request(struct shared_memory *shm) {
    while (1) {
        sleep(rand() % 3);  // Random sleep for simulating request timing

        P(shm->mutex);
        shm->Requesting_Critical_Section = true;
        shm->Request_Number = shm->Highest_Request_Number++;
        V(shm->mutex);

        shm->Outstanding_Reply_Count = shm->N - 1;

        for (int j = 1; j <= shm->N; j++) {
            if (j != shm->me) {
                Send_Message(j, REQUEST, shm->me, j, shm->Request_Number, NULL, shm);
                printf("Node %d: Sending request to node %d with sequence number %d\n", shm->me, j, shm->Request_Number);
            }
        }

        // Wait for all replies
        while (shm->Outstanding_Reply_Count != 0) {
            P(shm->wait_sem);
        }

        // Enter critical section
        critical_section(shm);

        // Release critical section
        shm->Requesting_Critical_Section = false;
        for (int j = 1; j <= shm->N; j++) {
            if (shm->Reply_Deferred[j]) {
                shm->Reply_Deferred[j] = false;
                Send_Message(j, REPLY, shm->me, j + shm->N, 0, NULL, shm);  // Send reply
            }
        }
    }
}

// Handle Incoming Request Messages
void Receive_Request(struct shared_memory *shm) {
    while (1) {
        Message msg;

        // Only accept messages with mtype as the node id
        if (msgrcv(shm->msg_q_id, &msg, sizeof(msg) - sizeof(long), shm->me, 0) == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("msgrcv");
            exit(EXIT_FAILURE);
        }

        printf("Node %d: Received request from Node %d with sequence number %d\n", shm->me, msg.sender_id, msg.sequence_number);
        printf("Node %d: Processing request\n", shm->me);

        // Check if the message type is REQUEST
        if (msg.message_type == REQUEST) {
            int defer_it = (shm->Requesting_Critical_Section) &&
                           ((msg.sequence_number > shm->Request_Number) || (msg.sequence_number == shm->Request_Number && msg.sender_id > shm->me));
            if (defer_it) {
                shm->Reply_Deferred[msg.sender_id] = true;
                printf("Node %d: Deferring reply to Node %d (priority check)\n", shm->me, msg.sender_id);
            } else {
                Send_Message(msg.sender_id, REPLY, shm->me, msg.sender_id + shm->N, 0, NULL, shm);  // Send reply
                printf("Node %d: Replying to Node %d\n", shm->me, msg.sender_id);
            }
        }
    }
}

// Process Receive Reply
void Receive_Reply(struct shared_memory *shm) {
    while (1) {
        Message msg;

        // Only accept messages with mtype as node_id + max_nodes
        if (msgrcv(shm->msg_q_id, &msg, sizeof(msg) - sizeof(long), shm->me + shm->N, 0) == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("msgrcv");
            exit(EXIT_FAILURE);
        }

        printf("Node %d: Received reply from Node %d\n", shm->me, msg.sender_id);
        shm->Outstanding_Reply_Count--;
        V(shm->wait_sem);
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

    key_t msg_key = ftok("msg", 65);
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
    msgctl(shm->msg_q_id, IPC_RMID, NULL);
   */
    return 0;
}
