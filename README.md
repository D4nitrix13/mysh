# mysh — Shell POSIX minimalista en C

Un shell Unix interactivo pequeño escrito en C para la clase de Arquitectura de Sistemas Operativos. Implementa los conceptos centrales detrás de `bash`: creación de procesos (`fork`), carga de programas (`exec`), espera del padre (`wait`), redirección de descriptores de archivo, tuberías y manejo de señales.

## Compilar

```bash
make          # produce ./mysh
make run      # compila + ejecuta
make clean    # borra artefactos de build
```

Requiere `gcc` y `libreadline` (ya presentes en la mayoría de distros Linux).

## Funcionalidades

### Comandos built-in
- `cd [dir]` — cambia de directorio (por defecto a `$HOME`)
- `cd -` — vuelve al directorio anterior (toggle entre actual y `$OLDPWD`)
- `exit [code]` — sale del shell con el código de status
- `pwd` — imprime el directorio de trabajo
- `echo [-n] [args...]` — imprime los argumentos
- `export NAME=VAL` — define variable de entorno (sin args = lista todas)
- `unset NAME` — elimina una variable de entorno
- `env` — vuelca todas las variables de entorno
- `history` — muestra el historial de comandos
- `type <cmd>` — indica si un comando es builtin o externo
- `help` — muestra los builtins disponibles
- `clear` — limpia la pantalla

### Comandos externos
Cualquier comando encontrado en `$PATH` se ejecuta vía `fork()` + `execvp()` + `waitpid()`. Se captura el exit status.

### Tuberías
```bash
ls /usr/bin | grep ^g | head -3
cat archivo.txt | wc -l
```

### Ejecución condicional / secuenciada
```bash
cmd1 && cmd2     # ejecuta cmd2 sólo si cmd1 salió bien
cmd1 || cmd2     # ejecuta cmd2 sólo si cmd1 falló
cmd1 ; cmd2 ; cmd3   # ejecuta todos en secuencia, ignorando el status
```

### Redirección de E/S
- `>` — redirige stdout (truncar)
- `>>` — redirige stdout (append)
- `<` — redirige stdin
- `2>` — redirige stderr (truncar)
- `2>>` — redirige stderr (append)

### Jobs en background
```bash
sleep 5 &
```
Imprime el PID y vuelve al prompt inmediatamente. Los jobs en background que terminaron se cosechan (reap) en el siguiente prompt.

### Variables y expansión
- `$VAR` y `${VAR}` — expansión de variable de entorno
- `$?` — exit status del último comando
- `$(comando)` — sustitución de comando (corre `comando`, reemplaza con su stdout, le quita los `\n` finales)
- `~` — se expande a `$HOME`
- Comillas simples `'...'` — literal
- Comillas dobles `"..."` — permite expansión de variables
- Backslash escapando fuera de comillas simples

### Brace expansion
- `{a,b,c}` — lista con comas, se expande a múltiples palabras (`a b c`)
- `{1..5}` — rango numérico (`1 2 3 4 5`); `{01..05}` preserva el zero-padding
- `{a..e}` — rango alfabético (`a b c d e`)
- Producto cartesiano: `pre{a,b}suf{1,2}` → `prea1 prea2 preb1 preb2`
- Las llaves dentro de comillas NO se expanden
- La expansión ocurre ANTES del globbing (así `*.{c,h}` funciona como se espera)

### Glob expansion
- `*` — matchea cualquier cantidad de chars (excepto `/`)
- `?` — matchea exactamente un char
- `[abc]` — clase de caracteres
- Implementado vía `glob(3)` POSIX en el executor (se expande por argumento dentro de `argv`)

### Historial
Historial persistente vía `readline` en `~/.mysh_history`. Up/down arrows + edición de línea vienen de fábrica.

### Manejo de señales
- `Ctrl+C` cancela el input actual (no mata al shell)
- `Ctrl+D` (EOF) sale de forma prolija
- `Ctrl+\` (SIGQUIT) se ignora
- Los procesos hijos en foreground reciben las señales normalmente; los jobs en background ignoran `SIGINT`/`SIGQUIT` de la terminal

## Arquitectura

```
src/
  main.c       — loop REPL, prompt, setup de señales, persistencia de historial
  parser.c     — tokenizer + AST builder → pipeline_t
  executor.c   — runner de pipeline con fork/exec/wait, redirección
  builtins.c   — tabla + dispatch de todos los builtins
  expand.c     — expansión de $VAR, ${VAR}, $(...), {}, ~
  util.c       — helpers chicos (xstrdup)
include/
  shell.h      — tipos compartidos (token_t, redir_t, simple_cmd_t, pipeline_t)
Makefile
```

### Flujo de datos
1. `readline()` lee una línea → `parse_line()` produce un `pipeline_t`
2. `pipeline_t` es una lista de `simple_cmd_t`, cada uno con `argv[]`, `redir_t[]`
3. `execute_pipeline()`:
   - Si es un único builtin sin redirección → corre en el proceso del shell
   - Si no: crea pipes, hace fork por comando, aplica redirecciones, `execvp()`, el padre espera
4. Después de ejecutar, el shell cosecha zombies y vuelve al loop

### Llamadas al sistema ejercitadas
- `fork()`, `execvp()`, `waitpid()` — ciclo de vida de procesos
- `pipe()`, `dup2()`, `open()`, `close()` — descriptores de archivo e IPC
- `chdir()`, `getcwd()`, `getenv()`, `setenv()`, `unsetenv()` — filesystem + env
- `signal()`, `sigaction()` — manejo de señales

## Ejemplos de uso

```bash
$ ./mysh
mysh — minimal POSIX shell (type 'help' for commands)

user@host:~$ echo "hola $USER"
hola d4nitrix13
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

### Script de demo

`demo.sh` recorre todas las funcionalidades de punta a punta (built-ins, pipes, redirección, `&&`/`||`/`;`, background, vars, `$?`, tilde, brace expansion, globs, `$(...)`, `type`, history). Ejecutalo con:

```bash
stdbuf -oL ./mysh < demo.sh
```

`stdbuf -oL` desactiva el buffering de stdout para que el output vaya apareciendo a medida que el shell procesa cada línea.

## Limitaciones

- Sin job control (`fg`, `bg`, `Ctrl+Z` no están manejados)
- Sin scripts / control de flujo (`if`, `for`, etc.)
- Sin here-strings (`<<<`) ni here-docs (`<<`)
- No se soporta redirección fd-a-fd estilo `2>&1` (sólo file-a-fd)
- Las strings en comillas simples igual expanden `$VAR` (el tokenizer les quita las comillas antes de expandir)
- Comentarios con `#` no soportados

Estas son omisiones a propósito para mantener el codebase enfocado en los conceptos centrales de SO sobre los que se evalúa el proyecto.
