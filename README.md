Shell Implementation README

Overview

This project is a simple shell implementation in C that supports basic command execution, process management, redirections, pipelining, and command history.

Features

Command Execution: Executes user-entered commands using execvp.

Process Management: Tracks running processes, allowing users to suspend, resume, and terminate them.

Redirections: Supports input (<) and output (>) redirections.

Pipelining: Executes commands connected via pipes (|).

Built-in Commands:
```
cd <directory>: Changes the working directory.

history: Displays the last 10 executed commands.

!! and !<num>: Executes the last or a specific command from history.

procs: Lists active processes.

stop <pid>: Suspends a process.

wake <pid>: Resumes a suspended process.

term <pid>: Terminates a process. 
```

Compilation & Execution

Requirements

GCC compiler

Linux environment

Compilation:
**Make**

Running the Shell:
**./myshell**
