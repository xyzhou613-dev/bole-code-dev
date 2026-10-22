/**
 * @file platform.h
 * @brief Internal platform abstraction: mutexes, condition variables, threads,
 * child processes, clocks, and small filesystem helpers.
 *
 * Not part of the public ABI; only native/src uses it.
 */

#ifndef BOLE_PLATFORM_H
#define BOLE_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
typedef struct bole_mutex { CRITICAL_SECTION cs; } bole_mutex;
typedef struct bole_cond { CONDITION_VARIABLE cv; } bole_cond;
typedef struct bole_thread { HANDLE handle; } bole_thread;
#else
#  include <pthread.h>
typedef struct bole_mutex { pthread_mutex_t mutex; } bole_mutex;
typedef struct bole_cond { pthread_cond_t cond; } bole_cond;
typedef struct bole_thread { pthread_t thread; int started; } bole_thread;
#endif

typedef void (*bole_thread_fn)(void* arg);

typedef struct bole_process {
#ifdef _WIN32
  PROCESS_INFORMATION info;
  HANDLE job;
  HANDLE stdin_write;
  HANDLE stdout_read;
  HANDLE stderr_read;
#else
  int pid;
  int stdin_fd;
  int stdout_fd;
  int stderr_fd;
#endif
  int spawned;
  int exit_code;
  int exited;
  bole_mutex lock;
  bole_cond cond;
} bole_process;

int bole_mutex_init(bole_mutex* mutex);
void bole_mutex_destroy(bole_mutex* mutex);
void bole_mutex_lock(bole_mutex* mutex);
void bole_mutex_unlock(bole_mutex* mutex);

int bole_cond_init(bole_cond* cond);
void bole_cond_destroy(bole_cond* cond);
/** Wait for a signal. Returns 0 when signaled, 1 on timeout, -1 on error. */
int bole_cond_wait(bole_cond* cond, bole_mutex* mutex, int timeout_ms);
void bole_cond_signal(bole_cond* cond);
void bole_cond_broadcast(bole_cond* cond);

int bole_thread_start(bole_thread* thread, bole_thread_fn fn, void* arg);
int bole_thread_join(bole_thread* thread);
/** Release the join handle; the thread frees itself when it exits. */
void bole_thread_detach(bole_thread* thread);
/** Opaque current-thread identity, used to reject forbidden callback re-entry. */
uintptr_t bole_thread_current_id(void);

uint64_t bole_monotonic_ms(void);
int64_t bole_wall_ms(void);

/** Cross-thread flags. Volatile alone is not a synchronization primitive. */
int bole_atomic_load(const volatile int* value);
void bole_atomic_store(volatile int* value, int next);

int bole_process_spawn(bole_process* process, const char* exe,
                       char* const argv[], const char* cwd,
                       char* const envp[]);
/** Block until the child exits. Returns 0 and sets exit_code. */
int bole_process_wait(bole_process* process, int* exit_code);
void bole_process_terminate(bole_process* process);
void bole_process_close(bole_process* process);

/**
 * Snapshot the current process environment as UTF-8 "KEY=VALUE" strings.
 * Returns a NULL-terminated array whose entries and array are owned by the
 * caller and released with bole_environment_free.
 */
char** bole_environment_list(void);
void bole_environment_free(char** environment);

/** Open a UTF-8 path. The caller owns the returned FILE. */
FILE* bole_fopen_utf8(const char* path, const char* mode);

int bole_path_exists(const char* path);
int bole_mkdir_p(const char* path);
char* bole_getcwd(void);

#endif /* BOLE_PLATFORM_H */
