/*
 * cush - the customizable shell.
 *
 * Developed by Godmar Back for CS 3214 Summer 2020
 * Virginia Tech.  Augmented to use posix_spawn in Fall 2021.
 */
#define _GNU_SOURCE 1
#include <assert.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
/* Max capacity of the pid array */
#define MAX_CAP 10

/* Since the handed out code contains a number of unused functions. */
#pragma GCC diagnostic ignored "-Wunused-function"

#include "shell-ast.h"
#include "signal_support.h"
#include "termstate_management.h"
#include "utils.h"

static void usage(char *progname) {
    printf(
        "Usage: %s -h\n"
        " -h            print this help\n",
        progname);

    exit(EXIT_SUCCESS);
}

/* Build a prompt */
static char *build_prompt(void) {
    char hostn[1204] = "";
    gethostname(hostn, sizeof(hostn));
    printf("<%s@%s in %s>$ ", getenv("LOGNAME"), hostn,
           basename(getenv("PWD")));
    return strdup("");
    // return strdup("cush> ");
}

enum job_status {
    FOREGROUND,    /* job is running in foreground.  Only one job can be
                      in the foreground state. */
    BACKGROUND,    /* job is running in background */
    STOPPED,       /* job is stopped via SIGSTOP */
    NEEDSTERMINAL, /* job is stopped because it was a background job
                      and requires exclusive terminal access */
};

struct job {
    struct list_elem elem; /* Link element for jobs list. */
    struct ast_pipeline
        *pipe;               /* The pipeline of commands this job represents */
    int jid;                 /* Job id. */
    enum job_status status;  /* Job status. */
    int num_processes_alive; /* The number of processes that we know to be alive
                              */
    struct termios
        saved_tty_state; /* The state of the terminal when this job was
                            stopped after having been in foreground */
    int total_processes; /* Total number of processes */
    int pid[MAX_CAP];    /* pid array */
    pid_t pgid;          /* Process group id */
};

void handle_child_process(int fds[], bool not_last, int total_pipes,
                          int pipe_counter, char *cmd_arg, char **argv);
static void handle_child_status(pid_t pid, int status);
int get_process_pgid(int jid);
bool is_built_in(char *cmd);
void handle_build_in(struct ast_command *cmd);
void execute(struct ast_command_line *cmd_line);
bool not_last_arg(int curr_cmd, int total_commands);
void handle_pipeline(struct ast_pipeline *pipe_line, struct ast_command *cmd);

/* Utility functions for job list management.
 * We use 2 data structures:
 * (a) an array jid2job to quickly find a job based on its id
 * (b) a linked list to support iteration
 */
#define MAXJOBS (1 << 16)
static struct list job_list;
static struct job *jid2job[MAXJOBS];

/* Return job corresponding to jid */
static struct job *get_job_from_jid(int jid) {
    if (jid > 0 && jid < MAXJOBS && jid2job[jid] != NULL) {
        return jid2job[jid];
    }
    return NULL;
}

/* Return job corresponding to pid */
static struct job *get_job_from_pid(pid_t pid) {
    /* Check if pid is positive */
    if (pid > 0) {
        struct job *j;
        /* Iterates through the job list */
        for (struct list_elem *e = list_begin(&job_list);
             e != list_end(&job_list); e = list_next(e)) {
            j = list_entry(e, struct job, elem);
            /* Iterates through the pid array */
            for (int i = 0; i < j->total_processes; i++) {
                /* Check if there is a matched job */
                if (j->pid[i] == pid) {
                    return j;
                }
            }
        }
        return NULL;
    }
    /* Return NULL if pid is invalid */
    else {
        return NULL;
    }
}

/* Return the pgid corresponding to jid */
int get_process_pgid(int jid) {
    struct job *j;
    for (struct list_elem *e = list_begin(&job_list); e != list_end(&job_list);
         e = list_next(e)) {
        j = list_entry(e, struct job, elem);
        if (j->jid == jid) return j->pgid;
    }
    return -1;
}

