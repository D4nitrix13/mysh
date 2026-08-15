#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_LINE 4096
#define MAX_ARGS 256
#define MAX_CMDS 32
#define MAX_REDIRS 16
#define MAX_NAME 128

typedef enum {
    TOK_WORD,
    TOK_PIPE,
    TOK_REDIR_IN,
    TOK_REDIR_OUT,
    TOK_REDIR_APP,
    TOK_REDIR_ERR,
    TOK_REDIR_APPERR,
    TOK_AND,
    TOK_SEMI,
    TOK_EOF,
    TOK_ERROR
} token_type_t;

typedef struct {
    token_type_t type;
    char *value;
} token_t;

typedef struct {
    token_type_t type;
    int fd;
    char *target;
} redir_t;

typedef struct {
    char **argv;
    int argc;
    redir_t *redirs;
    int nredirs;
} simple_cmd_t;

typedef struct {
    simple_cmd_t **cmds;
    int ncmds;
    int background;
} pipeline_t;

extern int g_last_status;
extern int g_shell_running;
extern char *g_user;
extern char *g_host;

char *xstrdup(const char *s);
pipeline_t *parse_line(const char *line);
void pipeline_free(pipeline_t *p);

int execute_pipeline(pipeline_t *p);
void reap_zombies(void);

int is_builtin(const char *name);
int run_builtin(simple_cmd_t *cmd);

char *expand_string(const char *s);
char *expand_braces_line(const char *line);
char *strip_quotes(const char *s);

#endif
