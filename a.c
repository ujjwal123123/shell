#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h> // needed for execl

#define COLOR "\x1B[31m"
#define RESET "\x1B[0m"
#define READ 0  // read (in) end of pipe
#define WRITE 1 // write (out) end of pipe

jmp_buf sigterm_buffer;
int devNull;

// do not do anything on ctrl-c
void sigint_handler() {
    // with help from
    // https://stackoverflow.com/questions/57480852/how-to-handle-control-c-signal-while-designing-a-shell
    printf("\n");
}

// Returns array of null-terminated strings
char **parse_command_with_spaces(char *command) {
    char **args = (char **)malloc(sizeof(char *) * 100);
    char *save_ptr;
    args[0] = strtok_r(command, " ", &save_ptr);
    int i = 0;
    while (args[i] != NULL) {
        args[++i] = strtok_r(NULL, " ", &save_ptr);
    }

    return args;
}

// Returns 0 if internal command was executed
int exec_internal_command(char **parsed_command) {
    char *command_name = parsed_command[0];

    if (strcmp(command_name, "exit") == 0) {
        printf("Bye bye!!\n");
        exit(0);
    }
    else if (strcmp(command_name, "test") == 0) {
        return 0;
    }
    else if (strcmp(command_name, "cd") == 0) {
        int status = chdir(parsed_command[1]);
        if (status == -1) {
            fprintf(stderr, "Error: Directory could not be changed\n");
        }
        return 0;
    }
    else if (strcmp(command_name, "source") == 0) {
        char *script_file = parsed_command[1];

        pid_t pid = fork();

        if (pid == 0) {
            int script_fd = open(script_file, O_RDONLY);
            if (script_fd < 0) {
                fprintf(stderr, "Cannot open file\n");
                return -1;
            }
            dup2(script_fd, STDIN_FILENO);
            // NOTE: name of the executable is hardcoded. `source` won't work
            // unless shell execuable is found at this location.
            int status = execl("./lab2shell", "lab2shell", "-noprompt", NULL);
            if (status == -1) {
                fprintf(stderr,
                        "Error: Could not execute the command. Line %d",
                        __LINE__);
            }

            return 0;
        }
        else {
            int status = 0;
            wait(&status);
            return WEXITSTATUS(status);
        }

        return 0;
    }
    else if (strcmp(command_name, "echo") == 0) {
        // NOTE: Quotes are not parsed. `echo "hello world" prints `"hello
        // world"` not `hello world`.
        for (char **itr = parsed_command + 1; *itr != NULL; itr++) {
            printf("%s ", *itr);
        }
        printf("\n");
        return 0;
    }

    return 1;
}

// remove trailing whitespace
void strstrip(char *s) {
    size_t size;
    char *end;

    size = strlen(s);

    if (!size)
        return;

    end = s + size - 1;
    while (end >= s && isspace(*end))
        end--;
    *(end + 1) = '\0';

    while (*s && isspace(*s))
        s++;
}

// Return 0 on success, -1 on failure
int parse_file_redirection(int fd[], char *command) {
    int in_fd = -1;
    int out_fd = -1;

    char *in_file_name = NULL;
    char *out_file_name = NULL;

    for (char *itr = command; *itr != '\0'; itr++) {
        if (*itr == '>') {
            *itr = '\0';
            while (*(itr + 1) == ' ') {
                itr++;
            }
            out_file_name = itr + 1;
        }
        else if (*itr == '<') {
            *itr = '\0';
            while (*(itr + 1) == ' ') {
                itr++;
            }
            in_file_name = itr + 1;
        }
    }

    if (in_file_name) {
        strstrip(in_file_name);
        in_fd = open(in_file_name, O_RDONLY);
        if (in_fd < 0) {
            fprintf(stderr, "Cannot open file\n");
            return -1;
        }
    }
    if (out_file_name) {
        strstrip(out_file_name);
        out_fd = creat(out_file_name, 0644); // this is rw-r--r--
        if (out_fd < 0) {
            fprintf(stderr, "Cannot create file %d\n", __LINE__);
            return -1;
        }
    }

    fd[READ] = in_fd;
    fd[WRITE] = out_fd;
    return 0;
}

