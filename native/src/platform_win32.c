/**
 * @file platform_win32.c
 * @brief Win32 implementation of platform.h: mutexes, condition variables,
 * threads, a 3-pipe child process inside a kill-on-close Job Object, and small
 * path helpers.
 *
 * The runtime is started with an explicit application name and an argv rebuilt
 * as a UTF-16 command line; stdio uses inheritable anonymous pipes and the
 * parent ends are made non-inheritable. The child is created suspended, assigned
 * to the Job, and only then resumed, so no tool or shell can outlive a crash of
 * the host (08 §6.5, 04 §6).
 */

#include "platform.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* ── UTF-8 ↔ UTF-16 ─────────────────────────────────────────────── */

static wchar_t* utf8_to_wide(const char* text) {
  if (text == NULL) return NULL;
  int needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
  if (needed <= 0) return NULL;
  wchar_t* wide = (wchar_t*)calloc((size_t)needed, sizeof(wchar_t));
  if (wide == NULL) return NULL;
  if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, needed) <= 0) {
    free(wide);
    return NULL;
  }
  return wide;
}

static char* wide_to_utf8(const wchar_t* text) {
  if (text == NULL) return NULL;
  int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
  if (needed <= 0) return NULL;
  char* utf8 = (char*)calloc((size_t)needed, 1);
  if (utf8 == NULL) return NULL;
  if (WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, needed, NULL, NULL) <= 0) {
    free(utf8);
    return NULL;
  }
  return utf8;
}

/* ── Sync primitives ────────────────────────────────────────────── */

int bole_mutex_init(bole_mutex* mutex) {
  InitializeCriticalSection(&mutex->cs);
  return 0;
}

void bole_mutex_destroy(bole_mutex* mutex) {
  DeleteCriticalSection(&mutex->cs);
}

void bole_mutex_lock(bole_mutex* mutex) {
  EnterCriticalSection(&mutex->cs);
}

void bole_mutex_unlock(bole_mutex* mutex) {
  LeaveCriticalSection(&mutex->cs);
}

int bole_cond_init(bole_cond* cond) {
  InitializeConditionVariable(&cond->cv);
  return 0;
}

void bole_cond_destroy(bole_cond* cond) {
  (void)cond;
}

int bole_cond_wait(bole_cond* cond, bole_mutex* mutex, int timeout_ms) {
  if (timeout_ms < 0) {
    return SleepConditionVariableCS(&cond->cv, &mutex->cs, INFINITE) ? 0 : -1;
  }
  if (SleepConditionVariableCS(&cond->cv, &mutex->cs, (DWORD)timeout_ms)) {
    return 0;
  }
  return GetLastError() == ERROR_TIMEOUT ? 1 : -1;
}

void bole_cond_signal(bole_cond* cond) {
  WakeConditionVariable(&cond->cv);
}

void bole_cond_broadcast(bole_cond* cond) {
  WakeAllConditionVariable(&cond->cv);
}

/* ── Threads ────────────────────────────────────────────────────── */

typedef struct thread_start {
  bole_thread_fn fn;
  void* arg;
} thread_start;

static DWORD WINAPI thread_entry(LPVOID raw) {
  thread_start* start = (thread_start*)raw;
  bole_thread_fn fn = start->fn;
  void* arg = start->arg;
  free(start);
  fn(arg);
  return 0;
}

int bole_thread_start(bole_thread* thread, bole_thread_fn fn, void* arg) {
  thread_start* start = (thread_start*)malloc(sizeof(thread_start));
  if (start == NULL) return -1;
  start->fn = fn;
  start->arg = arg;
  thread->handle = CreateThread(NULL, 0, thread_entry, start, 0, NULL);
  if (thread->handle == NULL) {
    free(start);
    return -1;
  }
  return 0;
}

int bole_thread_join(bole_thread* thread) {
  if (thread->handle == NULL) return 0;
  DWORD result = WaitForSingleObject(thread->handle, INFINITE);
  CloseHandle(thread->handle);
  thread->handle = NULL;
  return result == WAIT_OBJECT_0 ? 0 : -1;
}

