#include "shell.h"

static char *run_subst(const char *cmd);

static int has_unquoted_brace(const char *s) {
    int in_single = 0, in_double = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '\\' && *(p+1) && !in_single) { p++; continue; }
        if (!in_double && *p == '\'') in_single = !in_single;
        else if (!in_single && *p == '"') in_double = !in_double;
        else if (!in_single && !in_double && *p == '{') return 1;
    }
    return 0;
}

static const char *find_matching_brace(const char *p) {
    int depth = 0;
    int in_single = 0, in_double = 0;
    for (; *p; p++) {
        if (*p == '\\' && *(p+1) && !in_single) { p++; continue; }
        if (!in_double && *p == '\'') in_single = !in_single;
        else if (!in_single && *p == '"') in_double = !in_double;
        else if (!in_single && !in_double) {
            if (*p == '{') depth++;
            else if (*p == '}') {
                depth--;
                if (depth == 0) return p;
            }
        }
    }
    return NULL;
}

static int split_top_commas(const char *s, char ***out_pieces) {
    int cap = 8, n = 0;
    char **arr = malloc(cap * sizeof(char *));
    const char *start = s;
    int depth = 0;
    int in_single = 0, in_double = 0;
    for (const char *p = s; ; p++) {
        int end = (*p == '\0');
        if (!end) {
            if (*p == '\\' && *(p+1) && !in_single) { p++; continue; }
            if (!in_double && *p == '\'') in_single = !in_single;
            else if (!in_single && *p == '"') in_double = !in_double;
            else if (!in_single && !in_double) {
                if (*p == '{') depth++;
                else if (*p == '}') depth--;
                else if (*p == ',' && depth == 0) end = 1;
            }
        }
        if (end) {
            int len = p - start;
            char *piece = malloc(len + 1);
            memcpy(piece, start, len);
            piece[len] = '\0';
            if (n >= cap - 1) { cap *= 2; arr = realloc(arr, cap * sizeof(char *)); }
            arr[n++] = piece;
            if (*p == '\0') break;
            start = p + 1;
        }
    }
    arr[n] = NULL;
    *out_pieces = arr;
    return n;
}

static int try_range(const char *s, char ***out_items) {
    const char *dot = strstr(s, "..");
    if (!dot || dot == s || !*(dot+2)) return 0;
    const char *a = s;
    const char *b = dot + 2;
    int la = dot - a;
    int lb = strlen(b);
    if (la != lb) {
        // allow mismatched widths for numeric ranges
    }

    int ai = 0, bi = 0;
    int numeric = 1;
    for (int i = 0; i < la; i++) {
        if (!isdigit((unsigned char)a[i])) { numeric = 0; break; }
        ai = ai * 10 + (a[i] - '0');
    }
    for (int i = 0; i < lb; i++) {
        if (!isdigit((unsigned char)b[i])) { numeric = 0; break; }
        bi = bi * 10 + (b[i] - '0');
    }
    if (numeric) {
        if (bi < ai) return 0;
        int width = (la >= lb) ? la : lb;
        // pad with leading zeros if both have same width
        int pad = (la == lb) ? 1 : 0;
        int cap = bi - ai + 2;
        char **arr = malloc(cap * sizeof(char *));
        int n = 0;
        for (int v = ai; v <= bi; v++) {
            char buf[64];
            if (pad && width > 1 && width < 32)
                snprintf(buf, sizeof(buf), "%0*d", width, v);
            else
                snprintf(buf, sizeof(buf), "%d", v);
            arr[n++] = xstrdup(buf);
        }
        arr[n] = NULL;
        *out_items = arr;
        return n;
    }

    // alpha range: single chars a..z
    if (la == 1 && lb == 1 &&
        isalpha((unsigned char)a[0]) && isalpha((unsigned char)b[0])) {
        char lo = a[0] < b[0] ? a[0] : b[0];
        char hi = a[0] < b[0] ? b[0] : a[0];
        int cap = hi - lo + 2;
        char **arr = malloc(cap * sizeof(char *));
        int n = 0;
        for (char c = lo; c <= hi; c++) {
            char buf[2] = {c, '\0'};
            arr[n++] = xstrdup(buf);
        }
        arr[n] = NULL;
        *out_items = arr;
        return n;
    }
    return 0;
}

