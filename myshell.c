#include <stdio.h>
#include <linux/limits.h> // For PATH_MAX
#include <unistd.h>
#include <fcntl.h>
#include "LineParser.h"
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <ctype.h>
#include <stdlib.h>
#define CMD_MAX 2048
#define TERMINATED -1
#define RUNNING 1
#define SUSPENDED 0
#define HISTLEN 10

char cwd[PATH_MAX];
int debug = 0;

typedef struct process
{
  cmdLine *cmd;         /* the parsed command line*/
  pid_t pid;            /* the process id that is running the command*/
  int status;           /* status of the process: RUNNING/SUSPENDED/TERMINATED */
  struct process *next; /* next process in chain */
} process;

process *process_list = NULL;

typedef struct HistoryNode
{
  char *commandLine;
  struct HistoryNode *prev;
  struct HistoryNode *next;
} HistoryNode;

typedef struct HistoryList
{
  HistoryNode *first;
  HistoryNode *last;
  int size;
} HistoryList;

HistoryList *history = NULL; // Global history instance

void initHistory()
{
  history = (HistoryList *)malloc(sizeof(HistoryList));
  if (!history)
  {
    perror("malloc failed");
    exit(1);
  }
  history->first = NULL;
  history->last = NULL;
  history->size = 0;
}

void addHistory(char *commandLine)
{
  if (history->size == HISTLEN)
  {
    HistoryNode *old = history->first;
    history->first = history->first->next;
    if (history->first)
    {
      history->first->prev = NULL;
    }
    free(old->commandLine);
    free(old);
    history->size--;
  }
  HistoryNode *newNode = (HistoryNode *)malloc(sizeof(HistoryNode));
  if (!newNode)
  {
    perror("malloc failed");
    _exit(1);
  }
  newNode->commandLine = strdup(commandLine);
  newNode->next = NULL;
  if (!history->last)
  {
    history->first = newNode;
    newNode->prev = NULL;
  }
  else
  {
    history->last->next = newNode;
    newNode->prev = history->last;
  }
  history->last = newNode;
  history->size++;
}

void printHistory()
{
  if (!history || history->size == 0)
    printf("No history commands at the moment\n");
  else
  {
    HistoryNode *current = history->first;
    int i = 1;
    while (current)
    {
      printf("%d: %s", i++, current->commandLine);
      current = current->next;
    }
  }
}

void changeDirectory(cmdLine *pCmdLine)
{
  printf("cd: %s\n", pCmdLine->arguments[1]);
  if (pCmdLine->argCount < 1)
  {
    fprintf(stderr, "cd: missing argument\n");
  }
  else if (chdir(pCmdLine->arguments[1]) == -1)
  {
    perror("cd failed");
  }
}

void handleSignalCommands(cmdLine *pCmdLine)
{
  if (pCmdLine->argCount < 1)
  {
    fprintf(stderr, "%s: missing argument\n", pCmdLine->arguments[0]);
    return;
  }
  int signal = strcmp(pCmdLine->arguments[0], "stop") == 0 ? SIGTSTP : strcmp(pCmdLine->arguments[0], "wake") == 0 ? SIGCONT
                                                                                                                   : SIGINT;
  int status = signal == SIGCONT ? RUNNING : signal == SIGTSTP ? SUSPENDED
                                                               : TERMINATED;
  int pid = atoi(pCmdLine->arguments[1]);
  if (kill(pid, signal) == -1)
  {
    perror("kill failed");
  }
  else
  {
    updateProcessStatus(process_list, pid, status);
  }
}

