#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <fcntl.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/wait.h>


#define MAXLEN 1000
#define MAXARGS 255
#define CHARMAX 255
#define MAXCOMS 256


#define MAXTOKENS 64
#define TOKEN_DELIM " \t\r\n\a"

#define READ 0
#define WRITE 1

char curDir[FILENAME_MAX];
char prevDir[FILENAME_MAX];

typedef struct command {
    char* command;
    char* arguments;
    struct command *next;
} command;



char *shell_get_line(void) {
    char * line = NULL;
    ssize_t bufsize = 0;
    getline(&line, &bufsize, stdin);
    return line;
}




/* Function for splitting arguments from given input string */
/* takes user input as parameter */
/* returns arguments array */
char **shell_split_arg(char* input) {
    int pos = 0;
    int toksize = MAXTOKENS;
    char **args = malloc(toksize * sizeof(char*));
    char *arg;

    if (!args) {
        fprintf(stderr, "Allocation error on arguments.\n");
        exit(EXIT_FAILURE);
    }

    arg = strtok(input, TOKEN_DELIM);
    while (arg != NULL) {
        args[pos] = arg;
        pos++;

        if (pos >= toksize) {
            toksize += MAXTOKENS;
            args = realloc(args, toksize*sizeof(char*));
            if (!args) {
                fprintf(stderr, "Allocation error on arguments.\n");
                exit(EXIT_FAILURE);
            }
        }
        arg = strtok(NULL, TOKEN_DELIM);
    }
    // end argument array with null
    args[pos] = '\0';
    return args;
}

int shell_welcome () {
    printf("Welcome to shell\n");
}



void shell_prompt() {
    char* buffer;
    char prompt[CHARMAX];
    char hostname[CHARMAX];
    getcwd(curDir, FILENAME_MAX);
    printf("[%s@%s]$ >>> ", getenv("LOGNAME"),curDir); 
}

// Implementation for cd command
// takes arguments as parameteters, which can be either just 'cd' or cd with directory
int shell_cd(char** args) {
    /* Save current dir as previousDir */
    getcwd(prevDir, FILENAME_MAX);
    /* if no directory is not given change to home directory */
    if (args[1] == NULL) {
        chdir(getenv("HOME"));
        return 1;

    } 
    /* Else change directory to given argument */
    else {
        if (strcmp(args[1],"-") == 0) {
            if(chdir(prevDir) == -1) {
                fprintf(stderr,"No previous directory found");
                return -1;
            }
        } else if (chdir(args[1]) == -1) {
            printf("Directory '%s' not found\n", args[1]);
            return -1;
        }
    }
    return 0;
}
void interrupt_handler(int sig) {
    char c;
    // ignore interrupt signal
    signal(sig,SIG_IGN);
    printf("\nYou tried to interrupt shell\nDo you really want to interrupt shell? (Y/N): ");
    c = getchar();
    if(toupper(c) == 'Y') {
        exit(0);
    } else
        signal(SIGINT, interrupt_handler);
    getchar();
}
int shell_pwd(char** args) {
    int filefd;
    int stOut;
    if (args[0] != NULL) {
        // check if pwd command contains IO-redirection
        if (args[1] != NULL && strcmp(args[1],">") == 0 && args[2] != NULL) {
            /* Open and create new filedescription for redirection */
            filefd = open(args[2], O_CREAT | O_TRUNC | O_WRONLY, 0600);
            /* get copy of standard output description*/
            stOut = dup(STDOUT_FILENO);
            /* duplicate file description into standard output and close it after */
            dup2(filefd, STDOUT_FILENO);
            close(filefd);
            printf("%s\n", getcwd(curDir, FILENAME_MAX));
            /* set standard output back no normal */
            dup2(stOut, STDOUT_FILENO);
            return 1;

        }
        else {
            printf("Current dir: %s\n", getcwd(curDir, FILENAME_MAX));
            return 1;

        }
    }
    return -1;
}