// Retuns 0 if a command was executed. The command must not contain a pipe or
// a semi-colon.
int exec_single_command(char *command, int pipe_fd[2], bool background) {
    int redirection_fd[2]; // in, out

    if (parse_file_redirection(redirection_fd, command) != 0) {
        fprintf(stderr, "Error: Could not parse file redirections. Line %d\n",
                __LINE__);
        return -1;
    }

    char **args = parse_command_with_spaces(command);

    if (exec_internal_command(args) == 0) {
        free(args);

        return 0;
    }

    pid_t pid = fork();

    if (pid == 0) {
        // child process

        if (redirection_fd[READ] != -1) {
            dup2(redirection_fd[READ], STDIN_FILENO);
        }
        else if (pipe_fd[READ] != -1) {
            dup2(pipe_fd[READ], STDIN_FILENO);
        }
        if (redirection_fd[WRITE] != -1) {
            dup2(redirection_fd[WRITE], STDOUT_FILENO);
        }
        else if (pipe_fd[WRITE] != -1) {
            dup2(pipe_fd[WRITE], STDOUT_FILENO);
        }

        int status = execvp(args[0], args);
        // NOTE: ^C if passes via stdin, may not be understood by the shell.
        if (status == -1) {
            fprintf(stderr,
                    "Error: Could not execute the command. `%s` is not a "
                    "valid command.\n",
                    args[0]);
        }
        return 0;
    }
    else {
        int status = 0;
        if (!background) {
            wait(&status);
        }
        else {
            printf("Sending the process to background\n");
        }
        free(args);
        return WEXITSTATUS(status);
    }
}

// Returns 0 if a command was executed. The command may contain a pipe. The
// command must not contain a semi-colon.
int exec_command_with_pipe(char *command, bool background) {
    char **parsed_pipes = (char **)malloc(sizeof(char *) * 100);
    char *save_ptr;
    parsed_pipes[0] = strtok_r(command, "|", &save_ptr); // without pipes
    int len = 0;
    while (parsed_pipes[len] != NULL) {
        // exec_single_command(parsed_commands_without_pipe[i]);
        parsed_pipes[++len] = strtok_r(NULL, "|", &save_ptr);
    }

    int pipe_fd[2] = {-1, -1}; // in, out
    for (int j = 0; j < len; j++) {
        int in_fd = -1;
        int out_fd = -1;
        if (j != 0) {
            in_fd = pipe_fd[READ];
        }
        if (j != len - 1) {
            if (pipe(pipe_fd) == -1) { // create a pipe
                fprintf(stderr, "Pipe could not be created\n");
                exit(1);
            };
            out_fd = pipe_fd[WRITE];
        }
        else if (background) {
            out_fd = devNull;
        }

        int pipe_temp[2] = {in_fd, out_fd};
        exec_single_command(parsed_pipes[j], pipe_temp, background);

        if (j != len - 1) {
            close(pipe_fd[STDOUT_FILENO]);
        }
    }

    free(parsed_pipes);
    return 0;
}

// Execute a command which may consist of a number of commands separated by
// `;`q
int exec_multi_command(char *multi_command, bool background) {
    char **parsed_commands_without_semicolon =
        (char **)malloc(sizeof(char *) * 200);

    char *save_ptr;
    parsed_commands_without_semicolon[0] =
        strtok_r(multi_command, ";", &save_ptr);
    int i = 0;
    while (parsed_commands_without_semicolon[i] != NULL) {
        exec_command_with_pipe(parsed_commands_without_semicolon[i],
                               background);
        parsed_commands_without_semicolon[++i] =
            strtok_r(NULL, ";", &save_ptr);
    }
    free(parsed_commands_without_semicolon);

    return 0;
}

bool is_background(char *command) {
    char *end;
    size_t size = strlen(command);

    if (!size) {
        return false;
    }

    bool amp_found = false; // ampersand found

    end = command + size - 1;
    while (end >= command && !isalnum(*end) && *end != '&') {
        end--;
    }
    if (*end == '&') {
        *end = '\0';
        amp_found = true;
    }

    return amp_found;
}

int main(int argc, char **argv) {
    bool prompt = true;
    if (argc >= 2 && strcmp(argv[1], "-noprompt") == 0) {
        prompt = false;
    }

    devNull = open("/dev/null", O_WRONLY);

    char *current_dir_buf = (char *)malloc(sizeof(char) * 200);

    char *command = (char *)malloc(sizeof(char) * 500);

    sigaction(SIGINT, &(struct sigaction){.sa_handler = sigint_handler},
              NULL);

    while (1) {
        if (prompt) {
            printf(COLOR "%s $ " RESET, getcwd(current_dir_buf, 200));
        }
        int status = scanf(" %[^\n]s", command);

        bool background = is_background(command);

        if (status == EOF) {
            if (errno == EINTR) {
                continue; // read operation interrupted by signal
            }
            // handle Ctrl-D
            exec_multi_command("exit", false);
        }
        else if (exec_multi_command(command, background) == 0) {
            ;
        }
    }

    free(current_dir_buf);
    free(command);

    return 0;
}