uintptr_t bole_thread_current_id(void) {
  return (uintptr_t)GetCurrentThreadId();
}

void bole_thread_detach(bole_thread* thread) {
  if (thread == NULL || thread->handle == NULL) return;
  CloseHandle(thread->handle);
  thread->handle = NULL;
}

/* ── Clocks ─────────────────────────────────────────────────────── */

uint64_t bole_monotonic_ms(void) {
  return (uint64_t)GetTickCount64();
}

int64_t bole_wall_ms(void) {
  FILETIME file_time;
  ULARGE_INTEGER value;
  GetSystemTimeAsFileTime(&file_time);
  value.LowPart = file_time.dwLowDateTime;
  value.HighPart = file_time.dwHighDateTime;
  /* 100-ns intervals since 1601-01-01 → Unix milliseconds. */
  return (int64_t)(value.QuadPart / 10000ULL) - 11644473600000LL;
}

int bole_atomic_load(const volatile int* value) {
  return (int)InterlockedCompareExchange((volatile LONG*)value, 0, 0);
}

void bole_atomic_store(volatile int* value, int next) {
  InterlockedExchange((volatile LONG*)value, (LONG)next);
}

/* ── Child process ──────────────────────────────────────────────── */

/** Append one argv element using the MSVC runtime quoting rules. */
static int append_argument(char** buffer, size_t* length, size_t* capacity, const char* argument) {
  const char* p = argument;
  int needs_quote = argument[0] == '\0' || strpbrk(argument, " \t\n\v\"") != NULL;
  if (*length + 2 > *capacity) {
    size_t next = *capacity == 0 ? 256 : *capacity * 2;
    char* grown = (char*)realloc(*buffer, next);
    if (grown == NULL) return -1;
    *buffer = grown;
    *capacity = next;
  }
  if (needs_quote) (*buffer)[(*length)++] = '"';
  for (;;) {
    size_t backslashes = 0;
    while (*p == '\\') {
      backslashes++;
      p++;
    }
    if (*p == '\0') {
      if (needs_quote) {
        for (size_t i = 0; i < backslashes * 2; i++) {
          if (*length + 2 >= *capacity) {
            size_t next = *capacity * 2;
            char* grown = (char*)realloc(*buffer, next);
            if (grown == NULL) return -1;
            *buffer = grown;
            *capacity = next;
          }
          (*buffer)[(*length)++] = '\\';
        }
        if (*length + 2 > *capacity) {
          size_t next = *capacity * 2;
          char* grown = (char*)realloc(*buffer, next);
          if (grown == NULL) return -1;
          *buffer = grown;
          *capacity = next;
        }
        (*buffer)[(*length)++] = '"';
      } else {
        for (size_t i = 0; i < backslashes; i++) {
          if (*length + 2 > *capacity) {
            size_t next = *capacity * 2;
            char* grown = (char*)realloc(*buffer, next);
            if (grown == NULL) return -1;
            *buffer = grown;
            *capacity = next;
          }
          (*buffer)[(*length)++] = '\\';
        }
      }
      break;
    }
    if (*p == '"') {
      for (size_t i = 0; i < backslashes * 2 + 1; i++) {
        if (*length + 2 > *capacity) {
          size_t next = *capacity * 2;
          char* grown = (char*)realloc(*buffer, next);
          if (grown == NULL) return -1;
          *buffer = grown;
          *capacity = next;
        }
        (*buffer)[(*length)++] = '\\';
      }
    } else {
      for (size_t i = 0; i < backslashes; i++) {
        if (*length + 2 > *capacity) {
          size_t next = *capacity * 2;
          char* grown = (char*)realloc(*buffer, next);
          if (grown == NULL) return -1;
          *buffer = grown;
          *capacity = next;
        }
        (*buffer)[(*length)++] = '\\';
      }
    }
    if (*length + 2 > *capacity) {
      size_t next = *capacity * 2;
      char* grown = (char*)realloc(*buffer, next);
      if (grown == NULL) return -1;
      *buffer = grown;
      *capacity = next;
    }
    (*buffer)[(*length)++] = *p++;
  }
  (*buffer)[*length] = '\0';
  return 0;
}