void handleRedirections(cmdLine *pCmdLine)
{
  if (pCmdLine->inputRedirect)
  {
    int input_fd = open(pCmdLine->inputRedirect, O_RDONLY);
    if (input_fd == -1)
    {
      perror("open failed");
      _exit(1);
    }
    if (dup2(input_fd, STDIN_FILENO) == -1)
    {
      perror("dup2 failed");
      _exit(1);
    }
    close(input_fd);
  }
  if (pCmdLine->outputRedirect)
  {
    int output_fd = open(pCmdLine->outputRedirect, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (output_fd == -1)
    {
      perror("open failed");
      _exit(1);
    }
    if (dup2(output_fd, STDOUT_FILENO) == -1)
    {
      perror("dup2 failed");
      _exit(1);
    }
    close(output_fd);
  }
}

void restoreRedirections(int original_stdin, int original_stdout)
{
  if (original_stdin != -1)
  {
    dup2(original_stdin, STDIN_FILENO);
    close(original_stdin);
  }
  if (original_stdout != -1)
  {
    dup2(original_stdout, STDOUT_FILENO);
    close(original_stdout);
  }
}

void handlePipe(cmdLine *pCmdLine)
{
  if (pCmdLine->outputRedirect)
  {
    perror("Output redirect on pipe input is not allowed");
    _exit(1);
  }

  int files_d[2];
  if (pipe(files_d) == -1)
  {
    perror("pipe failed");
    exit(1);
  }

  int child1, child2;

  int original_stdin = dup(STDIN_FILENO);
  int original_stdout = dup(STDOUT_FILENO);
  // First child process
  if ((child1 = fork()) == 0)
  {
    close(files_d[0]);               // Close unused read end
    dup2(files_d[1], STDOUT_FILENO); // Redirect stdout to pipe write-end
    close(files_d[1]);               // Close write end

    handleRedirections(pCmdLine);

    if (strcmp(pCmdLine->arguments[0], "history") == 0)
    {
      printHistory(); // Output goes to STDOUT, redirected to pipe
      freeCmdLines(pCmdLine);
      restoreRedirections(original_stdin, original_stdout);
      exit(0);
    }
    else if (strcmp(pCmdLine->arguments[0], "!!") == 0 ||
             (pCmdLine->arguments[0][0] == '!' && isdigit(pCmdLine->arguments[0][1])))
    {
      executeHistoryCommand(pCmdLine->arguments[0]); // Output goes to STDOUT
      freeCmdLines(pCmdLine);
      restoreRedirections(original_stdin, original_stdout);
      exit(0);
    }
    else if (strcmp(pCmdLine->arguments[0], "procs") == 0)
    {
      printProcessList(&process_list);
      freeCmdLines(pCmdLine);
      restoreRedirections(original_stdin, original_stdout);
      return;
    }
    else if (execvp(pCmdLine->arguments[0], pCmdLine->arguments) == -1)
    {
      perror("execvp failed");
      freeCmdLines(pCmdLine);
      exit(1);
    }
  }
  else if (child1 < 0)
  {
    perror("fork failed");
    exit(1);
  }

  // Second child process
  if ((child2 = fork()) == 0)
  {
    close(files_d[1]);              // Close unused write end
    dup2(files_d[0], STDIN_FILENO); // Redirect stdin to pipe read-end
    close(files_d[0]);              // Close read end
    int original_stdin = -1, original_stdout = -1;
    handleRedirections(pCmdLine->next);

    if (execvp(pCmdLine->next->arguments[0], pCmdLine->next->arguments) == -1)
    {
      perror("execvp failed");
      freeCmdLines(pCmdLine->next);
      exit(1);
    }
  }
  else if (child2 < 0)
  {
    perror("fork failed");
    exit(1);
  }

  // Parent process
  close(files_d[0]); // Close both pipe ends
  close(files_d[1]);
  addProcess(&process_list, pCmdLine, child1);
  addProcess(&process_list, pCmdLine->next, child2);
  waitpid(child1, NULL, 0);
  waitpid(child2, NULL, 0);

  if (debug)
  {
    fprintf(stderr, "PID: %d, Executing command: %s\n", child1, pCmdLine->arguments[0]);
    fprintf(stderr, "PID: %d, Executing command: %s\n", child2, pCmdLine->next->arguments[0]);
  }
}

void execute(cmdLine *pCmdLine)
{
  int original_stdin = dup(STDIN_FILENO);
  int original_stdout = dup(STDOUT_FILENO);
  if (pCmdLine->next)
  {
    handlePipe(pCmdLine);
    return;
  }
  int completed = 0;
  if (strcmp(pCmdLine->arguments[0], "cd") == 0)
  {
    changeDirectory(pCmdLine);
    completed = 1;
  }
  else if (strcmp(pCmdLine->arguments[0], "history") == 0)
  {
    handleRedirections(pCmdLine);
    printHistory();
    freeCmdLines(pCmdLine);
    restoreRedirections(original_stdin, original_stdout);
    return;
  }
  else if (strcmp(pCmdLine->arguments[0], "!!") == 0 || (pCmdLine->arguments[0][0] == '!' && isdigit(pCmdLine->arguments[0][1])))
  {
    handleRedirections(pCmdLine);
    executeHistoryCommand(pCmdLine->arguments[0]);
    freeCmdLines(pCmdLine);
    restoreRedirections(original_stdin, original_stdout);
    return;
  }
  else if (strcmp(pCmdLine->arguments[0], "procs") == 0)
  {
    handleRedirections(pCmdLine);
    printProcessList(&process_list);
    freeCmdLines(pCmdLine);
    restoreRedirections(original_stdin, original_stdout);
    return;
  }
  else if (strcmp(pCmdLine->arguments[0], "stop") == 0 ||
           strcmp(pCmdLine->arguments[0], "wake") == 0 ||
           strcmp(pCmdLine->arguments[0], "term") == 0)
  {
    handleSignalCommands(pCmdLine);
    freeCmdLines(pCmdLine);
    return;
  }

  int pid;
  if (!(pid = fork()))
  {
    handleRedirections(pCmdLine);
    if (!completed)
    {
      if (execvp(pCmdLine->arguments[0], pCmdLine->arguments) == -1)
      {
        freeCmdLines(pCmdLine);
        perror("execvp failed");
        _exit(1);
      }
    }
  }

  else if (pid < 0)
  {
    perror("fork failed");
  }

  else if (pCmdLine->blocking)
    waitpid(pid, NULL, 0);
  if (debug)
  {
    fprintf(stderr, "PID: %d, Executing command: %s\n", pid, pCmdLine->arguments[0]);
  }
  addProcess(&process_list, pCmdLine, pid);
}

void addProcess(process **process_list, cmdLine *cmd, pid_t pid)
{
  process *p = (process *)malloc(sizeof(process));
  if (p == NULL)
  {
    perror("process allocation failed");
    exit(1);
  }
  p->cmd = cmd;
  p->pid = pid;
  p->next = *process_list;
  p->status = RUNNING;
  *process_list = p;
}

void freeProcessList(process *process_list)
{
  process *curr = process_list;
  while (curr != NULL)
  {
    process *next = curr->next;
    freeCmdLines(curr->cmd);
    free(curr);
    curr = next;
  }
}

void updateProcessStatus(process *process_list, int pid, int status)
{
  process *curr = process_list;
  while (curr != NULL)
  {
    if (curr->pid == pid)
    {
      curr->status = status;
      break;
    }
    curr = curr->next;
  }
}

void updateProcessList(process **process_list)
{
  process *current = *process_list;
  int status;
  pid_t result;
  while (current != NULL)
  {
    result = waitpid(current->pid, &status, WNOHANG | WUNTRACED);
    if (result != 0)
    {
      if (WIFSIGNALED(status) || WIFEXITED(status))
      {
        current->status = TERMINATED;
      }
      else if (WIFSTOPPED(status))
      {
        current->status = SUSPENDED;
      }
      else if (WIFCONTINUED(status))
      {
        current->status = RUNNING;
      }
    }
    current = current->next;
  }
}

void printProcessList(process **process_list)
{
  updateProcessList(process_list);
  process *current = *process_list;
  process *prev = NULL;
  int index = 0;
  printf("Index\tPID\tCommand\tSTATUS\n");
  while (current != NULL)
  {
    int cmdLen = 0;
    for (int i = 0; i < current->cmd->argCount; i++)
    {
      cmdLen += strlen(current->cmd->arguments[i]);
    }
    cmdLen = (cmdLen * 2) - 1; // for spaces between arguments
    char *fullCmd = (char *)malloc(cmdLen * sizeof(char));
    if (!fullCmd)
    {
      perror("malloc failed");
      exit(1);
    }
    strcpy(fullCmd, current->cmd->arguments[0]);
    for (int i = 1; i < current->cmd->argCount; i++)
    {
      strcat(fullCmd, " ");
      strcat(fullCmd, current->cmd->arguments[i]);
    }

    char *status;
    if (current->status == TERMINATED)
    {
      status = "Terminated";
    }
    else if (current->status == RUNNING)
    {
      status = "Running";
    }
    else if (current->status == SUSPENDED)
    {
      status = "Suspended";
    }

    printf("%d\t%d\t%s\t%s\n", index, current->pid, fullCmd, status);
    index++;
    free(fullCmd);
    if (current->status == TERMINATED)
    {
      if (prev == NULL)
      {
        // Current is the head of the list
        *process_list = current->next;
        current->cmd->next = NULL;
        freeCmdLines(current->cmd);
        free(current);
        current = *process_list;
      }
      else
      {
        prev->next = current->next;
        freeCmdLines(current->cmd);
        free(current);
        current = prev->next;
      }
    }
    else
    {
      prev = current;
      current = current->next;
    }
  }
}

void executeHistoryCommand(const char *cmd)
{
  if (!history || history->size == 0)
    printf("No history commands at the moment\n");
  else
  {
    const char *command = NULL;
    if (strcmp(cmd, "!!") == 0)
      command = history->last->commandLine;
    else if (cmd[0] == '!' && isdigit(cmd[1]))
    {
      int index = atoi(&cmd[1]);
      if (index > history->size || index < 1)
      {
        fprintf(stderr, "History index is out of range\n");
        return;
      }
      HistoryNode *current = history->first;
      for (int i = 1; i < index; i++)
      {
        current = current->next;
      }
      command = current->commandLine;
    }
    else
    {
      fprintf(stderr, "Invalid history command\n");
      return;
    }
    addHistory(command);
    cmdLine *pCmdLine = parseCmdLines(command);
    execute(pCmdLine);
  }
}

void freeHistory()
{
  if (!history)
    return;

  HistoryNode *current = history->first;
  while (current)
  {
    HistoryNode *next = current->next;
    free(current->commandLine);
    free(current);
    current = next;
  }

  free(history);
  history = NULL;
}

int main(int argc, char const *argv[])
{
  if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'd')
    debug = 1;
  char input[100];
  cmdLine *cmd = NULL;
  initHistory();
  while (1)
  {

    if (getcwd(cwd, sizeof(cwd)) != NULL)
    {
      printf("\n%s>", cwd);
    }
    else
    {
      fprintf("getcwd() error", stderr);
      exit(1);
    }
    fgets(input, CMD_MAX, stdin);
    if (strcmp(input, "quit\n") == 0)
    {
      exit(0);
    }
    if (feof(stdin))
    {
      // if (cmd != NULL)
      //   freeCmdLines(cmd);
      freeHistory();
      exit(0);
    }
    cmd = parseCmdLines(input);
    execute(cmd);
    if (input[0] != '!')
      addHistory(input);
  }

  return 0;
}