static int expand_braces_word(const char *word, char ***out_words) {
    if (!has_unquoted_brace(word)) {
        *out_words = malloc(2 * sizeof(char *));
        (*out_words)[0] = xstrdup(word);
        (*out_words)[1] = NULL;
        return 1;
    }

    // find first unquoted '{'
    const char *p = word;
    int in_single = 0, in_double = 0;
    while (*p) {
        if (*p == '\\' && *(p+1) && !in_single) { p += 2; continue; }
        if (!in_double && *p == '\'') in_single = !in_single;
        else if (!in_single && *p == '"') in_double = !in_double;
        else if (!in_single && !in_double && *p == '{') break;
        p++;
    }
    const char *brace_open = p;
    const char *brace_close = find_matching_brace(brace_open);
    if (!brace_close) {
        *out_words = malloc(2 * sizeof(char *));
        (*out_words)[0] = xstrdup(word);
        (*out_words)[1] = NULL;
        return 1;
    }
    int prefix_len = brace_open - word;
    char *prefix = malloc(prefix_len + 1);
    memcpy(prefix, word, prefix_len);
    prefix[prefix_len] = '\0';

    int content_len = brace_close - brace_open - 1;
    char *content = malloc(content_len + 1);
    memcpy(content, brace_open + 1, content_len);
    content[content_len] = '\0';

    const char *suffix = brace_close + 1;

    char **alts = NULL;
    int an = split_top_commas(content, &alts);

    char **result = malloc(16 * sizeof(char *));
    int rn = 0, rc = 16;

    for (int i = 0; i < an; i++) {
        char **expanded = NULL;
        int en = try_range(alts[i], &expanded);
        if (en == 0) {
            expanded = malloc(2 * sizeof(char *));
            expanded[0] = xstrdup(alts[i]);
            expanded[1] = NULL;
            en = 1;
        }
        for (int j = 0; j < en; j++) {
            int len = prefix_len + strlen(expanded[j]) + strlen(suffix);
            char *cand = malloc(len + 1);
            snprintf(cand, len + 1, "%s%s%s", prefix, expanded[j], suffix);
            free(expanded[j]);

            char **sr = NULL;
            int sn = expand_braces_word(cand, &sr);
            free(cand);
            for (int k = 0; k < sn; k++) {
                if (rn >= rc - 1) { rc *= 2; result = realloc(result, rc * sizeof(char *)); }
                result[rn++] = sr[k];
            }
            free(sr);
        }
        free(expanded);
        free(alts[i]);
    }
    free(alts);
    free(prefix);
    free(content);

    if (rn == 0) {
        free(result);
        *out_words = malloc(2 * sizeof(char *));
        (*out_words)[0] = xstrdup(word);
        (*out_words)[1] = NULL;
        return 1;
    }
    result[rn] = NULL;
    *out_words = result;
    return rn;
}

char *expand_braces_line(const char *line) {
    if (!line) return NULL;
    if (!has_unquoted_brace(line)) return xstrdup(line);

    // split line into whitespace-separated words (respecting quotes),
    // expand each, join with single spaces.
    size_t cap = strlen(line) * 4 + 64;
    char *out = malloc(cap);
    size_t oi = 0;

    const char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        // read one word
        const char *ws = p;
        int in_single = 0, in_double = 0;
        while (*p && (in_single || in_double || (*p != ' ' && *p != '\t'))) {
            if (*p == '\\' && *(p+1) && !in_single) { p += 2; continue; }
            if (!in_double && *p == '\'') in_single = !in_single;
            else if (!in_single && *p == '"') in_double = !in_double;
            p++;
        }
        int wlen = p - ws;
        char *word = malloc(wlen + 1);
        memcpy(word, ws, wlen);
        word[wlen] = '\0';

        char **expanded = NULL;
        int en = expand_braces_word(word, &expanded);
        free(word);
        for (int i = 0; i < en; i++) {
            if (oi > 0) {
                if (oi + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
                out[oi++] = ' ';
            }
            int len = strlen(expanded[i]);
            if (oi + len + 1 >= cap) {
                while (oi + len + 1 >= cap) cap *= 2;
                out = realloc(out, cap);
            }
            memcpy(out + oi, expanded[i], len);
            oi += len;
            free(expanded[i]);
        }
        free(expanded);
    }
    if (oi + 1 >= cap) { cap = oi + 2; out = realloc(out, cap); }
    out[oi] = '\0';
    return out;
}