static wchar_t* build_command_line(char* const argv[]) {
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  for (size_t i = 0; argv[i] != NULL; i++) {
    if (i > 0) {
      if (length + 2 > capacity) {
        size_t next = capacity == 0 ? 256 : capacity * 2;
        char* grown = (char*)realloc(buffer, next);
        if (grown == NULL) {
          free(buffer);
          return NULL;
        }
        buffer = grown;
        capacity = next;
      }
      buffer[length++] = ' ';
      buffer[length] = '\0';
    }
    if (append_argument(&buffer, &length, &capacity, argv[i]) != 0) {
      free(buffer);
      return NULL;
    }
  }
  wchar_t* wide = utf8_to_wide(buffer == NULL ? "" : buffer);
  free(buffer);
  return wide;
}

static wchar_t* build_environment(char* const envp[]) {
  size_t capacity = 4096;
  wchar_t* block = (wchar_t*)calloc(capacity, sizeof(wchar_t));
  size_t used = 0;
  if (block == NULL) return NULL;
  if (envp != NULL) {
    for (size_t i = 0; envp[i] != NULL; i++) {
      wchar_t* entry = utf8_to_wide(envp[i]);
      if (entry == NULL) continue;
      size_t entry_len = wcslen(entry);
      while (used + entry_len + 3 > capacity) {
        capacity *= 2;
        wchar_t* grown = (wchar_t*)realloc(block, capacity * sizeof(wchar_t));
        if (grown == NULL) {
          free(entry);
          free(block);
          return NULL;
        }
        block = grown;
      }
      memcpy(block + used, entry, (entry_len + 1) * sizeof(wchar_t));
      used += entry_len + 1;
      free(entry);
    }
  }
  block[used] = L'\0'; /* double NUL terminates the block */
  block[used + 1] = L'\0';
  return block;
}

static int create_pipe(HANDLE* read_end, HANDLE* write_end) {
  SECURITY_ATTRIBUTES attributes;
  memset(&attributes, 0, sizeof(attributes));
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  if (!CreatePipe(read_end, write_end, &attributes, 0)) {
    return -1;
  }
  return 0;
}

int bole_process_spawn(bole_process* process, const char* exe,
                       char* const argv[], const char* cwd,
                       char* const envp[]) {
  if (process == NULL || exe == NULL || argv == NULL) return -1;
  memset(process, 0, sizeof(*process));
  process->stdin_write = INVALID_HANDLE_VALUE;
  process->stdout_read = INVALID_HANDLE_VALUE;
  process->stderr_read = INVALID_HANDLE_VALUE;
  if (bole_mutex_init(&process->lock) != 0) return -1;
  if (bole_cond_init(&process->cond) != 0) {
    bole_mutex_destroy(&process->lock);
    return -1;
  }

  HANDLE child_stdin = INVALID_HANDLE_VALUE;
  HANDLE child_stdout = INVALID_HANDLE_VALUE;
  HANDLE child_stderr = INVALID_HANDLE_VALUE;
  HANDLE stdin_write = INVALID_HANDLE_VALUE;
  HANDLE stdout_read = INVALID_HANDLE_VALUE;
  HANDLE stderr_read = INVALID_HANDLE_VALUE;
  wchar_t* exe_wide = NULL;
  wchar_t* command_line = NULL;
  wchar_t* environment = NULL;
  wchar_t* cwd_wide = NULL;
  HANDLE job = NULL;

  if (create_pipe(&child_stdin, &stdin_write) != 0) goto fail;
  if (create_pipe(&stdout_read, &child_stdout) != 0) goto fail;
  if (create_pipe(&stderr_read, &child_stderr) != 0) goto fail;

  /* The child inherits the child-side ends; the parent ends must not leak. */
  SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

  exe_wide = utf8_to_wide(exe);
  command_line = build_command_line(argv);
  environment = build_environment(envp);
  cwd_wide = cwd == NULL ? NULL : utf8_to_wide(cwd);
  if (exe_wide == NULL || command_line == NULL || environment == NULL) goto fail;

  STARTUPINFOW startup;
  memset(&startup, 0, sizeof(startup));
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = child_stdin;
  startup.hStdOutput = child_stdout;
  startup.hStdError = child_stderr;

  DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW;
  if (!CreateProcessW(exe_wide, command_line, NULL, NULL, TRUE, flags,
                      environment, cwd_wide, &startup, &process->info)) {
    goto fail;
  }

  job = CreateJobObjectW(NULL, NULL);
  if (job != NULL) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    memset(&limits, 0, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                &limits, sizeof(limits)) != 0) {
      if (!AssignProcessToJobObject(job, process->info.hProcess)) {
        CloseHandle(job);
        job = NULL;
      }
    } else {
      CloseHandle(job);
      job = NULL;
    }
  }
  if (job == NULL) {
    TerminateProcess(process->info.hProcess, 1);
    goto fail;
  }
  ResumeThread(process->info.hThread);
  CloseHandle(process->info.hThread);
  process->info.hThread = NULL;

  CloseHandle(child_stdin);
  CloseHandle(child_stdout);
  CloseHandle(child_stderr);
  free(exe_wide);
  free(command_line);
  free(environment);
  free(cwd_wide);

  process->job = job;
  process->stdin_write = stdin_write;
  process->stdout_read = stdout_read;
  process->stderr_read = stderr_read;
  process->spawned = 1;
  return 0;

