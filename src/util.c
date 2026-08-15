#include "shell.h"

int g_last_status = 0;
int g_shell_running = 1;
char *g_user = NULL;
char *g_host = NULL;

char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (!p) {
        fprintf(stderr, "mysh: out of memory\n");
        exit(1);
    }
    memcpy(p, s, n);
    return p;
}