/* Function for executing not built in commands from bin */
/* takes in command arguments and bg status which tells if process goes to background */
/* returns -1 on failure and 1 on successs */
int execute_prog(char **args, int bg) {
    pid_t pid;
    pid = fork();
    if (pid == -1) {
        fprintf(stderr,"Child process creation failed\n");
        return -1;
    }
    /* child process */
    if (pid == 0) {
        setenv("parent",getcwd(curDir,FILENAME_MAX),1);
        if (execvp(args[0],args) == -1) {
            fprintf(stderr,"Command was not found\n");
            kill(getpid(),SIGTERM);
            return -1;
        }

    }

    /* if process isn't backgrounded wait for it to complete */
    if (bg == 0) {
        waitpid(pid,NULL,0);

    } 
    /* Otherwise dont't wait for process to complete and leave it to background */
    else {
        printf("Created background process with PID: %d\n",pid);
    }
    return 1;

}

/* Function for IO-redirection */
/* Takes to be executed commmand and arguments following the command as arguments */
/* Returns -1 on failure and 1 on success */
int shell_redirection(char** cmd, char** args) {
    pid_t pid;
    int filefd;

    pid = fork();

    if (pid == -1) {
        fprintf(stderr, "Child process creation in redirection failed\n");
        return -1;
    }

    /* Create child process */
    if (pid == 0) {
        /* Check if commands is redirected to output */
        if (strcmp(args[0],">") == 0) {
            /* Check if output parameter is given */
            if (args[1] == NULL) {
                fprintf(stderr,"No output file given\n");
                return -1;
            }
            /* Open output on given parameter as Write only */
            filefd = open(args[1], O_CREAT | O_TRUNC | O_WRONLY, 0600);
            /* Redirect std output to created file */ 
            dup2(filefd, STDOUT_FILENO);
            close(filefd);

        } 
        /* Check if command arguments contain input redirection */
        else if (strcmp(args[0], "<") == 0) {
            if (args[1] == NULL) {
                fprintf(stderr, "No input file given\n");
                return -1;
            }
            else {
                /* Redirect std input to given input file */
                filefd = open(args[1], O_RDONLY, 0600);
                dup2(filefd, STDIN_FILENO);
                close(filefd);
                /* If command arguments also contain output redirection, redirect std output to given output argument */
                if (strcmp(args[2],">") == 0 ) {
                    if (args[3] == NULL) {
                        fprintf(stderr,"Output wasnt't provided.\n");
                        return -1;

                    } else {

                        filefd = open(args[3], O_CREAT | O_TRUNC | O_WRONLY, 0600);
                        dup2(filefd,STDOUT_FILENO);
                        close(filefd);
                    }
                }
            }

        }


        /* Set parent environment to current directory */
        /* setenv("parent",getcwd(curDir,FILENAME_MAX),1); */
        /* Execute the redirected command and kill process on failure */
        if (execvp(cmd[0],cmd) == -1) {
            fprintf(stderr,"Redirection execution failed\n");
            kill(getpid(),SIGTERM);
        }
        
    }
    /* Parent waits for child processes to complete */
    waitpid(pid,NULL,0);
    /* Return 1 on success */
    return 1;

}

/* Function for parsing piped commands from given arguments */
char** parsePipeCommands(char** args) {
    int i = 0;
    int k = 0;
    char** commands = malloc(MAXCOMS*sizeof(char*));
    /* Add argument to commands array if it isn't pipe */
    while (args[i] != NULL) {
        if (strcmp(args[i],"|")!= 0) {
            commands[k] = args[i];
            printf("%s\n",commands[k]);
            k++;
        }
        i++;
    }
    k++;

    /* End command array with NULL */
    commands[k] = NULL;
    return commands;

}

/* Function for counting pipes in given arguments */
int countPipes(char** args) {
    int i = 0;
    int count = 0;
    while (args[i] != NULL) {
        if (strcmp(args[i],"|") == 0) {
            count++;
        }
        i++;
    }
    return count;
}

