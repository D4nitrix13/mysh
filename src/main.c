#include "shell.h"

static char *make_prompt(void) {
    static char prompt[1024];
    char *cwd = getcwd(NULL, 0);
    if (!cwd) cwd = xstrdup("?");

    char *home = getenv("HOME");
    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        char rest[512];
        snprintf(rest, sizeof(rest), "~%s", cwd + strlen(home));
        free(cwd);
        cwd = xstrdup(rest);
    }

    const char *user = g_user ? g_user : "user";
    const char *host = g_host ? g_host : "host";

    snprintf(prompt, sizeof(prompt),
             "\001\033[1;32m\002%s\001\033[0m\002@"
             "\001\033[1;32m\002%s\001\033[0m\002:"
             "\001\033[1;34m\002%s\001\033[0m\002$ ",
             user, host, cwd);
    free(cwd);
    return prompt;
}

static void setup_env(void) {
    g_user = getenv("USER");
    if (!g_user) g_user = "user";
    g_host = getenv("HOSTNAME");
    if (!g_host) g_host = "linux";

    setenv("SHELL", "/bin/mysh", 0);
    setenv("PS1", "$ ", 0);

    char level[16];
    snprintf(level, sizeof(level), "%d", (int)getpid());
    setenv("MYSH_PID", level, 1);
}

static int execute_segment(char *seg) {
    pipeline_t *p = parse_line(seg);
    if (!p) return 2;
    int s = execute_pipeline(p);
    pipeline_free(p);
    return s;
}

static int at_op(char *p) {
    return (*p == '&' && *(p+1) == '&') ||
           (*p == '|' && *(p+1) == '|') ||
           (*p == ';');
}

static void skip_quoted(char **pp) {
    char *p = *pp;
    if (*p == '\\' && p[1]) { *pp = p + 2; return; }
    if (*p == '"' || *p == '\'') {
        char q = *p++;
        while (*p && *p != q) {
            if (*p == '\\' && q == '"' && p[1]) p++;
            p++;
        }
        if (*p == q) p++;
    }
    *pp = p;
}

typedef struct {
    char *text;
    int pre_op;
} segment_t;

#define MAX_SEGS 64

static int split_compound(char *line, segment_t *segs) {
    int n = 0;
    char *p = line;
    int pending_op = 0;

    while (*p && n < MAX_SEGS) {
        char *seg_start = p;

        while (*p) {
            skip_quoted(&p);
            if (at_op(p)) break;
            p++;
        }

        if (!*p) {
            segs[n].text = seg_start;
            segs[n].pre_op = pending_op;
            n++;
            return n;
        }

        if (*p == ';') {
            *p = '\0';
            segs[n].text = seg_start;
            segs[n].pre_op = pending_op;
            n++;
            pending_op = 3;
            p++;
        } else if (*p == '&' && *(p+1) == '&') {
            *p = '\0'; *(p+1) = '\0';
            segs[n].text = seg_start;
            segs[n].pre_op = pending_op;
            n++;
            pending_op = 1;
            p += 2;
        } else if (*p == '|' && *(p+1) == '|') {
            *p = '\0'; *(p+1) = '\0';
            segs[n].text = seg_start;
            segs[n].pre_op = pending_op;
            n++;
            pending_op = 2;
            p += 2;
        }
        while (*p == ' ' || *p == '\t') p++;
    }
    return n;
}

static int execute_compound(char *line) {
    segment_t segs[MAX_SEGS];
    int n = split_compound(line, segs);

    int status = 0;
    for (int i = 0; i < n; i++) {
        int run = (segs[i].pre_op == 0) ||
                  (segs[i].pre_op == 3) ||
                  (segs[i].pre_op == 1 && status == 0) ||
                  (segs[i].pre_op == 2 && status != 0);
        if (run) status = execute_segment(segs[i].text);
    }
    return status;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    setup_env();

    using_history();
    char *histfile = getenv("HISTFILE");
    if (!histfile) histfile = xstrdup(".mysh_history");
    else histfile = xstrdup(histfile);

    if (access(histfile, R_OK) == 0) {
        read_history(histfile);
    }

    printf("\033[1;36mmysh\033[0m — minimal POSIX shell (type 'help' for commands)\n");
    printf("press Ctrl+D to exit\n\n");

    while (g_shell_running) {
        reap_zombies();

        char *line = readline(make_prompt());
        if (!line) {
            printf("\n");
            break;
        }

        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (*trimmed == '\0') {
            free(line);
            continue;
        }

        add_history(line);
        append_history(1, histfile);

        g_last_status = execute_compound(trimmed);
        free(line);
    }

    write_history(histfile);
    free(histfile);
    printf("\033[1;36mmysh\033[0m: goodbye\n");
    return g_last_status;
}
