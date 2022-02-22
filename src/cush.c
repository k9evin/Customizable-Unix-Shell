/*
 * cush - the customizable shell.
 *
 * Developed by Godmar Back for CS 3214 Summer 2020
 * Virginia Tech.  Augmented to use posix_spawn in Fall 2021.
 */
#define _GNU_SOURCE 1
#include <assert.h>
#include <libgen.h>
#include <limits.h>
#include <readline/readline.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* Since the handed out code contains a number of unused functions. */
#pragma GCC diagnostic ignored "-Wunused-function"

#include "shell-ast.h"
#include "signal_support.h"
#include "termstate_management.h"
#include "utils.h"

void cleanup(void);
static void handle_child_status(pid_t pid, int status);
int get_ppid(int jid);
bool is_built_in(char *cmd);
void handle_build_in(struct ast_command *cmd);
void execute(struct ast_command_line *cmd_line);

static void usage(char *progname)
{
    printf(
        "Usage: %s -h\n"
        " -h            print this help\n",
        progname);

    exit(EXIT_SUCCESS);
}

/* Build a prompt */
static char *build_prompt(void)
{
    char hostn[1204] = "";
    gethostname(hostn, sizeof(hostn));
    printf("<%s@%s %s>$ ", getenv("LOGNAME"), hostn, basename(getenv("PWD")));
    return strdup("");
}

enum job_status
{
    FOREGROUND,    /* job is running in foreground.  Only one job can be
                      in the foreground state. */
    BACKGROUND,    /* job is running in background */
    STOPPED,       /* job is stopped via SIGSTOP */
    NEEDSTERMINAL, /* job is stopped because it was a background job
                      and requires exclusive terminal access */
};

struct job
{
    struct list_elem elem; /* Link element for jobs list. */
    struct ast_pipeline
        *pipe;               /* The pipeline of commands this job represents */
    int jid;                 /* Job id. */
    int pid;                 /* Process pid. */
    enum job_status status;  /* Job status. */
    int num_processes_alive; /* The number of processes that we know to be alive
                              */
    struct termios
        saved_tty_state; /* The state of the terminal when this job was
                            stopped after having been in foreground */
};

/* Utility functions for job list management.
 * We use 2 data structures:
 * (a) an array jid2job to quickly find a job based on its id
 * (b) a linked list to support iteration
 */
#define MAXJOBS (1 << 16)
static struct list job_list;
static struct job *jid2job[MAXJOBS];

static int stopped_processes[MAXJOBS];
static int total_stopped_processes = 0;

/* Return job corresponding to jid */
static struct job *get_job_from_jid(int jid)
{
    if (jid > 0 && jid < MAXJOBS && jid2job[jid] != NULL)
        return jid2job[jid];

    return NULL;
}

/* Return job corresponding to pid */
static struct job *get_job_from_pid(pid_t pid)
{
    struct job *j;
    for (struct list_elem *e = list_begin(&job_list); e != list_end(&job_list);
         e = list_next(e))
    {
        j = list_entry(e, struct job, elem);
        if (j->pid == pid)
        {
            return j;
        }
    }
    return NULL;
}

// /* Return the ppid corresponding to jid */
// int get_ppid(int jid) {
//     struct job *j;
//     for (struct list_elem *e = list_begin(&job_list); e !=
//     list_end(&job_list); e = list_next(e)) {
//         j = list_entry(e, struct job, elem);
//         if (j->jid == jid) return j->ppid;
//     }
//     return -1;
// }

