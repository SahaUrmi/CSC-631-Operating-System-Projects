
// Created by Urmi Saha on 12/6/24.

#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/ipc.h>
#include<sys/msg.h>
#include<string.h>

#define PRINT 3

// Defined Message Queue Structure
 struct msgque {
     long msg_type;
     int  senderNodeID;
     int  Request_Number;
     char msgContent[1024];
 };

int main() {
    key_t msg_key = ftok("messagequeue", 65); // Key for the message queue
    int msg_q_id = msgget(msg_key, 0666 | IPC_CREAT); // Get the message queue ID

    if (msg_q_id < 0) {
        perror("msgget");
        exit(1);
    }

    printf("Hacker is entering ...\n");

    while (1) {
        usleep(0.5*1e6);
        struct msgque msg;
        msg.msg_type = PRINT;
        msg.senderNodeID = -1;
        // Directly assign a string to msgContent
        strncpy(msg.msgContent, "HACKER ja ja ja!", sizeof(msg.msgContent));
        msg.msgContent[sizeof(msg.msgContent) - 1] = '\0';  // Ensure null-terminated string

        if (msgsnd(msg_q_id, &msg, 256, 0) == -1) {
            perror("msgrcv failed");
            continue;
        }

        printf("Hacker-> Message Queue\n");
        fflush(stdout);
    }

    return 0;
}