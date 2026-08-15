# mysh — Minimal POSIX Shell in C

A small interactive Unix shell written in C for an Operating Systems Architecture class. Implements the core concepts behind `bash`: process creation (`fork`), program loading (`exec`), parent waiting (`wait`), file descriptor redirection, pipes, and signal handling.

## Build

```bash
make          # produces ./mysh
make run      # build + run
make clean    # remove build artifacts
```

Requires `gcc` and `libreadline` (already present on most Linux distros).

## Features

### Built-in commands
- `cd [dir]` — change directory (defaults to `$HOME`)
- `exit [code]` — exit shell with status code
- `pwd` — print working directory
- `echo [-n] [args...]` — print args
- `export NAME=VAL` — set environment variable (no args = list all)
- `unset NAME` — remove env variable
- `env` — dump all environment variables
- `history` — show command history
- `type <cmd>` — show whether a command is a builtin or external
- `help` — show available builtins
- `clear` — clear the screen

### External commands
Any command found in `$PATH` is executed via `fork()` + `execvp()` + `waitpid()`. Exit status is captured.

### Pipes
```bash
ls /usr/bin | grep ^g | head -3
cat file.txt | wc -l
```

### Conditional / sequenced execution
```bash
cmd1 && cmd2     # run cmd2 only if cmd1 succeeded
cmd1 || cmd2     # run cmd2 only if cmd1 failed
cmd1 ; cmd2 ; cmd3   # run all in sequence, ignoring status
```

### I/O redirection
- `>` — redirect stdout (truncate)
- `>>` — redirect stdout (append)
- `<` — redirect stdin
- `2>` — redirect stderr (truncate)
- `2>>` — redirect stderr (append)

### Background jobs
```bash
sleep 5 &
```
Prints the PID and returns to the prompt immediately. Finished background jobs are reaped on the next prompt.

### Variables and expansion
- `$VAR` and `${VAR}` — environment variable expansion
- `$?` — last command exit status
- `$(command)` — command substitution (runs `command`, replaces with stdout, trailing newlines stripped)
- `~` — expands to `$HOME`
- Single quotes `'...'` — literal
- Double quotes `"..."` — supports variable expansion
- Backslash escaping outside single quotes

### Brace expansion
- `{a,b,c}` — comma list, expands to multiple words (`a b c`)
- `{1..5}` — numeric range (`1 2 3 4 5`); `{01..05}` preserves zero-padding
- `{a..e}` — alpha range (`a b c d e`)
- Cartesian product: `pre{a,b}suf{1,2}` → `prea1 prea2 preb1 preb2`
- Braces inside quotes are NOT expanded
- Expansion happens BEFORE globbing (so `*.{c,h}` works as expected)

### Glob expansion
- `*` — matches any chars (except `/`)
- `?` — matches one char
- `[abc]` — character class
- Implemented via POSIX `glob(3)` in the executor (so it expands per-arg in `argv`)

### History
Persistent history via `readline` in `~/.mysh_history`. Up/down arrows + line editing are available out of the box.

### Signal handling
- `Ctrl+C` cancels the current input (does not kill the shell)
- `Ctrl+D` (EOF) exits gracefully
- `Ctrl+\` (SIGQUIT) is ignored
- Foreground child processes receive signals normally; background jobs ignore `SIGINT`/`SIGQUIT` from the terminal

## Architecture

```
src/
  main.c       — REPL loop, prompt, signal setup, history persistence
  parser.c     — tokenizer + AST builder → pipeline_t
  executor.c   — fork/exec/wait pipeline runner, redirection
  builtins.c   — all builtin commands table + dispatch
  expand.c     — $VAR, ${VAR}, ~ expansion
  util.c       — small helpers (xstrdup)
include/
  shell.h      — shared types (token_t, redir_t, simple_cmd_t, pipeline_t)
Makefile
```

### Data flow
1. `readline()` reads a line → `parse_line()` produces a `pipeline_t`
2. `pipeline_t` is a list of `simple_cmd_t`, each with `argv[]`, `redir_t[]`
3. `execute_pipeline()`:
   - If single builtin with no redirection → runs in shell process
   - Otherwise: creates pipes, forks one child per command, applies redirections, `execvp()`s, parent waits
4. After execution, the shell reaps zombies and loops

### Key system calls exercised
- `fork()`, `execvp()`, `waitpid()` — process lifecycle
- `pipe()`, `dup2()`, `open()`, `close()` — file descriptors and IPC
- `chdir()`, `getcwd()`, `getenv()`, `setenv()`, `unsetenv()` — filesystem + env
- `signal()`, `sigaction()` — signal handling

## Usage examples

```bash
$ ./mysh
mysh — minimal POSIX shell (type 'help' for commands)

user@host:~$ echo "hello $USER"
hello d4nitrix13
user@host:~$ export API=https://api.example.com
user@host:~$ echo $API
https://api.example.com
user@host:~$ ls /usr/bin | wc -l
4419
user@host:~$ gcc --version > gcc.txt 2> /tmp/gcc.err
user@host:~$ cat gcc.txt | head -1
gcc (GCC) 14.2.1
user@host:~$ sleep 30 &
[bg] pid 12345
user@host:~$ exit
mysh: goodbye
```

### Demo script

`demo.sh` runs every feature end-to-end (built-ins, pipes, redirection,
`&&`/`||`/`;`, background, vars, `$?`, tilde, brace expansion, globs,
`$(...)`, `type`, history). Run it with:

```bash
stdbuf -oL ./mysh < demo.sh
```

`stdbuf -oL` disables stdout buffering so the output streams as the
shell processes each line.

## Limitations

- No job control (`fg`, `bg`, `Ctrl+Z` are not handled)
- No shell scripts / control flow (`if`, `for`, etc.)
- No here-strings (`<<<`) or here-docs (`<<`)
- `2>&1` style fd-to-fd redirection is not supported (only file-to-fd)
- Single-quoted strings still expand `$VAR` (tokenizer strips outer quotes before expansion)
- `#` comments not supported

These are deliberate omissions to keep the codebase focused on the core OS concepts the project is graded on.