/* Add a new job to the job list */
static struct job *add_job(struct ast_pipeline *pipe) {
    struct job *job = malloc(sizeof *job);
    /* Initialize the job structure */
    job->total_processes = 0;
    job->pipe = pipe;
    job->num_processes_alive = 0;
    /* Check if the user enter & */
    if (pipe->bg_job) {
        job->status = BACKGROUND;
    } else {
        job->status = FOREGROUND;
    }
    list_push_back(&job_list, &job->elem);
    for (int i = 1; i < MAXJOBS; i++) {
        if (jid2job[i] == NULL) {
            jid2job[i] = job;
            job->jid = i;
            return job;
        }
    }
    fprintf(stderr, "Maximum number of jobs exceeded\n");
    abort();
    return NULL;
}

/* Delete a job.
 * This should be called only when all processes that were
 * forked for this job are known to have terminated.
 */
static void delete_job(struct job *job) {
    int jid = job->jid;
    assert(jid != -1);
    jid2job[jid]->jid = -1;
    jid2job[jid] = NULL;
    ast_pipeline_free(job->pipe);
    free(job);
}

/* Get the status of the running process */
static const char *get_status(enum job_status status) {
    switch (status) {
        case FOREGROUND:
            return "Foreground";
        case BACKGROUND:
            return "Running";
        case STOPPED:
            return "Stopped";
        case NEEDSTERMINAL:
            return "Stopped (tty)";
        default:
            return "Unknown";
    }
}

/* Print the command line that belongs to one job. */
static void print_cmdline(struct ast_pipeline *pipeline) {
    struct list_elem *e = list_begin(&pipeline->commands);
    for (; e != list_end(&pipeline->commands); e = list_next(e)) {
        struct ast_command *cmd = list_entry(e, struct ast_command, elem);
        if (e != list_begin(&pipeline->commands)) printf("| ");
        char **p = cmd->argv;
        printf("%s", *p++);
        while (*p) printf(" %s", *p++);
    }
}

/* Print a job */
static void print_job(struct job *job) {
    printf("[%d]\t%s\t\t(", job->jid, get_status(job->status));
    print_cmdline(job->pipe);
    printf(")\n");
}

/*
 * Suggested SIGCHLD handler.
 *
 * Call waitpid() to learn about any child processes that
 * have exited or changed status (been stopped, needed the
 * terminal, etc.)
 * Just record the information by updating the job list
 * data structures.  Since the call may be spurious (e.g.
 * an already pending SIGCHLD is delivered even though
 * a foreground process was already reaped), ignore when
 * waitpid returns -1.
 * Use a loop with WNOHANG since only a single SIGCHLD
 * signal may be delivered for multiple children that have
 * exited. All of them need to be reaped.
 */
static void sigchld_handler(int sig, siginfo_t *info, void *_ctxt) {
    pid_t child;
    int status;

    assert(sig == SIGCHLD);

    while ((child = waitpid(-1, &status, WUNTRACED | WNOHANG)) > 0) {
        handle_child_status(child, status);
    }
}

/* Wait for all processes in this job to complete, or for
 * the job no longer to be in the foreground.
 * You should call this function from a) where you wait for
 * jobs started without the &; and b) where you implement the
 * 'fg' command.
 *
 * Implement handle_child_status such that it records the
 * information obtained from waitpid() for pid 'child.'
 *
 * If a process exited, it must find the job to which it
 * belongs and decrement num_processes_alive.
 *
 * However, note that it is not safe to call delete_job
 * in handle_child_status because wait_for_job assumes that
 * even jobs with no more num_processes_alive haven't been
 * deallocated.  You should postpone deleting completed
 * jobs from the job list until when your code will no
 * longer touch them.
 *
 * The code below relies on `job->status` having been set to FOREGROUND
 * and `job->num_processes_alive` having been set to the number of
 * processes successfully forked for this job.
 */
