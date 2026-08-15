#!/usr/bin/env bash
# mysh demo — exercises every feature, including the extras that go
# beyond the Group 1 PDF requirements.
#
# Run from the repo root with:
#     stdbuf -oL ./mysh < demo.sh
#
# Each section prints a header line so the audience can follow along.

export REPO=/home/d4nitrix13/Code/shell-linux
export SRC=$REPO/src

echo "=== [1] BASIC BUILTINS ==="
echo hello world
pwd
export API=https://api.example.com
echo "API=$API"

echo "=== [2] cd - (toggle previous directory) ==="
cd /tmp
cd /home
cd -
cd -

echo "=== [3] PIPES + REDIRECTION ==="
ls $SRC | wc -l
cat $REPO/README.md | grep -E "^##" | head -3
echo "stored" > /tmp/mysh_demo_out.txt
cat /tmp/mysh_demo_out.txt
echo "appended" >> /tmp/mysh_demo_out.txt
cat /tmp/mysh_demo_out.txt
echo "stderr redirect" 1>/tmp/mysh_demo_err.txt
cat /tmp/mysh_demo_err.txt

echo "=== [4] CONDITIONAL + SEQUENCE OPERATORS ==="
true && echo "after true: yes"
false || echo "after false: fallback"
ls /no/such/path 2>/dev/null || echo "missing -> caught"
echo a ; echo b ; echo c
false && echo "skipped" || echo "ran via fallback"

echo "=== [5] BACKGROUND JOBS ==="
sleep 0.2 &
sleep 0.1 &
echo "both jobs launched, prompt returns immediately"

echo "=== [6] VARIABLE EXPANSION ==="
echo "USER=$USER"
echo "HOME=$HOME"
echo "USER via braces: ${USER}"

echo "=== [7] \$? (last exit status) ==="
true
echo "after true: $?"
false
echo "after false: $?"
ls /no/such 2>/dev/null
echo "after failed ls: $?"

echo "=== [8] TILDE + QUOTES ==="
echo "home is ~"
echo 'literal $USER not expanded in single quotes'
echo "double quotes expand: USER=$USER"

echo "=== [9] BRACE EXPANSION ==="
echo "-- comma list --"
echo {red,green,blue}
echo "-- numeric range --"
echo {1..5}
echo "-- zero-padded range --"
echo {01..05}
echo "-- alpha range --"
echo {a..e}
echo "-- attached to prefix/suffix --"
echo img_{01..03}.png
echo "-- cartesian product --"
echo {x,y}_{1..3}
echo "-- brace inside quotes is NOT expanded --"
echo "{not,expanded}"
echo "-- nested braces --"
echo {{a,b},{c,d}}

echo "=== [10] GLOB EXPANSION ==="
echo "-- * matches any --"
ls $SRC/*.c
echo "-- ? matches one char --"
touch /tmp/aa.txt /tmp/ab.txt /tmp/ac.txt
ls /tmp/a?.txt
rm /tmp/aa.txt /tmp/ab.txt /tmp/ac.txt
echo "-- [...] character class --"
ls $SRC/[a-m]*.c

echo "=== [11] COMMAND SUBSTITUTION \$(...) ==="
echo "user is $(whoami)"
echo "year=$(date +%Y) month=$(date +%m) day=$(date +%d)"
echo "file count: $(ls $SRC | wc -l)"
echo "-- nested substitution --"
echo "$(echo outer $(echo inner))"
echo "-- substitution inside double quotes preserves spaces --"
echo "files: $(ls $SRC)"

echo "=== [12] COMBINING EVERYTHING ==="
mkdir -p /tmp/mysh_demo_glob
touch /tmp/mysh_demo_glob/file_{1..3}.{txt,log}
echo "-- all log files in dir --"
ls /tmp/mysh_demo_glob/*.log
echo "-- count via subst --"
echo "logs: $(ls /tmp/mysh_demo_glob/*.log | wc -l)"
echo "-- conditional + subst + brace --"
test -d /tmp/mysh_demo_glob && echo "dir exists: $(basename /tmp/mysh_demo_glob)" || echo "no dir"
rm -rf /tmp/mysh_demo_glob /tmp/mysh_demo_out.txt /tmp/mysh_demo_err.txt

echo "=== [13] BUILT-IN TOOLS ==="
type echo
type ls
type cd
type nonexistent_command
history | tail -5

echo "=== END OF DEMO ==="
exit
