/**
 * CS2106 AY21/22 Semester 1 - Lab 2
 *
 * This file contains function definitions. Your implementation should go in
 * this file.
 */

#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "myshell.h"

#define MAX_PROCS 128

typedef struct {
    pid_t pid;
    int status;
    int exit_status;
} proc_status_t;

enum state { EXITED, RUNNING, TERMINATING, STOPPED, WAITING };

proc_status_t* procs[MAX_PROCS] = { 0 };
pid_t proc_idx = 0;

// func declaration to avoid compiler warning
int kill(pid_t pid, int sig);

void signal_handler(int signum){
     switch (signum){
         case SIGINT:
             for (int i = 0; i < (int) proc_idx; ++i) {
                 if (procs[i]->status == WAITING) {
                     kill(-procs[i]->pid, SIGINT);
                     printf("[%d] interrupted\n", procs[i]->pid);
                     procs[i]->status = TERMINATING;
                     break;
                 }
             }
             break;
         case SIGTSTP:
             for (int i = 0; i < (int) proc_idx; ++i) {
                 if (procs[i]->status == WAITING) {
                     kill(-procs[i]->pid, SIGTSTP);
                     printf("[%d] stopped\n", procs[i]->pid);
                     procs[i]->status = STOPPED;
                     break;
                 }
             }
     }
}

void my_init(void) {
    // Initialize what you need here
}

pid_t waitpid_wrapper(pid_t pid, int* status, int options) {
    // if its non blocking, simply call waitpid
    if (options == WNOHANG) {
        return waitpid(pid, status, options);
    }
    pid_t res = -1;
    for (int i = 0; i < (int) proc_idx; ++i) {
        if (procs[i]->pid == pid) {
            procs[i]->status = WAITING;
            res = waitpid(pid, status, options);
            // if it has not been stopped, set to EXITED
            if (procs[i]->status == WAITING) {
                procs[i]->status = EXITED;
            }
            break;
        }
    }
    return res;
}

void resume_pid(int child_pid) {
    for (int i = 0; i < (int) proc_idx; ++i) {
        if (procs[i]->pid == child_pid && procs[i]->status == STOPPED) {
            kill(-child_pid, SIGCONT);
            waitpid_wrapper(child_pid, &(procs[i]->exit_status), WUNTRACED);
        }
    }
}

void get_status(void) {
    // update exited procs
    for (int i = 0; i < (int) proc_idx; ++i) {
        pid_t status = waitpid_wrapper(procs[i]->pid, &(procs[i]->exit_status), WNOHANG);
        // exited (zombie process) or error (child doesnt exist)
        if (status) {
            procs[i]->status = EXITED;
        }
    }
    // print out procs
    for (int i = 0; i < (int) proc_idx; ++i) {
        printf("[%d] ", procs[i]->pid);
        if (procs[i]->status == RUNNING) {
            printf("Running\n");
            continue;
        }
        if (procs[i]->status == TERMINATING) {
            printf("Terminating\n");
            continue;
        }
        if (procs[i]->status == STOPPED) {
            printf("Stopped\n");
            continue;
        }
        printf("Exited %d\n", WEXITSTATUS(procs[i]->exit_status));
    }
}

void wait_proc(int child_pid) {
    for (int i = 0; i < (int) proc_idx; ++i) {
        if (procs[i]->pid == child_pid && (procs[i]->status == RUNNING || procs[i]->status == TERMINATING)) {
            waitpid_wrapper(child_pid, &(procs[i]->exit_status), WUNTRACED);
            break;
        }
    }
}

void term_pid(int child_pid) {
    for (int i = 0; i < (int) proc_idx; ++i) {
        if (procs[i]->pid == child_pid && procs[i]->status == RUNNING) {
            // term pid here
            procs[i]->status = TERMINATING;
            kill(-child_pid, SIGTERM);
            break;
        }
    }
}

// if query in tokens (starting from index start_idx), returns idx of query
// else returns -1
int get_idx(char* query, char** tokens, int start_idx) {
    int idx = start_idx;
    while (tokens[idx] && strcmp(tokens[idx], query) != 0) {
        idx++;
    }
    return tokens[idx] ? idx : -1;
}