/* Add a new job to the job list */
static struct job *add_job(struct ast_pipeline *pipe)
{
    struct job *job = malloc(sizeof *job);
    job->pid = 0;
    job->pipe = pipe;
    job->num_processes_alive = 0;
    if (pipe->bg_job)
    {
        job->status = BACKGROUND;
    }
    else
    {
        job->status = FOREGROUND;
    }
    list_push_back(&job_list, &job->elem);
    for (int i = 1; i < MAXJOBS; i++)
    {
        if (jid2job[i] == NULL)
        {
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
static void delete_job(struct job *job)
{
    int jid = job->jid;
    assert(jid != -1);
    jid2job[jid]->jid = -1;
    jid2job[jid] = NULL;
    ast_pipeline_free(job->pipe);
    free(job);
}

static const char *get_status(enum job_status status)
{
    switch (status)
    {
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
static void print_cmdline(struct ast_pipeline *pipeline)
{
    struct list_elem *e = list_begin(&pipeline->commands);
    for (; e != list_end(&pipeline->commands); e = list_next(e))
    {
        struct ast_command *cmd = list_entry(e, struct ast_command, elem);
        if (e != list_begin(&pipeline->commands))
            printf("| ");
        char **p = cmd->argv;
        printf("%s", *p++);
        while (*p)
            printf(" %s", *p++);
    }
}

/* Print a job */
static void print_job(struct job *job)
{
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
static void sigchld_handler(int sig, siginfo_t *info, void *_ctxt)
{
    pid_t child;
    int status;

    assert(sig == SIGCHLD);

    while ((child = waitpid(-1, &status, WUNTRACED | WNOHANG)) > 0)
    {
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
static void wait_for_job(struct job *job)
{
    assert(signal_is_blocked(SIGCHLD));

    while (job->status == FOREGROUND && job->num_processes_alive > 0)
    {
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

static void handle_child_status(pid_t pid, int status)
{
    assert(signal_is_blocked(SIGCHLD));

    /* To be implemented.
     * Step 1. Given the pid, determine which job this pid is a part of
     *         (how to do this is not part of the provided code.)
     * Step 2. Determine what status change occurred using the
     *         WIF*() macros.
     * Step 3. Update the job status accordingly, and adjust
     *         num_processes_alive if appropriate.
     *         If a process was stopped, save the terminal state.
     */

    struct job *job = get_job_from_pid(pid);

    if (WIFSTOPPED(status))
    {
        // int stop_status = WSTOPSIG(status);
        job->status = STOPPED;
        int stop_signal = WSTOPSIG(status);
        if (job->status = FOREGROUND)
        {
            termstate_save(&job->saved_tty_state);
            print_job(job);
        }
        else
        {
            if (stop_signal == SIGTTIN || stop_signal == SIGTTOU)
            {
                job->status = NEEDSTERMINAL;
            }
            else
            {
                print_job(job);
            }
            add_stopped_process(job);
        }
    }
    else if (WIFEXITED(status))
    {
        job->num_processes_alive--;
    }
    else if (WIFSIGNALED(status))
    {
        int signal_status = WTERMSIG(status);
        job->num_processes_alive--;
        if (signal_status == 6)
        {
            utils_error("aborted\n");
        }
        else if (signal_status == 8)
        {
            utils_error("floating point exception\n");
        }
        else if (signal_status == 9)
        {
            utils_error("killed\n");
        }
        else if (signal_status == 11)
        {
            utils_error("segmentation fault\n");
        }
        else if (signal_status == 15)
        {
            utils_error("terminated\n");
        }
    }
    termstate_give_terminal_back_to_shell();
}

/* Check if the command is a build in function */
bool is_built_in(char *cmd)
{
    return (strcmp(cmd, "jobs") == 0 || strcmp(cmd, "fg") == 0 ||
            strcmp(cmd, "bg") == 0 || strcmp(cmd, "stop") == 0 ||
            strcmp(cmd, "kill") == 0 || strcmp(cmd, "exit") == 0);
}

/* Handle the build in function */
void handle_build_in(struct ast_command *cmd)
{
    char **cmd_argv = cmd->argv;
    int argc = 0;
    while (*(cmd_argv + argc) != NULL)
    {
        argc++;
    }

    if (strcmp(*cmd_argv, "exit") == 0)
    {
        exit(0);
    }
    else if (strcmp(*cmd_argv, "jobs") == 0)
    {
        if (argc == 1)
        {
            struct job *j;
            for (struct list_elem *e = list_begin(&job_list);
                 e != list_end(&job_list); e = list_next(e))
            {
                j = list_entry(e, struct job, elem);
                print_job(j);
            }
        }
        else
        {
            printf("jobs: expected only one argument\n");
        }
    }
    else if (strcmp(*cmd_argv, "bg") == 0)
    {
        if (argc == 1)
        {
            printf("bg: job id is missing\n");
        }
        else if (argc == 2)
        {
            char *arguments[MAX_CAPACITY];
            int index = 0;
            while (*cmd_argv)
            {
                arguments[index] = *cmd_argv++;
                index++;
            }
            arguments[index] = NULL;
            int jid = atoi(arguments[1]);
            int ppid = get_ppid(jid);
            struct job *j = get_job_from_pid(ppid);
            j->status = BACKGROUND;
            killpg(ppid, SIGCONT);
        }
    }
    else if (strcmp(*cmd_argv, "kill") == 0)
    {
        if (argc == 2)
        {
            int jid = atoi(*(cmd_argv + 1));
            struct job *j = get_job_from_jid(jid);
            if (j == NULL)
            {
                printf("%d was not found\n", jid);
            }
            else
            {
                int return_status = killpg(j->pid, SIGSTOP);
                if (return_status >= 0)
                {
                    j->status =STOPPED;
                    termstate_save(&j->saved_tty_state);
                }
                else{
                    print("%d was not stopped\n", jid);
                }
            }
        }
        else{
            printf("Incorrect number of arguments for command 'kill'\n");
        }
    }
    else if(strcmp(*cmd_argv, "stop") == 0){
        if (argc == 2){
            int jid = atoi(*(cmd_argv + 1));
            struct job* j = get_job_from_jid(jid);
            if (j == NULL){
                printf("%d was not found\n", jid);
            }
            else{
                int return_status = killpg(j->pid, SIGSTOP);
                if (return_status >= 0)
                {
                    j->status = STOPPED;
                    termstate_save(&j->saved_tty_state);
                }
                else{
                    printf("%d was not stopped\n", jid);
                }
            }
        }
        else{
            printf("Incorrect number of arguments for command 'stop'\n");
        }
    }
    else if (strcmp(*cmd_argv, "fg") == 0)
    {
        struct job* j = NULL;
        int jid = 0;

        if (argc == 1)
        {
            if (total_stopped_processes > 0)
            {
                j = get_job_from_jid(stopped_processes[total_stopped_processes - 1]);
                jid = j->jid;
            }
            else {
                printf("No stopped job\n");
                return;
            }
        }
        else if (argc == 2)
        {
            jid = atoi(*(cmd_argv + 1));
            j = get_job_from_jid(jid);
            if (j == NULL)
            {
                printf("job was not found\n");
                return;
            }
            if (j->status == FOREGROUND){
                printf("%d is running\n", jid);
                return;
            }
        }
        else{
            printf("Incorrect number of arguments for command 'fg'\n");
            return;
        }
        if (j == NULL)
        {
            printf("No stopped job was founded\n");
            return;
        }
        else{
            signal_block(SIGCHLD);
            int return_status = killpg(j->pid, SIGCONT);
            if(return_status >= 0)
            {
                
            }
        }
    }
}

/* Command execution function */
void execute(struct ast_command_line *cmd_line)
{
    for (struct list_elem *e = list_begin(&cmd_line->pipes);
         e != list_end(&cmd_line->pipes); e = list_next(e))
    {
        // pid_t pid = -1;
        // int counter = 0;
        struct ast_pipeline *pipe_line =
            list_entry(e, struct ast_pipeline, elem);
        struct list_elem *cmd_list = list_begin(&pipe_line->commands);
        struct ast_command *cmd =
            list_entry(cmd_list, struct ast_command, elem);

        if (is_built_in(cmd->argv[0]))
        {
            handle_build_in(cmd);
            break;
        }
        add_job(pipe_line);
    }
}

/* Clean up the job list if the process is finished */
void cleanup()
{
    struct job *j;
    for (struct list_elem *e = list_begin(&job_list);
         e != list_end(&job_list);)
    {
        j = list_entry(e, struct job, elem);
        if (j->finished)
        {
            delete_job(j);
            e = list_remove(e);
        }
        else
        {
            e = list_next(e);
        }
    }
}

int main(int ac, char *av[])
{
    int opt;

    /* Process command-line arguments. See getopt(3) */
    while ((opt = getopt(ac, av, "h")) > 0)
    {
        switch (opt)
        {
        case 'h':
            usage(av[0]);
            break;
        }
    }

    list_init(&job_list);
    signal_set_handler(SIGCHLD, sigchld_handler);
    termstate_init();

    /* Read/eval loop. */
    for (;;)
    {
        cleanup();
        /* Do not output a prompt unless shell's stdin is a terminal */
        char *prompt = isatty(0) ? build_prompt() : NULL;
        char *cmdline = readline(prompt);
        free(prompt);

        if (cmdline == NULL) /* User typed EOF */
            break;

        struct ast_command_line *cline = ast_parse_command_line(cmdline);
        free(cmdline);
        if (cline == NULL) /* Error in command line */
            continue;

        if (list_empty(&cline->pipes))
        { /* User hit enter */
            ast_command_line_free(cline);
            continue;
        }

        ast_command_line_print(cline); /* Output a representation of
                                          the entered command line */

        /* Free the command line.
         * This will free the ast_pipeline objects still contained
         * in the ast_command_line.  Once you implement a job list
         * that may take ownership of ast_pipeline objects that are
         * associated with jobs you will need to reconsider how you
         * manage the lifetime of the associated ast_pipelines.
         * Otherwise, freeing here will cause use-after-free errors.
         */
        ast_command_line_free(cline);
        continue;
    }
    else
    {
        execute(cline);
    }

    // ast_command_line_print(cline); /* Output a representation of
    //                                   the entered command line */

    /* Free the command line.
     * This will free the ast_pipeline objects still contained
     * in the ast_command_line.  Once you implement a job list
     * that may take ownership of ast_pipeline objects that are
     * associated with jobs you will need to reconsider how you
     * manage the lifetime of the associated ast_pipelines.
     * Otherwise, freeing here will cause use-after-free errors.
     */
    ast_command_line_free(cline);
}