static void wait_for_job(struct job *job) {
    assert(signal_is_blocked(SIGCHLD));

    while (job->status == FOREGROUND && job->num_processes_alive > 0) {
        int status;

        pid_t child = waitpid(-1, &status, WUNTRACED);

        // When called here, any error returned by waitpid indicates a logic
        // bug in the shell.
        // In particular, ECHILD "No child process" means that there has
        // already been a successful waitpid() call that reaped the child, so
        // there's likely a bug in handle_child_status where it failed to update
        // the "job" status and/or num_processes_alive fields in the required
        // fashion.
        // Since SIGCHLD is blocked, there cannot be races where a child's exit
        // was handled via the SIGCHLD signal handler.
        if (child != -1)
            handle_child_status(child, status);
        else
            utils_fatal_error("waitpid failed, see code for explanation");
    }
}

/* Update child status when it received signal */
static void handle_child_status(pid_t pid, int status) {
    assert(signal_is_blocked(SIGCHLD));

    /* To be implemented.
     * Step 1. Given the pid, determine which job this pid is a part of
     *         (how to do this is not part of the provided code.)
     */
    struct job *job = get_job_from_pid(pid);

    /* Step 2. Determine what status change occurred using the
     *         WIF*() macros.
     * Step 3. Update the job status accordingly, and adjust
     *         num_processes_alive if appropriate.
     *         If a process was stopped, save the terminal state.
     */

    /* Check if the child was stopped by a stop signal */
    if (WIFSTOPPED(status)) {
        /* Set its status to stopped */
        job->status = STOPPED;
        /* Save the current terminal settings */
        termstate_save(&job->saved_tty_state);
        /* Print the stopped process */
        print_job(job);
    }
    /* Check if the child exited */
    else if (WIFEXITED(status)) {
        /* Decrement the number of running processes */
        job->num_processes_alive--;
    }
    /* Check if the child was terminated by a signal */
    else if (WIFSIGNALED(status)) {
        job->num_processes_alive = 0;
        int term_signal = WTERMSIG(status);
        if (term_signal == 6) {
            utils_error("aborted\n");
        } else if (term_signal == 8) {
            utils_error("floating point exception\n");
        } else if (term_signal == 9) {
            utils_error("killed\n");
        } else if (term_signal == 11) {
            utils_error("segmentation fault\n");
        } else if (term_signal == 15) {
            utils_error("terminated\n");
        }
    }
}

/* Check if the command is a build in function */
bool is_built_in(char *cmd) {
    return (strcmp(cmd, "jobs") == 0 || strcmp(cmd, "fg") == 0 ||
            strcmp(cmd, "bg") == 0 || strcmp(cmd, "stop") == 0 ||
            strcmp(cmd, "kill") == 0 || strcmp(cmd, "exit") == 0 ||
            strcmp(cmd, "history") == 0);
}

