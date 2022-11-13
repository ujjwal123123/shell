# Unix command shell

A command shell implemented in C using Linux system calls.

## Features:
- Pipes
- Internal commands: `exit`, `cd`, `source`, `echo`
- Input/output redirection
- `Ctrl + C` handling
- `;` for command separation
- `&` for background processes

## Build

```bash
gcc a.c -Wall -Wextra -o shell
./shell
```
