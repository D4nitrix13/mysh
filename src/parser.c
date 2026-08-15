#include "shell.h"

static int is_word_char(char c) {
    return c != ' ' && c != '\t' && c != '|' && c != '&' &&
           c != ';' && c != '<' && c != '>' && c != '\0';
}

static token_t *tokenize(const char *line, int *ntoks) {
    int cap = 32;
    token_t *toks = malloc(cap * sizeof(token_t));
    int n = 0;

    const char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (n >= cap - 2) {
            cap *= 2;
            toks = realloc(toks, cap * sizeof(token_t));
        }

        if (*p == '|') {
            toks[n].type = TOK_PIPE;
            toks[n].value = NULL;
            n++; p++;
        } else if (*p == '&') {
            toks[n].type = TOK_AND;
            toks[n].value = NULL;
            n++; p++;
        } else if (*p == ';') {
            toks[n].type = TOK_SEMI;
            toks[n].value = NULL;
            n++; p++;
        } else if (*p == '>' && *(p+1) == '>') {
            toks[n].type = TOK_REDIR_APP;
            toks[n].value = NULL;
            n++; p += 2;
        } else if (*p == '>') {
            toks[n].type = TOK_REDIR_OUT;
            toks[n].value = NULL;
            n++; p++;
        } else if (*p == '<') {
            toks[n].type = TOK_REDIR_IN;
            toks[n].value = NULL;
            n++; p++;
        } else if (isdigit((unsigned char)*p) && *(p+1) == '>' && *(p+2) == '>') {
            toks[n].type = TOK_REDIR_APPERR;
            toks[n].value = NULL;
            n++; p += 3;
        } else if (isdigit((unsigned char)*p) && *(p+1) == '>') {
            toks[n].type = TOK_REDIR_ERR;
            toks[n].value = NULL;
            n++; p += 2;
        } else {
            char buf[MAX_LINE];
            int bi = 0;
            int in_single = 0, in_double = 0;
            int in_subst = 0;
            int subst_depth = 0;

            while (*p && (in_single || in_double || in_subst || is_word_char(*p))) {
                if (*p == '\'' && !in_double) {
                    in_single = !in_single;
                    p++;
                } else if (*p == '"' && !in_single) {
                    in_double = !in_double;
                    p++;
                } else if (*p == '\\' && !in_single && *(p+1)) {
                    if (bi < MAX_LINE - 1) buf[bi++] = *p++;
                    if (bi < MAX_LINE - 1) buf[bi++] = *p;
                    p++;
                } else if (!in_single && !in_double && !in_subst &&
                           p[0] == '$' && p[1] == '(') {
                    in_subst = 1;
                    subst_depth = 1;
                    if (bi < MAX_LINE - 1) buf[bi++] = *p++;
                    if (bi < MAX_LINE - 1) buf[bi++] = *p++;
                } else if (!in_single && !in_double && in_subst) {
                    if (*p == '(') subst_depth++;
                    else if (*p == ')') {
                        subst_depth--;
                        if (subst_depth == 0) in_subst = 0;
                    }
                    if (bi < MAX_LINE - 1) buf[bi++] = *p;
                    p++;
                } else {
                    if (bi < MAX_LINE - 1) buf[bi++] = *p;
                    p++;
                }
            }
            buf[bi] = '\0';

            toks[n].type = TOK_WORD;
            toks[n].value = xstrdup(buf);
            n++;
        }
    }

    toks[n].type = TOK_EOF;
    toks[n].value = NULL;
    *ntoks = n;
    return toks;
}

static void free_tokens(token_t *toks, int ntoks) {
    for (int i = 0; i < ntoks; i++) {
        free(toks[i].value);
    }
    free(toks);
}