/* Handle the build in function */
void handle_build_in(struct ast_command *cmd) {
    /* Count the number of arguments in the command */
    char **cmd_argv = cmd->argv;
    int argc = 0;
    while (*(cmd_argv + argc) != NULL) {
        argc++;
    }

    /* Compare the first argument to the build in function */
    if (strcmp(*cmd_argv, "exit") == 0) {
        exit(0);
    } else if (strcmp(*cmd_argv, "jobs") == 0) {
        /* Check if jobs has only one argument */
        if (argc == 1) {
            struct job *j;
            /* Iterates through the job list */
            for (struct list_elem *e = list_begin(&job_list);
                 e != list_end(&job_list); e = list_next(e)) {
                j = list_entry(e, struct job, elem);
                /* Print out the existing job */
                print_job(j);
            }
        } else {
            printf("jobs: expected exactly one argument\n");
        }
    } else if (strcmp(*cmd_argv, "bg") == 0) {
        /* Check if bg has two arguments */
        if (argc == 2) {
            /* Convert the string to int */
            int jid = atoi(*(cmd_argv + 1));
            struct job *j = get_job_from_jid(jid);
            /* If the job does not exist, print no such job */
            if (j == NULL) {
                printf("bg %d: No such job\n", jid);
                return;
            }
            /* Sent signal to the process group*/
            if (killpg(get_process_pgid(jid), SIGCONT) == -1) {
                perror("bg: Error when calling killpg");
                exit(EXIT_FAILURE);
            }
            /* Set the status of the job to BACKGROUND*/
            j->status = BACKGROUND;
        } else {
            printf("bg: job id is missing\n");
        }
    } else if (strcmp(*cmd_argv, "kill") == 0) {
        /* Check if kill has two arguments */
        if (argc == 2) {
            /* Convert the string to int */
            int jid = atoi(*(cmd_argv + 1));
            struct job *j = get_job_from_jid(jid);
            /* If the job does not exist, print no such job */
            if (j == NULL) {
                printf("kill %d: No such job\n", jid);
            }
            /* Sent signal to the process group*/
            if (killpg(get_process_pgid(jid), SIGKILL) == -1) {
                perror("kill: Error when calling killpg");
                exit(EXIT_FAILURE);
            }
        } else {
            printf("kill: job id is missing\n");
        }
    } else if (strcmp(*cmd_argv, "stop") == 0) {
        /* Check if kill has two arguments */
        if (argc == 2) {
            /* Convert the string to int */
            int jid = atoi(*(cmd_argv + 1));
            struct job *j = get_job_from_jid(jid);
            /* If the job does not exist, print no such job */
            if (j == NULL) {
                printf("stop %d: No such job\n", jid);
            }
            /* Sent signal to the process group*/
            if (killpg(get_process_pgid(jid), SIGSTOP) == -1) {
                perror("stop: Error when calling killpg");
                exit(EXIT_FAILURE);
            }
        } else {
            printf("stop: job id is missing\n");
        }
    } else if (strcmp(*cmd_argv, "fg") == 0) {
        /* Check if kill has two arguments */
        if (argc == 2) {
            struct job *j = NULL;
            /* Convert the string to int */
            int jid = atoi(*(cmd_argv + 1));
            /* Get the job from jid */
            j = get_job_from_jid(jid);
            /* If the job does not exist, print no such job */
            if (j == NULL) {
                printf("job was not found\n");
            }

            /* Set the status of the job to FOREGROUND */
            j->status = FOREGROUND;

            /* Print the job to stdout */
            print_cmdline(j->pipe);
            printf("\n");
            fflush(stdout);

            /* Sent signal to the process group*/
            if (killpg(get_process_pgid(jid), SIGCONT) == -1) {
                perror("fg: Error when calling killpg");
                exit(EXIT_FAILURE);
            }

            /* Block the signal */
            signal_block(SIGCHLD);
            /* Give the terminal to the process group */
            termstate_give_terminal_to(NULL, get_process_pgid(jid));
            /* Wait until the job is done */
            wait_for_job(j);
            /* Give the terminal back to shell */
            termstate_give_terminal_back_to_shell();
            /* Unblock the signal */
            signal_unblock(SIGCHLD);
        } else {
            printf("fg: job id is missing\n");
        }
    } else if (strcmp(*cmd_argv, "history") == 0) {
        HISTORY_STATE *history = history_get_history_state();
        HIST_ENTRY **hist_list = history_list();
        for (int i = 0; i < history->length; i++) {
            printf("%d %s\n", (i + 1), hist_list[i]->line);
        }
    }
}

/* Execute the commands */
void execute(struct ast_command_line *cmdline) {
    /* Iterates through the command line to get the pipeline */
    for (struct list_elem *e = list_begin(&cmdline->pipes);
         e != list_end(&cmdline->pipes); e = list_next(e)) {
        /* Get the pipeline from the command line */
        struct ast_pipeline *pipe_line =
            list_entry(e, struct ast_pipeline, elem);

        /* Get the first command from the pipeline */
        struct ast_command *cmd = list_entry(list_begin(&pipe_line->commands),
                                             struct ast_command, elem);

        /* Check if the command is the build in function */
        if (is_built_in(cmd->argv[0])) {
            handle_build_in(cmd);
            break;
        }
        /* If the command is not build in, run the handle_pipeline function */
        handle_pipeline(pipe_line, cmd);
    }
}