fail:
  if (process->info.hThread != NULL && process->info.hThread != INVALID_HANDLE_VALUE) {
    CloseHandle(process->info.hThread);
  }
  if (process->info.hProcess != NULL && process->info.hProcess != INVALID_HANDLE_VALUE) {
    CloseHandle(process->info.hProcess);
  }
  if (job != NULL) CloseHandle(job);
  if (child_stdin != INVALID_HANDLE_VALUE) CloseHandle(child_stdin);
  if (child_stdout != INVALID_HANDLE_VALUE) CloseHandle(child_stdout);
  if (child_stderr != INVALID_HANDLE_VALUE) CloseHandle(child_stderr);
  if (stdin_write != INVALID_HANDLE_VALUE) CloseHandle(stdin_write);
  if (stdout_read != INVALID_HANDLE_VALUE) CloseHandle(stdout_read);
  if (stderr_read != INVALID_HANDLE_VALUE) CloseHandle(stderr_read);
  free(exe_wide);
  free(command_line);
  free(environment);
  free(cwd_wide);
  bole_cond_destroy(&process->cond);
  bole_mutex_destroy(&process->lock);
  return -1;
}

int bole_process_wait(bole_process* process, int* exit_code) {
  if (process == NULL || !process->spawned) return -1;
  DWORD waited = WaitForSingleObject(process->info.hProcess, INFINITE);
  if (waited != WAIT_OBJECT_0) return -1;
  DWORD code = 0;
  GetExitCodeProcess(process->info.hProcess, &code);
  bole_mutex_lock(&process->lock);
  process->exit_code = (int)code;
  process->exited = 1;
  bole_cond_broadcast(&process->cond);
  bole_mutex_unlock(&process->lock);
  if (exit_code != NULL) *exit_code = (int)code;
  return 0;
}

void bole_process_terminate(bole_process* process) {
  if (process == NULL || !process->spawned) return;
  /* Kill the whole tree; the job also enforces this when the handle closes. */
  if (process->job != NULL) {
    TerminateJobObject(process->job, 1);
  } else {
    TerminateProcess(process->info.hProcess, 1);
  }
}

