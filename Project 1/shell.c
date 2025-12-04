#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#define MAXLINE 1024
#define MAXARG 100

// To handle ^C signal
void handle_signal(int sig_num) {
    // Ignore the signal
    printf("\nType 'exit' to quit the shell.\n");
    fflush(stdout);
}

// To execute commands
void execute_Command(char **args, int background) {
    pid_t pid = fork();

    if (pid < 0) {
        // Fork failed condition
        perror("Fork failed");
        exit(0);

    } else if (pid == 0) {
        // In the child process
        if (execvp(args[0], args) == -1) {
            perror("File could not be found");
        }
        exit(0);
    } else {
        // In the parent process
        if (!background) {
            // Wait for the child to complete if not running in the background
            wait(NULL);
        }
    }
}

// To parse input into command and arguments
int parse_Input(char *input, char **args) {
    int i = 0;
    char *token = strtok(input, " \t\n");

    while (token != NULL) {
        args[i++] = token;
        token = strtok(NULL," \t\n");
    }
    args[i] = NULL;

    // Check if the command should be run in the background
    if (i > 0 && strcmp(args[i - 1], "&") == 0) {
        args[i - 1] = NULL; // Remove '&' from arguments
        return 1; // Return 1 to indicate background execution
    }
    return 0; // Return 0 to indicate foreground execution
}


int main() {
    char input[MAXLINE];
    char *args[MAXARG];
    int background;

    // Call the SIGINT handler
    signal(SIGINT, handle_signal);

    while (1) {
        // Print prompt
        printf("Simple Shell> ");
        fflush(stdout);

        // Read input
        if (fgets(input, MAXLINE, stdin) == NULL) {
            // End of input, exit the shell
            printf("\n");
            break;
        }

        // Parse the input into command and arguments
        background = parse_Input(input, args);

        // Check for 'exit' command
        if (args[0] == NULL) {
            // Empty input, continue
            continue;
        }
        if (strcmp(args[0], "exit") == 0) {
            break;
        }

        // Execute the command
        execute_Command(args, background);
    }

    return 0;
}