void handle_pipeline(struct ast_pipeline *pipe_line, struct ast_command *cmd) {
    int pipe_counter = 0;
    pid_t pid = -1;

    /* Add a new job to the job list */
    struct job *j = add_job(pipe_line);
    /* Get the total number of commands in the pipeline */
    int total_commands = list_size(&pipe_line->commands);
    /* The total number of pipes should be one less than the number of total
     * commands */
    int total_pipes = total_commands - 1;
    /* Initialize the current command counter */
    int curr_cmd = 0;

    /* Create the necessary number of pipes */
    int fds[2 * total_pipes];
    for (int i = 0; i < total_pipes; i++) {
        /* Print error if piping failed */
        if (pipe2((fds + i * 2), O_CLOEXEC) == -1) {
            perror("Error when piping");
            exit(EXIT_FAILURE);
        }
    }

    /* ---------- Handle I/O redirections ---------- */
    /* Read input from iored_input file */
    if (j->pipe->iored_input != NULL) {
        freopen(j->pipe->iored_input, "r", stdin);
    }
    /* Write the last command to iored_output file */
    if (j->pipe->iored_output != NULL) {
        /* Check if it needs to be appended to the end of the file */
        if (j->pipe->append_to_output) {
            freopen(j->pipe->iored_output, "a", stdout);
        }
        /* Write the last command to iored_output file */
        else {
            freopen(j->pipe->iored_output, "w", stdout);
        }
        /* Check if stderr should be redirected as well */
        if (cmd->dup_stderr_to_stdout) {
            dup2(STDOUT_FILENO, STDERR_FILENO);
        }
    }
    /* ---------- Handle I/O redirections ---------- */

    /* ------------- Handle I/O Piping ------------- */
    pid_t pgid = -1;
    /* Iterate through the commands in the pipeline */
    for (struct list_elem *cmd_line = list_begin(&pipe_line->commands);
         cmd_line != list_end(&pipe_line->commands);
         cmd_line = list_next(cmd_line)) {
        struct ast_command *cmd =
            list_entry(cmd_line, struct ast_command, elem);

        /* Block the signal */
        signal_block(SIGCHLD);

        /* Put the commands into an array */
        char **p = cmd->argv; /* array of pointers to words
                            making up this command. */
        char *cmd_arg = *p;
        char *argv[10];
        int argc = 0;
        while (*p) {
            argv[argc] = *p++;
            argc++;
        }
        argv[argc] = NULL;

        /* Fork off a child process to execute each command in a pipeline */
        if ((pid = fork()) == 0) {
            /* Create a new process group if it is the first command */
            if (curr_cmd == 0) {
                setpgid(0, 0);
            }
            /* Set the remaining processes in the pipe to have the same pgid */
            else {
                setpgid(0, j->pgid);
            }

            /* Idetify the last command in the pipe */
            bool not_last = not_last_arg(curr_cmd, total_commands);

            /* Handle the child process after forking */
            handle_child_process(fds, not_last, total_pipes, pipe_counter,
                                 cmd_arg, argv);
        }

        /* Parent process */
        else {
            /* Parent's pid and pgid will be the same as its child pgid */
            if (curr_cmd == 0) {
                pgid = pid;
                j->pgid = pgid;
            }

            setpgid(pid, pgid);
            /* Add pid to the pid array in job */
            j->pid[curr_cmd] = pid;

            /* Update the number of alive process and the total process */
            j->num_processes_alive = j->num_processes_alive + 1;
            j->total_processes = j->total_processes + 1;

            /* Increment command and pipe counter */
            curr_cmd++;
            pipe_counter += 2;
        }
    }

    /* Close the opened pipe */
    for (int i = 0; i < total_pipes * 2; i++) {
        close(fds[i]);
    }

    /* Reset the file to the console after I/O redirection */
    freopen("/dev/tty", "w", stdout);
    freopen("/dev/tty", "r", stdin);
    dup2(STDERR_FILENO, STDERR_FILENO);

    /* Check if the program is executed in the background & */
    if (j->pipe->bg_job) {
        /* Set the status of the job to BG */
        j->status = BACKGROUND;
        /* Print the job running on the BG */
        printf("[%d] %d\n", j->jid, pid);
    } else {
        /* Give the terminal to the process group */
        termstate_give_terminal_to(NULL, pgid);
        /* Wait until the job is done */
        wait_for_job(j);
        /* Give the terminal back to shell */
        termstate_give_terminal_back_to_shell();
    }
    /* Unblock the signal */
    signal_unblock(SIGCHLD);
}

