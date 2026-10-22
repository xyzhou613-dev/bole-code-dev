#include "platform.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int bole_mutex_init(bole_mutex* mutex) {
  return pthread_mutex_init(&mutex->mutex, NULL) == 0 ? 0 : -1;
}

void bole_mutex_destroy(bole_mutex* mutex) {
  pthread_mutex_destroy(&mutex->mutex);
}

void bole_mutex_lock(bole_mutex* mutex) {
  pthread_mutex_lock(&mutex->mutex);
}

void bole_mutex_unlock(bole_mutex* mutex) {
  pthread_mutex_unlock(&mutex->mutex);
}

int bole_cond_init(bole_cond* cond) {
  pthread_condattr_t attr;
  if (pthread_condattr_init(&attr) != 0) {
    return -1;
  }
  pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
  int result = pthread_cond_init(&cond->cond, &attr) == 0 ? 0 : -1;
  pthread_condattr_destroy(&attr);
  return result;
}

void bole_cond_destroy(bole_cond* cond) {
  pthread_cond_destroy(&cond->cond);
}

int bole_cond_wait(bole_cond* cond, bole_mutex* mutex, int timeout_ms) {
  if (timeout_ms < 0) {
    return pthread_cond_wait(&cond->cond, &mutex->mutex) == 0 ? 0 : -1;
  }
  struct timespec deadline;
  clock_gettime(CLOCK_MONOTONIC, &deadline);
  deadline.tv_sec += timeout_ms / 1000;
  deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
  if (deadline.tv_nsec >= 1000000000L) {
    deadline.tv_sec += 1;
    deadline.tv_nsec -= 1000000000L;
  }
  int result = pthread_cond_timedwait(&cond->cond, &mutex->mutex, &deadline);
  if (result == 0) {
    return 0;
  }
  return result == ETIMEDOUT ? 1 : -1;
}

void bole_cond_signal(bole_cond* cond) {
  pthread_cond_signal(&cond->cond);
}

void bole_cond_broadcast(bole_cond* cond) {
  pthread_cond_broadcast(&cond->cond);
}

typedef struct thread_start {
  bole_thread_fn fn;
  void* arg;
} thread_start;

static void* thread_entry(void* raw) {
  thread_start start = *(thread_start*)raw;
  free(raw);
  start.fn(start.arg);
  return NULL;
}

int bole_thread_start(bole_thread* thread, bole_thread_fn fn, void* arg) {
  thread_start* start = (thread_start*)malloc(sizeof(thread_start));
  if (start == NULL) {
    return -1;
  }
  start->fn = fn;
  start->arg = arg;
  if (pthread_create(&thread->thread, NULL, thread_entry, start) != 0) {
    free(start);
    return -1;
  }
  thread->started = 1;
  return 0;
}

int bole_thread_join(bole_thread* thread) {
  if (!thread->started) {
    return 0;
  }
  int result = pthread_join(thread->thread, NULL);
  thread->started = 0;
  return result == 0 ? 0 : -1;
}

uint64_t bole_monotonic_ms(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

int64_t bole_wall_ms(void) {
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int bole_process_spawn(bole_process* process, const char* exe,
                       char* const argv[], const char* cwd,
                       char* const envp[]) {
  if (process == NULL || exe == NULL || argv == NULL) {
    return -1;
  }
  memset(process, 0, sizeof(*process));
  process->pid = -1;
  process->stdin_fd = -1;
  process->stdout_fd = -1;
  process->stderr_fd = -1;
  if (bole_mutex_init(&process->lock) != 0) {
    return -1;
  }
  if (bole_cond_init(&process->cond) != 0) {
    bole_mutex_destroy(&process->lock);
    return -1;
  }

  int stdin_pipe[2];
  int stdout_pipe[2];
  int stderr_pipe[2];
  if (pipe(stdin_pipe) != 0) {
    goto fail;
  }
  if (pipe(stdout_pipe) != 0) {
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    goto fail;
  }
  if (pipe(stderr_pipe) != 0) {
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    goto fail;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    goto fail;
  }
  if (pid == 0) {
    if (dup2(stdin_pipe[0], STDIN_FILENO) < 0
        || dup2(stdout_pipe[1], STDOUT_FILENO) < 0
        || dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
      _exit(127);
    }
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    if (cwd != NULL && chdir(cwd) != 0) {
      _exit(127);
    }
    execve(exe, argv, envp);
    _exit(127);
  }

  close(stdin_pipe[0]);
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);
  process->pid = (int)pid;
  process->stdin_fd = stdin_pipe[1];
  process->stdout_fd = stdout_pipe[0];
  process->stderr_fd = stderr_pipe[0];
  process->spawned = 1;
  int flags = fcntl(process->stdout_fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(process->stdout_fd, F_SETFL, flags | O_NONBLOCK);
  }
  return 0;

fail:
  bole_cond_destroy(&process->cond);
  bole_mutex_destroy(&process->lock);
  return -1;
}

int bole_process_wait(bole_process* process, int* exit_code) {
  if (process == NULL || !process->spawned) {
    return -1;
  }
  int status = 0;
  for (;;) {
    pid_t result = waitpid((pid_t)process->pid, &status, 0);
    if (result == (pid_t)process->pid) {
      break;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    return -1;
  }
  int code = 0;
  if (WIFEXITED(status)) {
    code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    code = 128 + WTERMSIG(status);
  }
  bole_mutex_lock(&process->lock);
  process->exit_code = code;
  process->exited = 1;
  bole_cond_broadcast(&process->cond);
  bole_mutex_unlock(&process->lock);
  if (exit_code != NULL) {
    *exit_code = code;
  }
  return 0;
}

void bole_process_terminate(bole_process* process) {
  if (process == NULL || !process->spawned) {
    return;
  }
  kill((pid_t)process->pid, SIGKILL);
}

void bole_process_close(bole_process* process) {
  if (process == NULL) {
    return;
  }
  if (process->stdin_fd >= 0) {
    close(process->stdin_fd);
    process->stdin_fd = -1;
  }
  if (process->stdout_fd >= 0) {
    close(process->stdout_fd);
    process->stdout_fd = -1;
  }
  if (process->stderr_fd >= 0) {
    close(process->stderr_fd);
    process->stderr_fd = -1;
  }
}

int bole_path_exists(const char* path) {
  return path != NULL && access(path, F_OK) == 0;
}

int bole_mkdir_p(const char* path) {
  if (path == NULL || path[0] == '\0') {
    return -1;
  }
  char* copy = (char*)malloc(strlen(path) + 1);
  if (copy == NULL) {
    return -1;
  }
  strcpy(copy, path);
  for (char* p = copy + 1; *p != '\0'; p++) {
    if (*p == '/' || *p == '\\') {
      char saved = *p;
      *p = '\0';
      if (mkdir(copy, 0777) != 0 && errno != EEXIST) {
        free(copy);
        return -1;
      }
      *p = saved;
    }
  }
  if (mkdir(copy, 0777) != 0 && errno != EEXIST) {
    free(copy);
    return -1;
  }
  free(copy);
  return 0;
}

char* bole_getcwd(void) {
  size_t size = 256;
  for (;;) {
    char* buffer = (char*)malloc(size);
    if (buffer == NULL) {
      return NULL;
    }
    if (getcwd(buffer, size) != NULL) {
      return buffer;
    }
    free(buffer);
    if (errno != ERANGE || size > (1u << 20)) {
      return NULL;
    }
    size *= 2;
  }
}