static simple_cmd_t *new_cmd(void) {
    simple_cmd_t *c = calloc(1, sizeof(simple_cmd_t));
    if (!c) return NULL;
    c->argv = calloc(MAX_ARGS, sizeof(char *));
    c->redirs = calloc(MAX_REDIRS, sizeof(redir_t));
    if (!c->argv || !c->redirs) {
        free(c->argv); free(c->redirs); free(c);
        return NULL;
    }
    return c;
}

static void free_cmd(simple_cmd_t *c) {
    if (!c) return;
    for (int i = 0; i < c->argc; i++) free(c->argv[i]);
    for (int i = 0; i < c->nredirs; i++) free(c->redirs[i].target);
    free(c->argv);
    free(c->redirs);
    free(c);
}

void pipeline_free(pipeline_t *p) {
    if (!p) return;
    for (int i = 0; i < p->ncmds; i++) {
        free_cmd(p->cmds[i]);
    }
    free(p->cmds);
    free(p);
}

static void add_arg(simple_cmd_t *c, const char *s) {
    if (c->argc >= MAX_ARGS - 1) return;
    char *expanded = expand_string(s);
    c->argv[c->argc++] = expanded;
}

static void add_redir(simple_cmd_t *c, token_type_t type, const char *target) {
    if (c->nredirs >= MAX_REDIRS) return;
    redir_t *r = &c->redirs[c->nredirs++];
    r->type = type;
    r->target = xstrdup(target);
    if (type == TOK_REDIR_IN) r->fd = STDIN_FILENO;
    else if (type == TOK_REDIR_OUT || type == TOK_REDIR_APP) r->fd = STDOUT_FILENO;
    else r->fd = STDERR_FILENO;
}

pipeline_t *parse_line(const char *line) {
    char *braced = expand_braces_line(line);
    if (!braced) return NULL;
    int ntoks;
    token_t *toks = tokenize(braced, &ntoks);
    free(braced);

    pipeline_t *p = calloc(1, sizeof(pipeline_t));
    p->cmds = calloc(MAX_CMDS, sizeof(simple_cmd_t *));
    p->cmds[0] = new_cmd();
    p->ncmds = 0;

    int cur = 0;
    int i = 0;
    int has_error = 0;
    while (i < ntoks && toks[i].type != TOK_EOF && !has_error) {
        token_t *t = &toks[i];

        switch (t->type) {
            case TOK_WORD:
                add_arg(p->cmds[cur], t->value);
                i++;
                break;
            case TOK_PIPE:
                if (p->cmds[cur]->argc == 0 && p->cmds[cur]->nredirs == 0) {
                    fprintf(stderr, "mysh: syntax error: empty command before '|'\n");
                    has_error = 1;
                    break;
                }
                cur++;
                if (cur >= MAX_CMDS) {
                    fprintf(stderr, "mysh: too many commands in pipeline\n");
                    has_error = 1;
                    break;
                }
                p->cmds[cur] = new_cmd();
                i++;
                break;
            case TOK_AND:
                p->background = 1;
                i++;
                break;
            case TOK_SEMI:
                i++;
                break;
            case TOK_REDIR_IN:
            case TOK_REDIR_OUT:
            case TOK_REDIR_APP:
            case TOK_REDIR_ERR:
            case TOK_REDIR_APPERR: {
                token_type_t rt = t->type;
                i++;
                if (i >= ntoks || toks[i].type != TOK_WORD) {
                    fprintf(stderr, "mysh: syntax error: expected filename after redirection\n");
                    has_error = 1;
                    break;
                }
                add_redir(p->cmds[cur], rt, toks[i].value);
                i++;
                break;
            }
            default:
                i++;
                break;
        }
    }

    if (has_error || (p->cmds[cur]->argc == 0 && p->cmds[cur]->nredirs == 0)) {
        free_tokens(toks, ntoks);
        pipeline_free(p);
        return NULL;
    }

    p->ncmds = cur + 1;
    for (int j = 0; j < p->ncmds; j++) {
        p->cmds[j]->argv[p->cmds[j]->argc] = NULL;
    }

    free_tokens(toks, ntoks);
    return p;
}
