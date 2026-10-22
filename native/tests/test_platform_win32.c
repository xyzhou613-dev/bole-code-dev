/**
 * @file test_platform_win32.c
 * @brief Win32 smoke for B3: UTF-8 file paths, pipe-backed child processes,
 * and Job Object termination.
 */

#include "../src/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int failures = 0;

#define CHECK(condition, message) \
  do { \
    if (!(condition)) { \
      fprintf(stderr, "FAIL %s (line %d)\n", message, __LINE__); \
      failures++; \
    } \
  } while (0)

static wchar_t* wide_from_utf8(const char* text) {
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

static char* utf8_from_wide(const wchar_t* text) {
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

static char* temp_path(const char* leaf) {
  wchar_t temp[MAX_PATH];
  DWORD length = GetTempPathW(MAX_PATH, temp);
  if (length == 0 || length >= MAX_PATH) return NULL;
  size_t temp_length = wcslen(temp);
  while (temp_length > 0 && (temp[temp_length - 1] == L'\\' || temp[temp_length - 1] == L'/')) {
    temp[--temp_length] = L'\0';
  }
  wchar_t full[MAX_PATH];
  _snwprintf(full, MAX_PATH, L"%s\\bole-platform-%lu-%s",
             temp, (unsigned long)GetCurrentProcessId(),
             leaf == NULL ? L"dir" : L"dir");
  return utf8_from_wide(full);
}

static void test_utf8_path(void) {
  char* root = temp_path(NULL);
  CHECK(root != NULL, "temp path");
  if (root == NULL) return;
  int mkdir_result = bole_mkdir_p(root);
  if (mkdir_result != 0) fprintf(stderr, "mkdir root=%s error=%lu\n", root, (unsigned long)GetLastError());
  CHECK(mkdir_result == 0, "mkdir utf8 path");
  char file[MAX_PATH];
  snprintf(file, sizeof(file), "%s\\中文 带空格.txt", root);
  FILE* out = bole_fopen_utf8(file, "wb");
  CHECK(out != NULL, "open utf8 file for write");
  if (out != NULL) {
    CHECK(fwrite("hello", 1, 5, out) == 5, "write utf8 file");
    fclose(out);
  }
  CHECK(bole_path_exists(file) == 1, "utf8 path exists");
  FILE* in = bole_fopen_utf8(file, "rb");
  CHECK(in != NULL, "open utf8 file for read");
  if (in != NULL) {
    char buffer[8] = {0};
    CHECK(fread(buffer, 1, 5, in) == 5, "read utf8 file");
    CHECK(strcmp(buffer, "hello") == 0, "utf8 file content");
    fclose(in);
  }
  wchar_t* wide_file = wide_from_utf8(file);
  wchar_t* wide_root = wide_from_utf8(root);
  if (wide_file != NULL) DeleteFileW(wide_file);
  if (wide_root != NULL) RemoveDirectoryW(wide_root);
  free(wide_file);
  free(wide_root);
  free(root);
}

static char* comspec(void) {
  wchar_t value[MAX_PATH];
  DWORD length = GetEnvironmentVariableW(L"ComSpec", value, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return NULL;
  return utf8_from_wide(value);
}

static void test_process_pipe(void) {
  char* cmd = comspec();
  CHECK(cmd != NULL, "ComSpec");
  if (cmd == NULL) return;
  char* argv[] = {cmd, "/d", "/s", "/c", "echo platform-ok", NULL};
  bole_process process;
  CHECK(bole_process_spawn(&process, cmd, argv, NULL, NULL) == 0, "spawn pipe child");
  if (process.spawned) {
    CloseHandle(process.stdin_write);
    process.stdin_write = INVALID_HANDLE_VALUE;
    char output[256] = {0};
    DWORD read = 0;
    BOOL ok = ReadFile(process.stdout_read, output, sizeof(output) - 1, &read, NULL);
    CHECK(ok && read > 0, "read child stdout");
    CloseHandle(process.stdout_read);
    process.stdout_read = INVALID_HANDLE_VALUE;
    CHECK(strstr(output, "platform-ok") != NULL, "child stdout content");
    int code = -1;
    CHECK(bole_process_wait(&process, &code) == 0, "wait pipe child");
    CHECK(code == 0, "pipe child exit code");
    bole_process_close(&process);
  }
  free(cmd);
}

static void test_job_terminate(void) {
  char* cmd = comspec();
  CHECK(cmd != NULL, "ComSpec for terminate");
  if (cmd == NULL) return;
  char* argv[] = {cmd, "/d", "/s", "/c", "ping -n 30 127.0.0.1 >nul", NULL};
  bole_process process;
  CHECK(bole_process_spawn(&process, cmd, argv, NULL, NULL) == 0, "spawn long child");
  if (process.spawned) {
    Sleep(300);
    bole_process_terminate(&process);
    int code = 0;
    CHECK(bole_process_wait(&process, &code) == 0, "wait terminated child");
    CHECK(code != 0, "terminated child exit code");
    bole_process_close(&process);
  }
  free(cmd);
}

int main(void) {
  test_utf8_path();
  test_process_pipe();
  test_job_terminate();
  if (failures != 0) {
    fprintf(stderr, "%d platform test(s) failed\n", failures);
    return 1;
  }
  printf("win32 platform tests passed\n");
  return 0;
}