void shell_piping(char** args) {
    int i = 0;
    int j = 0;
    int k = 0;
    int s = 0;
    pid_t pid;
    char** comms;

    /* Count amount of pipes on arguments */
    int pipe_count = countPipes(args);

    /* Seperate piped commands into comms array */
    comms = parsePipeCommands(args);
    /* Array for current command on execution */
    char* curCommand[MAXARGS];

    /* Create both READ and WRITE file descriptions for pipes */
    int filefd[2*pipe_count];

    printf("%s\n",args[0]);

    for (i = 0; i < pipe_count; i++) {
        /* Create pipes for all commands */
        /* Check for errors in piping and exit on failure */
        if(pipe(filefd + i*2) < 0) {
            fprintf(stderr,"Error in piping\n");
            exit(EXIT_FAILURE);

        }
    }
    i = 0;

    /* Loop through every single argument in arguments */
    while(args[j] != NULL) {
        int c = 0;

        /* Get current command from arguments for execution */
        while (strcmp(args[j],"|") != 0) {
            printf("Current arg: %s\n", args[s]);
            curCommand[c] = args[j];
            j++;
            /* if next argument is empty we know that we're in last command */
            if (args[j] == NULL)  {
                c++;
                break;
            }
            c++;
        }
        curCommand[c] = NULL;
        printf("Current command: %s\n",curCommand);

        j++;

        pid = fork();

        /* Inside child process */
        if (pid == 0) {
    /* Commands which are not last need to redirect their WRITE_END for next command*/ 
            if (args[j] != NULL) {
                if (dup2(filefd[k+1],WRITE) < 0){
                    fprintf(stderr,"Error dup2\n");
                    exit(EXIT_FAILURE);

                }
            }
            /* Redirect commands to read from last command */
            if (j != 0) {
                if(dup2(filefd[k-2],READ) < 0) {
                    fprintf(stderr,"Error dup2\n");
                    exit(EXIT_FAILURE);
                }
            }
            /* close file descriptions inside child */
            for (i = 0; i < 2*pipe_count; i++) {
                close(filefd[i]);
            }

            /* Execute the piped command */
            if (execvp(curCommand[0],curCommand) < 0) {
                    fprintf(stderr,"Error in pipe execution\n");
                    exit(EXIT_FAILURE);
            }
        } 
        /* Check for errors in forking process */
        else if (pid < 0) {
            fprintf(stderr,"Error in forking process\n");
            exit(EXIT_FAILURE);
        }
        /* Increment j for going to next command */
        j++;
        /* Skip two filefd']s for next output and input in piping */
        k += 2;

    }
    /* Inside parent process */
    /* Close every file descriptions inside parent process */
    for (i = 0; i < 2 * pipe_count; i++) {
        close(filefd[i]);
    }
    /* wait for child processes to complete */
    waitpid(pid,NULL,0);

}


int shell_execution(char** args) {
    int bg = 0;
    char *args_cmd[MAXARGS];
    int i = 0;
    int j = 0;
    while (args[i] != NULL) {
        if ( (strcmp(args[i],">") == 0) || (strcmp(args[i],"<") == 0) || (strcmp(args[i],"&") == 0))
                break;
        args_cmd[i] = args[i];
        i++;
    }

    if(strcmp(args[0],"exit") == 0){
        printf("Exited shell...\n");
        exit(0);
        
    } else if (strcmp(args[0], "cd") == 0) {
        shell_cd(args);
    } else if (strcmp(args[0], "pwd") == 0)
        shell_pwd(args);

    else {
        while(args[j] && bg == 0 ) {
            if (strcmp(args[j],"&") == 0)
                bg = 1;
            else if (strcmp(args[j],">") == 0 || strcmp(args[j],"<") == 0) {
                char* io_args[] = {args[j],args[j+1],args[j+2],args[j+3]}; 
                return shell_redirection(args_cmd,io_args);
            } else if (strcmp(args[j],"|") == 0) {
                shell_piping(args);
                return 1;
            }
            j++;
        }

        args_cmd[j] = NULL;
        execute_prog(args_cmd,bg);
    }
    return 1;
}


int main(int argc, const char *argv[]) {
    char** args;
    char * input;
    shell_welcome();
    // replaceinterruption signal with own handler
    signal(SIGINT,interrupt_handler);
    while(1) {
        shell_prompt();
        input = shell_get_line();
        args = shell_split_arg(input);
        if(args[0] == NULL) continue;
        shell_execution(args);
        free(input);
        free(args);
    }
    return 0;
}
