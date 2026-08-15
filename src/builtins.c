#include "shell.h"

static int builtin_cd(simple_cmd_t *cmd) {
    char *dir = NULL;
    if (cmd->argc < 2) {
        dir = getenv("HOME");
        if (!dir) dir = "/";
    } else if (strcmp(cmd->argv[1], "-") == 0) {
        dir = getenv("OLDPWD");
        if (!dir) {
            fprintf(stderr, "mysh: cd: OLDPWD not set\n");
            return 1;
        }
        printf("%s\n", dir);
    } else {
        dir = cmd->argv[1];
    }

    char *oldpwd = getcwd(NULL, 0);

    if (chdir(dir) != 0) {
        fprintf(stderr, "mysh: cd: %s: %s\n", dir, strerror(errno));
        free(oldpwd);
        return 1;
    }

    if (oldpwd) {
        setenv("OLDPWD", oldpwd, 1);
        free(oldpwd);
    }

    char *newpwd = getcwd(NULL, 0);
    if (newpwd) {
        setenv("PWD", newpwd, 1);
        free(newpwd);
    }

    return 0;
}

static int builtin_exit(simple_cmd_t *cmd) {
    int code = g_last_status;
    if (cmd->argc >= 2) {
        code = atoi(cmd->argv[1]);
    }
    g_shell_running = 0;
    return code;
}

static int builtin_pwd(simple_cmd_t *cmd) {
    (void)cmd;
    char *cwd = getcwd(NULL, 0);
    if (!cwd) {
        perror("mysh: pwd");
        return 1;
    }
    printf("%s\n", cwd);
    free(cwd);
    return 0;
}

static int builtin_echo(simple_cmd_t *cmd) {
    int newline = 1;
    int start = 1;
    if (cmd->argc >= 2 && strcmp(cmd->argv[1], "-n") == 0) {
        newline = 0;
        start = 2;
    }
    for (int i = start; i < cmd->argc; i++) {
        if (i > start) putchar(' ');
        fputs(cmd->argv[i], stdout);
    }
    if (newline) putchar('\n');
    return 0;
}

static int builtin_export(simple_cmd_t *cmd) {
    extern char **environ;
    if (cmd->argc < 2) {
        for (char **e = environ; *e; e++) printf("%s\n", *e);
        return 0;
    }
    for (int i = 1; i < cmd->argc; i++) {
        char *eq = strchr(cmd->argv[i], '=');
        if (eq) {
            *eq = '\0';
            if (setenv(cmd->argv[i], eq + 1, 1) != 0) {
                fprintf(stderr, "mysh: export: %s\n", strerror(errno));
                return 1;
            }
        } else if (getenv(cmd->argv[i]) == NULL) {
            setenv(cmd->argv[i], "", 1);
        }
    }
    return 0;
}

static int builtin_unset(simple_cmd_t *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "mysh: unset: usage: unset NAME\n");
        return 1;
    }
    for (int i = 1; i < cmd->argc; i++) {
        unsetenv(cmd->argv[i]);
    }
    return 0;
}

static int builtin_env(simple_cmd_t *cmd) {
    (void)cmd;
    extern char **environ;
    for (char **e = environ; *e; e++) printf("%s\n", *e);
    return 0;
}

static int builtin_help(simple_cmd_t *cmd) {
    (void)cmd;
    printf("mysh — built-in commands:\n");
    printf("  cd [dir]          Change directory (default: $HOME)\n");
    printf("  exit [code]       Exit the shell with status code\n");
    printf("  pwd               Print working directory\n");
    printf("  echo [-n] [args]  Print arguments\n");
    printf("  export NAME=VAL   Set env variable (no args: list all)\n");
    printf("  unset NAME        Remove env variable\n");
    printf("  env               Print environment\n");
    printf("  history           Show command history\n");
    printf("  type <cmd>        Show if command is builtin or external\n");
    printf("  help              Show this help\n");
    printf("\nFeatures:\n");
    printf("  Pipes:        cmd1 | cmd2 | cmd3\n");
    printf("  Redirect:     > file, >> file, < file, 2>, 2>>\n");
    printf("  Background:   cmd &\n");
    printf("  Conditional:  cmd1 && cmd2, cmd1 || cmd2\n");
    printf("  Sequence:     cmd1 ; cmd2 ; cmd3\n");
    printf("  Variables:    $NAME or ${NAME}\n");
    printf("  Quotes:       \"...\" and '...'\n");
    printf("  Tilde:        ~ expands to $HOME\n");
    return 0;
}

static int builtin_history_cmd(simple_cmd_t *cmd) {
    (void)cmd;
    HIST_ENTRY **list = history_list();
    if (!list) return 0;
    for (int i = 0; list[i]; i++) {
        printf("%5d  %s\n", i + history_base, list[i]->line);
    }
    return 0;
}

static int builtin_type(simple_cmd_t *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "mysh: type: usage: type <command>\n");
        return 1;
    }
    const char *name = cmd->argv[1];
    if (is_builtin(name)) {
        printf("%s is a shell builtin\n", name);
        return 0;
    }
    char *path = getenv("PATH");
    if (!path) path = "/usr/bin:/bin";
    char *pathdup = xstrdup(path);
    char *save = NULL;
    char *dir = strtok_r(pathdup, ":", &save);
    while (dir) {
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir, name);
        if (access(full, X_OK) == 0) {
            printf("%s is %s\n", name, full);
            free(pathdup);
            return 0;
        }
        dir = strtok_r(NULL, ":", &save);
    }
    free(pathdup);
    fprintf(stderr, "mysh: type: %s: not found\n", name);
    return 1;
}

static int builtin_clear(simple_cmd_t *cmd) {
    (void)cmd;
    printf("\033[2J\033[H");
    return 0;
}

static struct {
    const char *name;
    int (*fn)(simple_cmd_t *);
} builtin_table[] = {
    {"cd",     builtin_cd},
    {"exit",   builtin_exit},
    {"pwd",    builtin_pwd},
    {"echo",   builtin_echo},
    {"export", builtin_export},
    {"unset",  builtin_unset},
    {"env",    builtin_env},
    {"help",   builtin_help},
    {"history",builtin_history_cmd},
    {"type",   builtin_type},
    {"clear",  builtin_clear},
    {NULL, NULL}
};

int is_builtin(const char *name) {
    if (!name) return 0;
    for (int i = 0; builtin_table[i].name; i++) {
        if (strcmp(name, builtin_table[i].name) == 0) return 1;
    }
    return 0;
}

int run_builtin(simple_cmd_t *cmd) {
    if (!cmd || cmd->argc == 0) return 0;
    for (int i = 0; builtin_table[i].name; i++) {
        if (strcmp(cmd->argv[0], builtin_table[i].name) == 0) {
            return builtin_table[i].fn(cmd);
        }
    }
    return 1;
}
