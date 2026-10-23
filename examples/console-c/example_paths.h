/**
 * @file example_paths.h
 * @brief Path resolution shared by the console and self-check examples.
 *
 * Order: BOLE_RUNTIME_DIR/BOLE_DSH_HOME, then the package layout derived from
 * this executable (samples/<example>/<exe> -> package/runtime and package/home).
 */
#ifndef BOLE_EXAMPLE_PATHS_H
#define BOLE_EXAMPLE_PATHS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>
#else
#  include <limits.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif


#define BOLE_EXAMPLE_PATH_CAP 4096
#define BOLE_EXAMPLE_DEFAULT_PROMPT \
  "Call mcp__toy__add with 1+2 and answer with the result."

#ifdef _WIN32
static char* bole_example_utf8(const wchar_t* text) {
  int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
  if (needed <= 0) return NULL;
  char* result = (char*)calloc((size_t)needed, 1);
  if (result == NULL) return NULL;
  if (WideCharToMultiByte(CP_UTF8, 0, text, -1, result, needed, NULL, NULL) <= 0) {
    free(result);
    return NULL;
  }
  return result;
}

/** Convert GetCommandLineW to UTF-8 argv (Windows CRT argv uses the ANSI code page). */
static char** bole_example_utf8_argv(int* argc_out) {
  int wide_count = 0;
  wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &wide_count);
  if (wide == NULL) return NULL;
  char** argv = (char**)calloc((size_t)wide_count + 1, sizeof(char*));
  if (argv == NULL) { LocalFree(wide); return NULL; }
  for (int i = 0; i < wide_count; i++) {
    argv[i] = bole_example_utf8(wide[i]);
    if (argv[i] == NULL) {
      for (int j = 0; j < i; j++) free(argv[j]);
      free(argv);
      LocalFree(wide);
      return NULL;
    }
  }
  argv[wide_count] = NULL;
  LocalFree(wide);
  if (argc_out != NULL) *argc_out = wide_count;
  return argv;
}

static void bole_example_free_argv(char** argv, int argc) {
  if (argv == NULL) return;
  for (int i = 0; i < argc; i++) free(argv[i]);
  free(argv);
}

static wchar_t* bole_example_wide(const char* text) {
  int needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
  if (needed <= 0) return NULL;
  wchar_t* result = (wchar_t*)calloc((size_t)needed, sizeof(wchar_t));
  if (result == NULL) return NULL;
  if (MultiByteToWideChar(CP_UTF8, 0, text, -1, result, needed) <= 0) {
    free(result);
    return NULL;
  }
  return result;
}

static int bole_example_exists(const char* path, int directory) {
  wchar_t* wide = bole_example_wide(path);
  if (wide == NULL) return 0;
  DWORD attributes = GetFileAttributesW(wide);
  free(wide);
  if (attributes == INVALID_FILE_ATTRIBUTES) return 0;
  return directory ? ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                   : ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

static int bole_example_executable(char* path, size_t capacity) {
  wchar_t wide[BOLE_EXAMPLE_PATH_CAP];
  DWORD length = GetModuleFileNameW(NULL, wide, BOLE_EXAMPLE_PATH_CAP);
  if (length == 0 || length >= BOLE_EXAMPLE_PATH_CAP) return -1;
  char* utf8 = bole_example_utf8(wide);
  if (utf8 == NULL) return -1;
  snprintf(path, capacity, "%s", utf8);
  free(utf8);
  return 0;
}
#else
static int bole_example_exists(const char* path, int directory) {
  struct stat info;
  if (stat(path, &info) != 0) return 0;
  return directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode);
}

static int bole_example_executable(char* path, size_t capacity) {
  ssize_t length = readlink("/proc/self/exe", path, capacity - 1);
  if (length <= 0 || (size_t)length >= capacity - 1) return -1;
  path[length] = '\0';
  return 0;
}
#endif

static void bole_example_parent(char* path) {
  size_t length = strlen(path);
  while (length > 1 && (path[length - 1] == '\\' || path[length - 1] == '/')) {
    path[--length] = '\0';
  }
  char* slash = strrchr(path, '\\');
  char* forward = strrchr(path, '/');
  if (forward != NULL && (slash == NULL || forward > slash)) slash = forward;
  if (slash == NULL) return;
#ifdef _WIN32
  if (slash == path + 2 && path[1] == ':') { path[3] = '\0'; return; }
#endif
  *slash = '\0';
}

static void bole_example_dir(char* path) {
  bole_example_parent(path);
}

/** Resolve runtime_dir/home; returns 0 on success, -1 with both strings filled. */
static int bole_example_resolve(char* runtime, size_t runtime_cap,
                                char* home, size_t home_cap) {
  const char* env_runtime = getenv("BOLE_RUNTIME_DIR");
  const char* env_home = getenv("BOLE_DSH_HOME");
  if (env_runtime != NULL && env_home != NULL && env_runtime[0] != '\0' && env_home[0] != '\0') {
    snprintf(runtime, runtime_cap, "%s", env_runtime);
    snprintf(home, home_cap, "%s", env_home);
  } else {
    char exe[BOLE_EXAMPLE_PATH_CAP];
    if (bole_example_executable(exe, sizeof(exe)) != 0) return -1;
    bole_example_dir(exe);   /* samples\<example> */
    bole_example_parent(exe); /* samples */
    bole_example_parent(exe); /* package root */
    snprintf(runtime, runtime_cap, "%s%c%s", exe,
#ifdef _WIN32
             '\\', "runtime");
#else
             '/', "runtime");
#endif
    snprintf(home, home_cap, "%s%c%s", exe,
#ifdef _WIN32
             '\\', "home");
#else
             '/', "home");
#endif
  }
  char node[BOLE_EXAMPLE_PATH_CAP];
#ifdef _WIN32
  snprintf(node, sizeof(node), "%s\\node.exe", runtime);
#else
  snprintf(node, sizeof(node), "%s/node", runtime);
#endif
  if (!bole_example_exists(node, 0) || !bole_example_exists(home, 1)) return -1;
  return 0;
}

#endif /* BOLE_EXAMPLE_PATHS_H */