void bole_process_close(bole_process* process) {
  if (process == NULL) return;
  if (process->stdin_write != INVALID_HANDLE_VALUE && process->stdin_write != NULL) {
    CloseHandle(process->stdin_write);
    process->stdin_write = INVALID_HANDLE_VALUE;
  }
  if (process->stdout_read != INVALID_HANDLE_VALUE && process->stdout_read != NULL) {
    CloseHandle(process->stdout_read);
    process->stdout_read = INVALID_HANDLE_VALUE;
  }
  if (process->stderr_read != INVALID_HANDLE_VALUE && process->stderr_read != NULL) {
    CloseHandle(process->stderr_read);
    process->stderr_read = INVALID_HANDLE_VALUE;
  }
  if (process->job != NULL) {
    CloseHandle(process->job);
    process->job = NULL;
  }
  if (process->info.hProcess != NULL && process->info.hProcess != INVALID_HANDLE_VALUE) {
    CloseHandle(process->info.hProcess);
    process->info.hProcess = NULL;
  }
}

int bole_path_exists(const char* path) {
  if (path == NULL) return 0;
  wchar_t* wide = utf8_to_wide(path);
  if (wide == NULL) return 0;
  DWORD attributes = GetFileAttributesW(wide);
  free(wide);
  return attributes != INVALID_FILE_ATTRIBUTES;
}

int bole_mkdir_p(const char* path) {
  if (path == NULL || path[0] == '\0') return -1;
  size_t length = strlen(path);
  char* copy = (char*)malloc(length + 1);
  if (copy == NULL) return -1;
  memcpy(copy, path, length + 1);
  for (size_t i = 1; i <= length; i++) {
    if (copy[i] == '/' || copy[i] == '\\' || copy[i] == '\0') {
      /* Skip the drive root ("C:") and the leading separator of a UNC path;
         CreateDirectoryW rejects both as standalone names. */
      if ((i == 2 && copy[1] == ':')
          || (i == 1 && (copy[0] == '\\' || copy[0] == '/'))) {
        continue;
      }
      char saved = copy[i];
      copy[i] = '\0';
      wchar_t* wide = utf8_to_wide(copy);
      if (wide != NULL && !CreateDirectoryW(wide, NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
          free(wide);
          free(copy);
          return -1;
        }
      }
      free(wide);
      copy[i] = saved;
    }
  }
  free(copy);
  return 0;
}

char* bole_getcwd(void) {
  DWORD needed = GetCurrentDirectoryW(0, NULL);
  if (needed == 0) return NULL;
  wchar_t* wide = (wchar_t*)calloc(needed, sizeof(wchar_t));
  if (wide == NULL) return NULL;
  if (GetCurrentDirectoryW(needed, wide) == 0) {
    free(wide);
    return NULL;
  }
  char* utf8 = wide_to_utf8(wide);
  free(wide);
  return utf8;
}

char** bole_environment_list(void) {
  LPWCH block = GetEnvironmentStringsW();
  if (block == NULL) return NULL;
  size_t count = 0;
  for (const wchar_t* p = block; *p != L'\0'; p += wcslen(p) + 1) {
    /* Hidden per-drive entries start with '='; they are not KEY=VALUE pairs. */
    if (*p != L'=') {
      count++;
    }
  }
  char** list = (char**)calloc(count + 1, sizeof(char*));
  if (list == NULL) {
    FreeEnvironmentStringsW(block);
    return NULL;
  }
  size_t out = 0;
  for (const wchar_t* p = block; *p != L'\0'; p += wcslen(p) + 1) {
    if (*p == L'=') {
      continue;
    }
    list[out] = wide_to_utf8(p);
    if (list[out] == NULL) {
      bole_environment_free(list);
      FreeEnvironmentStringsW(block);
      return NULL;
    }
    out++;
  }
  FreeEnvironmentStringsW(block);
  return list;
}

void bole_environment_free(char** environment) {
  if (environment == NULL) return;
  for (size_t i = 0; environment[i] != NULL; i++) {
    free(environment[i]);
  }
  free(environment);
}

FILE* bole_fopen_utf8(const char* path, const char* mode) {
  if (path == NULL || mode == NULL) return NULL;
  wchar_t* wide_path = utf8_to_wide(path);
  wchar_t* wide_mode = utf8_to_wide(mode);
  if (wide_path == NULL || wide_mode == NULL) {
    free(wide_path);
    free(wide_mode);
    return NULL;
  }
  FILE* file = _wfopen(wide_path, wide_mode);
  free(wide_path);
  free(wide_mode);
  return file;
}
