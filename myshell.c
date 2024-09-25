#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glob.h>
#include <fcntl.h>
#include <sys/wait.h>

#define MAX_COMMAND_LENGTH 1024
#define MAX_ARGS 100

int lastES = 0;

void parseAndExecute(char *command);
void executeCommand(char *argv[]);
void executeWhichCommand(const char *command);
void handleRedirectionAndExecution(char *args[], char *inputFile, char *outputFile);
void handlePipeAndExecution(char *args1[], char *args2[], char *inputFile, char *outputFile);
void expandWildcards(char *arg, char **argv, int *argc);

// Main function to drive the shell
int main(int argc, char *argv[]) {
    if (argc == 2) {
        // Batch mode: commands are read from a file
        FILE *file = fopen(argv[1], "r");
        if (!file) {
            perror("Error opening file");
            exit(EXIT_FAILURE);
        }
        char command[MAX_COMMAND_LENGTH];
        while (fgets(command, sizeof(command), file)) {
            command[strcspn(command, "\n")] = 0; // Remove trailing newline
            parseAndExecute(command);
        }
        fclose(file);
    } else {
        // Interactive mode: commands are read from stdin
        char command[MAX_COMMAND_LENGTH];
        printf("Custom shell. Type 'exit' to quit.\n");
        while (1) {
            printf("> ");
            if (!fgets(command, sizeof(command), stdin)) break;
            command[strcspn(command, "\n")] = 0; // Remove trailing newline
            if (strcmp(command, "exit") == 0) break;
            parseAndExecute(command);
        }
    }
    return 0;
}

void parseAndExecute(char *cmd) {
    if (cmd[0] == '\0' || cmd[0] == '#') return; // Ignore empty lines and comments

    char *args[MAX_ARGS], *inputFile = NULL, *outputFile = NULL, *pipeArgs[MAX_ARGS];
    int argc = 0, argcPipe = 0, pipeFound = 0;

    for (char *token = strtok(cmd, " "); token != NULL; token = strtok(NULL, " ")) {
        if (strcmp(token, "|") == 0) {
            pipeFound = 1;
            args[argc] = NULL; // Terminate the first part of the command
            continue;
        }

        if (!pipeFound) {
            if (strcmp(token, "<") == 0 || strcmp(token, ">") == 0) {
                char *fileToken = strtok(NULL, " ");
                if (!fileToken) {
                    fprintf(stderr, "Syntax error: Missing filename after '%s'\n", token);
                    return;
                }
                if (strcmp(token, "<") == 0) inputFile = fileToken;
                else outputFile = fileToken;
            } else if (strchr(token, '*')) {
                expandWildcards(token, args, &argc);
            } else {
                args[argc++] = token;
            }
        } else {
            // Parse the second part of the command after a pipe
            pipeArgs[argcPipe++] = token;
        }
    }

    if (argc == 0 && argcPipe == 0) return; // No command entered

    args[argc] = NULL; // Null-terminate the arguments array
    pipeArgs[argcPipe] = NULL; // Null-terminate the pipe arguments array

    // Handle built-in commands without forking
    if (strcmp(args[0], "cd") == 0 || strcmp(args[0], "pwd") == 0 || strcmp(args[0], "which") == 0) {
        executeCommand(args);
        return;
    }

    // Execute the command with potential redirection and pipe
    if (pipeFound) {
        handlePipeAndExecution(args, pipeArgs, inputFile, outputFile);
    } else {
        handleRedirectionAndExecution(args, inputFile, outputFile);
    }
}

