#include "shell.h"
#include <glob.h>

static int has_glob(const char *s) {
    for (; *s; s++) {
        if (*s == '*' || *s == '?' || *s == '[') return 1;
    }
    return 0;
}

static void expand_glob_argv(simple_cmd_t *cmd) {
    if (!cmd || cmd->argc == 0) return;

    char **new_argv = calloc(MAX_ARGS, sizeof(char *));
    int new_argc = 0;

    for (int i = 0; i < cmd->argc; i++) {
        if (!has_glob(cmd->argv[i])) {
            new_argv[new_argc++] = xstrdup(cmd->argv[i]);
            continue;
        }

        glob_t g;
        memset(&g, 0, sizeof(g));
        int rc = glob(cmd->argv[i], 0, NULL, &g);

        if (rc == 0 && g.gl_pathc > 0) {
            for (size_t j = 0; j < g.gl_pathc; j++) {
                if (new_argc < MAX_ARGS - 1) {
                    new_argv[new_argc++] = xstrdup(g.gl_pathv[j]);
                }
            }
        } else {
            if (new_argc < MAX_ARGS - 1) {
                new_argv[new_argc++] = xstrdup(cmd->argv[i]);
            }
        }
        globfree(&g);
    }

    new_argv[new_argc] = NULL;

    for (int i = 0; i < cmd->argc; i++) free(cmd->argv[i]);
    free(cmd->argv);

    cmd->argv = new_argv;
    cmd->argc = new_argc;
}

void reap_zombies(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            fprintf(stderr, "[bg %d] done, exit=%d\n", pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            fprintf(stderr, "[bg %d] killed by signal %d\n", pid, WTERMSIG(status));
        }
    }
}

static int apply_redirs(simple_cmd_t *cmd) {
    for (int i = 0; i < cmd->nredirs; i++) {
        redir_t *r = &cmd->redirs[i];
        int fd;
        if (r->type == TOK_REDIR_IN) {
            fd = open(r->target, O_RDONLY);
        } else if (r->type == TOK_REDIR_OUT || r->type == TOK_REDIR_ERR) {
            int flags = O_WRONLY | O_CREAT |
                        ((r->type == TOK_REDIR_OUT) ? O_TRUNC : O_TRUNC);
            fd = open(r->target, flags, 0644);
        } else {
            fd = open(r->target, O_WRONLY | O_CREAT | O_APPEND, 0644);
        }
        if (fd < 0) {
            fprintf(stderr, "mysh: %s: %s\n", r->target, strerror(errno));
            return -1;
        }
        if (dup2(fd, r->fd) < 0) {
            perror("mysh: dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

static int exec_simple(simple_cmd_t *cmd) {
    if (cmd->argc == 0) return 0;

    if (apply_redirs(cmd) < 0) return 1;

    if (is_builtin(cmd->argv[0])) {
        return run_builtin(cmd);
    }

    execvp(cmd->argv[0], cmd->argv);
    fprintf(stderr, "mysh: %s: command not found\n", cmd->argv[0]);
    return 127;
}

int execute_pipeline(pipeline_t *p) {
    if (!p || p->ncmds == 0) return 0;

    for (int i = 0; i < p->ncmds; i++) {
        expand_glob_argv(p->cmds[i]);
    }

    int n = p->ncmds;
    int is_single_no_redir = (n == 1 && p->cmds[0]->nredirs == 0);

    if (is_single_no_redir && !p->background &&
        p->cmds[0]->argc > 0 && is_builtin(p->cmds[0]->argv[0])) {
        return run_builtin(p->cmds[0]);
    }

    int pipes[MAX_CMDS - 1][2];
    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("mysh: pipe");
            return 1;
        }
    }

    pid_t pids[MAX_CMDS];
    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("mysh: fork");
            return 1;
        }
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);

            if (i > 0) {
                dup2(pipes[i-1][0], STDIN_FILENO);
            }
            if (i < n - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }
            for (int j = 0; j < n - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            int rc = exec_simple(p->cmds[i]);
            exit(rc);
        }
        pids[i] = pid;
    }

    for (int i = 0; i < n - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    if (p->background) {
        printf("[bg] pid %d\n", pids[n-1]);
        return 0;
    }

    int status = 0;
    for (int i = 0; i < n; i++) {
        int s;
        while (waitpid(pids[i], &s, 0) < 0 && errno == EINTR);
        if (i == n - 1) {
            if (WIFEXITED(s)) status = WEXITSTATUS(s);
            else if (WIFSIGNALED(s)) status = 128 + WTERMSIG(s);
        }
    }

    reap_zombies();
    return status;
}