static void expand_into(const char *s, char *out, int *bi, int maxlen) {
    while (*s && *bi < maxlen - 1) {
        if (s[0] == '\\' && s[1] != '\0') {
            out[(*bi)++] = *++s;
            s++;
        } else if (s[0] == '$' && s[1] == '?') {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", g_last_status);
            const char *p = buf;
            while (*p && *bi < maxlen - 1) out[(*bi)++] = *p++;
            s += 2;
        } else if (s[0] == '$' && s[1] == '(') {
            const char *cs = s + 2;
            const char *p = cs;
            int depth = 1;
            int in_single = 0, in_double = 0;
            while (*p && depth > 0) {
                if (*p == '\\' && *(p+1) && !in_single) { p += 2; continue; }
                if (!in_double && *p == '\'') { in_single = !in_single; p++; continue; }
                if (!in_single && *p == '"') {
                    in_double = !in_double; p++; continue;
                }
                if (!in_single && !in_double) {
                    if (*p == '(') depth++;
                    else if (*p == ')') { depth--; if (depth == 0) break; }
                }
                p++;
            }
            if (depth != 0 || !*p) {
                // unmatched — emit literal
                out[(*bi)++] = *s++;
                continue;
            }
            int cmd_len = p - cs;
            char *cmd = malloc(cmd_len + 1);
            memcpy(cmd, cs, cmd_len);
            cmd[cmd_len] = '\0';
            char *result = run_subst(cmd);
            free(cmd);
            if (result) {
                const char *q = result;
                while (*q && *bi < maxlen - 1) out[(*bi)++] = *q++;
                free(result);
            }
            s = p + 1;
        } else if (s[0] == '$' && s[1] == '{') {
            const char *p = s + 2;
            char name[MAX_NAME];
            int ni = 0;
            while (*p && *p != '}' &&
                   (isalnum((unsigned char)*p) || *p == '_')) {
                if (ni < MAX_NAME - 1) name[ni++] = *p;
                p++;
            }
            name[ni] = '\0';
            if (*p == '}') p++;
            char *val = getenv(name);
            if (val) {
                while (*val && *bi < maxlen - 1) out[(*bi)++] = *val++;
            }
            s = p;
        } else if (s[0] == '$' && (isalpha((unsigned char)s[1]) || s[1] == '_')) {
            const char *p = s + 1;
            char name[MAX_NAME];
            int ni = 0;
            while (*p && (isalnum((unsigned char)*p) || *p == '_')) {
                if (ni < MAX_NAME - 1) name[ni++] = *p;
                p++;
            }
            name[ni] = '\0';
            char *val = getenv(name);
            if (val) {
                while (*val && *bi < maxlen - 1) out[(*bi)++] = *val++;
            }
            s = p;
        } else if (s[0] == '~' && (s[1] == '/' || s[1] == '\0')) {
            char *home = getenv("HOME");
            if (home) {
                while (*home && *bi < maxlen - 1) out[(*bi)++] = *home++;
            }
            s++;
        } else {
            out[(*bi)++] = *s++;
        }
    }
}

static char *run_subst(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return xstrdup("");
    size_t cap = 256, len = 0;
    char *buf = malloc(cap);
    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, fp)) > 0) {
        len += n;
        if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
    }
    pclose(fp);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
        buf[--len] = '\0';
    }
    buf[len] = '\0';
    return buf;
}

char *expand_string(const char *s) {
    if (!s) return NULL;
    int len = strlen(s);
    char *out = malloc(len * 8 + 16);
    if (!out) return NULL;
    int bi = 0;
    expand_into(s, out, &bi, len * 8 + 16);
    out[bi] = '\0';
    return out;
}

char *strip_quotes(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    if (len >= 2 && ((s[0] == '"' && s[len-1] == '"') ||
                     (s[0] == '\'' && s[len-1] == '\''))) {
        char *r = malloc(len - 1);
        if (!r) return NULL;
        memcpy(r, s + 1, len - 2);
        r[len - 2] = '\0';
        return r;
    }
    return xstrdup(s);
}