/* Handle the child process */
void handle_child_process(int fds[], bool not_last, int total_pipes,
                          int pipe_counter, char *cmd_arg, char **argv) {
    /* If it is not the first command, read from stdin */
    if (pipe_counter != 0) {
        if (dup2(fds[pipe_counter - 2], STDIN_FILENO) == -1) {
            perror("Error occurred at dup2()");
            exit(EXIT_FAILURE);
        }
    }
    /* If it is not the last command, write to stdout */
    if (not_last) {
        if (dup2(fds[pipe_counter + 1], STDOUT_FILENO) == -1) {
            perror("Error occurred at dup2()");
            exit(EXIT_FAILURE);
        }
    }
    /* Execute the command by replacing the current process */
    if (execvp(cmd_arg, argv) == -1) {
        perror("");
        exit(EXIT_FAILURE);
    }
}

/* Check whether this is the last argument in the command */
bool not_last_arg(int curr_cmd, int total_commands) {
    return curr_cmd != total_commands - 1;
}

/* The main function */
int main(int ac, char *av[]) {
    int opt;

    /* Process command-line arguments. See getopt(3) */
    while ((opt = getopt(ac, av, "h")) > 0) {
        switch (opt) {
            case 'h':
                usage(av[0]);
                break;
        }
    }

    list_init(&job_list);
    signal_set_handler(SIGCHLD, sigchld_handler);
    termstate_init();
    using_history();

    /* Read/eval loop. */
    for (;;) {
        /* Clean up the job list when the process is finished */
        struct job *j;
        for (struct list_elem *e = list_begin(&job_list);
             e != list_end(&job_list);) {
            j = list_entry(e, struct job, elem);
            if (j->num_processes_alive == 0) {
                e = list_remove(e);
                delete_job(j);
            } else {
                e = list_next(e);
            }
        }
        /* Do not output a prompt unless shell's stdin is a terminal */
        char *prompt = isatty(0) ? build_prompt() : NULL;
        char *cmdline = readline(prompt);
        free(prompt);

        if (cmdline == NULL) { /* User typed EOF */
            break;
        } else {
            add_history(cmdline);
            add_history_time(cmdline);
        }

        struct ast_command_line *cline = ast_parse_command_line(cmdline);
        free(cmdline);
        if (cline == NULL) /* Error in command line */
            continue;

        if (list_empty(&cline->pipes)) { /* User hit enter */
            ast_command_line_free(cline);
            continue;
        } else {
            execute(cline);
        }

        /* Output a representation of the entered command line (Useful when
         * debugging) */
        // ast_command_line_print(cline);
        // /* Free the command line.
        //  * This will free the ast_pipeline objects still contained
        //  * in the ast_command_line.  Once you implement a job list
        //  * that may take ownership of ast_pipeline objects that are
        //  * associated with jobs you will need to reconsider how you
        //  * manage the lifetime of the associated ast_pipelines.
        //  * Otherwise, freeing here will cause use-after-free errors.
        //  */
        // ast_command_line_free(cline);
    }
    return 0;
}