// execs command starting from index start till NULL
// command is guaranteed to be valid
pid_t exec_command(int start, char **tokens) {
    int in = get_idx("<", tokens, start);
    int out = get_idx(">", tokens, start);
    int err = get_idx("2>", tokens, start);
    pid_t child_pid = fork();
    if (!child_pid) {
        // set unique pgid
        setpgid(0, 0);
        int fd;
        // handle <
        if (in != -1) {
            fd = open(tokens[in + 1], O_RDONLY);
            dup2(fd, STDIN_FILENO);
            close(fd);
            tokens[in] = NULL;
        }
        // handle >
        if (out != -1) {
            fd = open(tokens[out + 1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH | S_IRGRP);
            dup2(fd, STDOUT_FILENO);
            close(fd);
            tokens[out] = NULL;
        }
        // handle 2>
        if (err != -1) {
            fd = open(tokens[err + 1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH | S_IRGRP);
            dup2(fd, STDERR_FILENO);
            close(fd);
            tokens[err] = NULL;
        }
        execv(tokens[start], (char* const*) &tokens[start]);
        perror("execv");
        fprintf(stderr, "Did not recognize %s.\n", tokens[start]);
        exit(EXIT_FAILURE);
    }
    return child_pid;
}

void my_process_command(size_t num_tokens, char **tokens) {
    // Your code here, refer to the lab document for a description of the arguments
    // register handler
    signal(SIGINT, signal_handler);
    signal(SIGTSTP, signal_handler);
    const char *const cmd = tokens[0];
    if (!cmd) {
        // no-op
        return;
    }
    if (strcmp(cmd, "info") == 0) {
        get_status();
        return;
    }
    if (strcmp(cmd, "wait") == 0) {
        int pid = atoi(tokens[1]);
        if (pid == 0 && strcmp(tokens[1], "0") != 0) {
            // invalid num
            return;
        };
        wait_proc(pid);
        return;
    }
    if (strcmp(cmd, "terminate") == 0) {
        int pid = atoi(tokens[1]);
        if (pid == 0 && strcmp(tokens[1], "0") != 0) {
            // invalid num
            return;
        };
        term_pid(pid);
        return;
    }
    if (strcmp(cmd, "fg") == 0) {
        if (!tokens[1]) {
            return;
        }
        int pid = atoi(tokens[1]);
        if (pid == 0 && strcmp(tokens[1], "0") != 0) {
            // invalid num
            return;
        };
        resume_pid(pid);
        return;
    }
    // check if first binary exists
    if (access(tokens[0], F_OK) != 0 ) {
        printf("%s not found\n", tokens[0]);
        return;
    }
    // handle background task
    if (strcmp(tokens[num_tokens - 2], "&") == 0) {
        tokens[num_tokens - 2] = NULL; // set end of command
        // check if invalid input file is supplied
        int redir_idx = get_idx("<", tokens, 0);
        if (redir_idx != -1 && access(tokens[redir_idx + 1], F_OK) != 0) {
            printf("%s does not exist\n", tokens[redir_idx + 1]);
            return;
        }
        proc_status_t* proc = (proc_status_t*) malloc(sizeof(proc_status_t));
        proc->pid = exec_command(0, tokens);
        proc->status = RUNNING;
        printf("Child[%d] in background\n", proc->pid);
        procs[proc_idx++] = proc;
        return;
    }
    // handle one or more chained tasks
    for (int i = 0; i < (int) num_tokens - 1; ++i) {
        // check if file exists
        if (access(tokens[i], F_OK) != 0 ) {
            printf("%s not found\n", tokens[i]);
            return;
        }
        // get index of && if avail, else terminating NULL
        int start = i;
        while (tokens[i] && strcmp(tokens[i], "&&") != 0) i++;
        bool isFinalCommand = !tokens[i];
        tokens[i] = NULL;
        // check if invalid input file is supplied``
        int redir_idx = get_idx("<", tokens, start);
        if (redir_idx != -1 && access(tokens[redir_idx + 1], F_OK) != 0) {
            printf("%s does not exist\n", tokens[redir_idx + 1]);
            return;
        }
        // run the binary
        proc_status_t* proc = (proc_status_t*) malloc(sizeof(proc_status_t));
        proc->pid = exec_command(start, tokens);
        proc->status = EXITED;
        procs[proc_idx++] = proc;
        waitpid_wrapper(proc->pid, &(proc->exit_status), WUNTRACED);
        if (!isFinalCommand && proc->exit_status != EXIT_SUCCESS) {
            printf("%s failed\n", tokens[start]);
            return;
        }
    }
}

void my_quit(void) {
    // sigterm to all
    for (int i = 0; i < (int) proc_idx; ++i) {
        if (procs[i]->status == RUNNING || procs[i]->status == TERMINATING) {
            kill(-procs[i]->pid, SIGTERM);
            procs[i]->status = TERMINATING;
        }
        if (procs[i]->status == STOPPED) {
            kill(-procs[i]->pid, SIGCONT);
            kill(-procs[i]->pid, SIGTERM);
            procs[i]->status = TERMINATING;
        }
    }
    // wait for all
    for (int i = 0; i < (int) proc_idx; ++i) {
        if (procs[i]->status == TERMINATING) {
            waitpid_wrapper(procs[i]->pid, NULL, 0);
        }
    }
    // Clean up function, called after "quit" is entered as a user command
    for (int i = 0; i < (int) proc_idx; ++i) {
        free(procs[i]);
    }
    printf("Goodbye!\n");
}