void executeCommand(char *argv[]) {
    if (strcmp(argv[0], "cd") == 0) {
        if (argv[1] == NULL || chdir(argv[1]) != 0) {
            perror("cd error");
            lastES = 1;
        }
    } else if (strcmp(argv[0], "pwd") == 0) {
        char cwd[MAX_COMMAND_LENGTH];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        } else {
            perror("pwd error");
            lastES = 1;
        }
    } else if (strcmp(argv[0], "which") == 0) {
        if (argv[1]) {
            executeWhichCommand(argv[1]);
        } else {
            fprintf(stderr, "which: missing argument\n");
            lastES = 1;
        }
    } else {
        // Execute external command
        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            execvp(argv[0], argv);
            // If execvp returns, it must have failed
            perror("execvp");
            exit(EXIT_FAILURE);
        } else if (pid > 0) {
            // Parent process
            wait(NULL); // Wait for the child process to finish
        } else {
            // Fork failed
            perror("fork");
            exit(EXIT_FAILURE);
        }
    }
}

void executeWhichCommand(const char *command) {
    // Implementation similar to the previous `execute_which_command` function
    char *path = getenv("PATH");
    if (!path) {
        fprintf(stderr, "Error: PATH environment variable not found\n");
        return;
    }

    char *pathCopy = strdup(path);
    char fullPath[MAX_COMMAND_LENGTH];
    for (char *dir = strtok(pathCopy, ":"); dir != NULL; dir = strtok(NULL, ":")) {
        snprintf(fullPath, sizeof(fullPath), "%s/%s", dir, command);
        if (access(fullPath, X_OK) == 0) {
            printf("%s\n", fullPath);
            free(pathCopy);
            return;
        }
    }

    printf("which: no %s in (%s)\n", command, path);
    free(pathCopy);
}

void handleRedirectionAndExecution(char *args[], char *inputFile, char *outputFile) {
    // Fork and execute the command with optional redirection
    pid_t pid = fork();
    if (pid == 0) {
        // Child process: set up redirection if needed and execute the command
        if (inputFile) {
            int inFd = open(inputFile, O_RDONLY);
            if (inFd < 0) {
                perror("Error opening input file");
                exit(EXIT_FAILURE);
            }
            dup2(inFd, STDIN_FILENO);
            close(inFd);
        }
        if (outputFile) {
            int outFd = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (outFd < 0) {
                perror("Error opening output file");
                exit(EXIT_FAILURE);
            }
            dup2(outFd, STDOUT_FILENO);
            close(outFd);
        }
        execvp(args[0], args);
        perror("Error executing command with redirection");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        // Parent process: wait for the child to finish
        wait(NULL);
    } else {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    }
}

void handlePipeAndExecution(char *args1[], char *args2[], char *inputFile, char *outputFile) {
    // Implementation of pipe handling between two commands, with optional input/output redirection
    int pipeFds[2];
    if (pipe(pipeFds) != 0) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid_t pid1 = fork();
    if (pid1 == 0) {
        // First command: output to pipe
        if (inputFile) {
            int inFd = open(inputFile, O_RDONLY);
            dup2(inFd, STDIN_FILENO);
            close(inFd);
        }
        dup2(pipeFds[1], STDOUT_FILENO);
        close(pipeFds[0]);
        close(pipeFds[1]);
        execvp(args1[0], args1);
        perror("execvp first command");
        exit(EXIT_FAILURE);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        // Second command: input from pipe
        if (outputFile) {
            int outFd = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            dup2(outFd, STDOUT_FILENO);
            close(outFd);
        }
        dup2(pipeFds[0], STDIN_FILENO);
        close(pipeFds[0]);
        close(pipeFds[1]);
        execvp(args2[0], args2);
        perror("execvp second command");
        exit(EXIT_FAILURE);
    }

    // Close pipe in the parent process and wait for both child processes
    close(pipeFds[0]);
    close(pipeFds[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}

void expandWildcards(char *arg, char **argv, int *argc) {
    // Wildcard expansion implementation
    glob_t globResult;
    if (glob(arg, GLOB_NOCHECK | GLOB_TILDE, NULL, &globResult) == 0) {
        for (size_t i = 0; i < globResult.gl_pathc && *argc < MAX_ARGS - 1; i++) {
            argv[(*argc)++] = strdup(globResult.gl_pathv[i]);
        }
    }
    globfree(&globResult);
}
