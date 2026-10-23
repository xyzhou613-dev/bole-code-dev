/* Bole SDK Win32 GUI example: input, submit, live status, appended log,
 * cancel, multi-turn. The SDK runs on a worker thread; callbacks only post
 * messages to the UI thread.
 *
 * Paths: command line (runtime home) > BOLE_RUNTIME_DIR/BOLE_DSH_HOME >
 * package layout derived from this exe (.../samples/gui-win32 -> package). */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bole_agent.h"

#define WM_BOLE_EVENT (WM_APP + 1)
#define ID_INPUT 101
#define ID_SUBMIT 102
#define ID_CANCEL 103
#define ID_STATUS 104
#define ID_RESULT 105
#define PATH_CAP 4096

static HWND g_input, g_submit, g_cancel, g_status, g_result;
static bole_handle* g_handle;
static bole_session* g_session;
static HANDLE g_worker;
static volatile LONG g_running;
static char g_runtime_dir[PATH_CAP];
static char g_dsh_home[PATH_CAP];
static char g_path_error[512];
static char g_session_id[64];
static char g_log[256 * 1024];


static char* wide_to_utf8(const wchar_t* text) {
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

static wchar_t* utf8_to_wide(const char* text) {
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

static void copy_string(char* destination, size_t capacity, const char* source) {
  if (capacity == 0) return;
  if (source == NULL) { destination[0] = '\0'; return; }
  snprintf(destination, capacity, "%s", source);
}

static int path_exists(const char* path, int directory) {
  wchar_t* wide = utf8_to_wide(path);
  if (wide == NULL) return 0;
  DWORD attributes = GetFileAttributesW(wide);
  free(wide);
  if (attributes == INVALID_FILE_ATTRIBUTES) return 0;
  return directory ? ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                   : ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

static void trim_trailing_separators(char* path) {
  size_t length = strlen(path);
  while (length > 1 && (path[length - 1] == '\\' || path[length - 1] == '/')) {
    path[--length] = '\0';
  }
}

static void parent_dir(char* path) {
  trim_trailing_separators(path);
  char* separator = strrchr(path, '\\');
  char* forward = strrchr(path, '/');
  if (forward != NULL && (separator == NULL || forward > separator)) separator = forward;
  if (separator == NULL) return;
  if (separator == path + 2 && path[1] == ':') { path[3] = '\0'; return; }
  *separator = '\0';
}

static void dir_of(char* path) {
  parent_dir(path);
}

static int env_path(const wchar_t* name, char* destination, size_t capacity) {
  wchar_t value[PATH_CAP];
  DWORD length = GetEnvironmentVariableW(name, value, PATH_CAP);
  if (length == 0 || length >= PATH_CAP) return 0;
  char* utf8 = wide_to_utf8(value);
  if (utf8 == NULL) return 0;
  copy_string(destination, capacity, utf8);
  free(utf8);
  return 1;
}

static void resolve_paths(int argc, wchar_t** argv) {
  g_runtime_dir[0] = '\0';
  g_dsh_home[0] = '\0';
  g_path_error[0] = '\0';

  if (argc >= 3 && argv[1][0] != L'\0' && argv[2][0] != L'\0') {
    char* runtime = wide_to_utf8(argv[1]);
    char* home = wide_to_utf8(argv[2]);
    if (runtime != NULL && home != NULL) {
      copy_string(g_runtime_dir, sizeof(g_runtime_dir), runtime);
      copy_string(g_dsh_home, sizeof(g_dsh_home), home);
    }
    free(runtime);
    free(home);
  } else if (env_path(L"BOLE_RUNTIME_DIR", g_runtime_dir, sizeof(g_runtime_dir))
             && env_path(L"BOLE_DSH_HOME", g_dsh_home, sizeof(g_dsh_home))) {
    /* Environment override. */
  } else {
    wchar_t module[PATH_CAP];
    DWORD length = GetModuleFileNameW(NULL, module, PATH_CAP);
    if (length == 0 || length >= PATH_CAP) {
      copy_string(g_path_error, sizeof(g_path_error), "cannot locate bole_gui.exe");
      return;
    }
    char* exe = wide_to_utf8(module);
    if (exe == NULL) {
      copy_string(g_path_error, sizeof(g_path_error), "cannot decode bole_gui.exe path");
      return;
    }
    char package[PATH_CAP];
    copy_string(package, sizeof(package), exe);
    dir_of(package);   /* samples\gui-win32 */
    parent_dir(package); /* samples */
    parent_dir(package); /* package root */
    snprintf(g_runtime_dir, sizeof(g_runtime_dir), "%s\\runtime", package);
    snprintf(g_dsh_home, sizeof(g_dsh_home), "%s\\home", package);
    free(exe);
  }

  char node[PATH_CAP];
  snprintf(node, sizeof(node), "%s\\node.exe", g_runtime_dir);
  if (!path_exists(node, 0)) {
    snprintf(g_path_error, sizeof(g_path_error),
             "runtime not found: %s (pass <runtime_dir> <dsh_home>)", g_runtime_dir);
  } else if (!path_exists(g_dsh_home, 1)) {
    snprintf(g_path_error, sizeof(g_path_error),
             "home not found: %s (pass <runtime_dir> <dsh_home>)", g_dsh_home);
  }
}

static void post_text(HWND hwnd, int control, const char* text) {
  size_t n = strlen(text) + 1;
  char* copy = (char*)malloc(n);
  if (copy == NULL) return;
  memcpy(copy, text, n);
  if (!PostMessageA(hwnd, WM_BOLE_EVENT, (WPARAM)control, (LPARAM)copy)) free(copy);
}

static void append_result(HWND hwnd, const char* text) {
  size_t used = strlen(g_log);
  size_t room = sizeof(g_log) - used - 1;
  if (room > 0 && text != NULL) {
    size_t length = strlen(text);
    if (length > room - 2) length = room - 2;
    memcpy(g_log + used, text, length);
    used += length;
    g_log[used++] = '\r';
    g_log[used++] = '\n';
    g_log[used] = '\0';
  }
  SetWindowTextA(hwnd, g_log);
  SendMessageA(hwnd, EM_SETSEL, (WPARAM)strlen(g_log), (LPARAM)strlen(g_log));
  SendMessageA(hwnd, EM_SCROLLCARET, 0, 0);
}

static void BOLE_CALL on_event(void* user, const bole_event* event) {
  HWND hwnd = (HWND)user;
  switch (event->type) {
    case BOLE_EVENT_RUNTIME_READY: post_text(hwnd, ID_STATUS, "runtime ready"); break;
    case BOLE_EVENT_TASK_STARTED: post_text(hwnd, ID_STATUS, "task started"); break;
    case BOLE_EVENT_TOOL_CALL_STARTED: post_text(hwnd, ID_STATUS, "tool running"); break;
    case BOLE_EVENT_TOOL_CALL_FINISHED: post_text(hwnd, ID_STATUS, "tool done"); break;
    case BOLE_EVENT_TEXT_DELTA:
    case BOLE_EVENT_ASSISTANT_MESSAGE:
      post_text(hwnd, ID_RESULT, event->payload_json); break;
    case BOLE_EVENT_TASK_FINISHED:
      if (event->payload_json != NULL && strstr(event->payload_json, "aborted") != NULL) {
        post_text(hwnd, ID_STATUS, "finished (aborted)");
      } else {
        post_text(hwnd, ID_STATUS, "finished");
      }
      InterlockedExchange(&g_running, 0);
      break;
    case BOLE_EVENT_TASK_FAILED:
      post_text(hwnd, ID_STATUS, "failed");
      InterlockedExchange(&g_running, 0);
      break;
    case BOLE_EVENT_RUNTIME_FAILED:
    case BOLE_EVENT_RUNTIME_EXITED:
      post_text(hwnd, ID_STATUS, "runtime stopped");
      InterlockedExchange(&g_running, 0);
      break;
    default: break;
  }
}

static DWORD WINAPI worker(LPVOID param) {
  HWND hwnd = (HWND)param;
  if (g_handle == NULL) {
    bole_config config;
    memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    config.runtime_dir = g_runtime_dir;
    config.dsh_home = g_dsh_home;
    config.model_provider = "bole";
    config.model_name = "qwen3.8-27b";
    config.event_cb = on_event;
    config.user_data = hwnd;

    if (g_runtime_dir[0] == '\0' || g_dsh_home[0] == '\0') {
      post_text(hwnd, ID_STATUS, g_path_error[0] != '\0' ? g_path_error
                                                         : "runtime/home not set");
      InterlockedExchange(&g_running, 0);
      return 0;
    }
    if (bole_init(&config, &g_handle) != 0 || bole_wait_ready(g_handle, 30000) != 0) {
      if (g_handle != NULL) {
        bole_release(g_handle);
        g_handle = NULL;
      }
      post_text(hwnd, ID_STATUS, "runtime not ready");
      InterlockedExchange(&g_running, 0);
      return 0;
    }
  }

  if (g_session == NULL) {
    snprintf(g_session_id, sizeof(g_session_id), "gui-%lu",
             (unsigned long)GetCurrentProcessId());
    if (bole_session_create(g_handle, g_session_id, NULL, &g_session) != 0) {
      post_text(hwnd, ID_STATUS, "session create failed");
      InterlockedExchange(&g_running, 0);
      return 0;
    }
  }

  char text[4096];
  GetWindowTextA(g_input, text, sizeof(text));
  if (text[0] == '\0') {
    post_text(hwnd, ID_STATUS, "empty input");
    InterlockedExchange(&g_running, 0);
    return 0;
  }
  if (bole_submit(g_session, text) != 0) {
    post_text(hwnd, ID_STATUS, "submit failed");
    InterlockedExchange(&g_running, 0);
  }
  return 0;
}

static void on_command(HWND hwnd, int id) {
  if (id == ID_SUBMIT) {
    if (InterlockedCompareExchange(&g_running, 1, 0) != 0) return;
    if (g_worker != NULL) {
      CloseHandle(g_worker);
      g_worker = NULL;
    }
    SetWindowTextA(g_status, "starting");
    g_worker = CreateThread(NULL, 0, worker, hwnd, 0, NULL);
    if (g_worker == NULL) {
      InterlockedExchange(&g_running, 0);
      SetWindowTextA(g_status, "thread failed");
    }
  } else if (id == ID_CANCEL) {
    if (g_session != NULL) bole_cancel(g_session);
  }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE:
      g_input = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                              10, 10, 460, 24, hwnd, (HMENU)ID_INPUT, NULL, NULL);
      g_submit = CreateWindowA("BUTTON", "Submit", WS_CHILD | WS_VISIBLE,
                               10, 44, 90, 26, hwnd, (HMENU)ID_SUBMIT, NULL, NULL);
      g_cancel = CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE,
                               110, 44, 90, 26, hwnd, (HMENU)ID_CANCEL, NULL, NULL);
      g_status = CreateWindowA("STATIC", g_path_error[0] != '\0' ? g_path_error : "idle",
                               WS_CHILD | WS_VISIBLE, 220, 48, 250, 20,
                               hwnd, (HMENU)ID_STATUS, NULL, NULL);
      g_result = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
                               ES_AUTOVSCROLL | WS_VSCROLL,
                               10, 80, 460, 240, hwnd, (HMENU)ID_RESULT, NULL, NULL);
      return 0;
    case WM_COMMAND: on_command(hwnd, LOWORD(wp)); return 0;
    case WM_BOLE_EVENT: {
      char* text = (char*)lp;
      if ((int)wp == ID_STATUS) SetWindowTextA(g_status, text);
      else append_result(g_result, text);
      free(text);
      return 0;
    }
    case WM_DESTROY:
      if (g_session != NULL) bole_cancel(g_session);
      if (g_worker != NULL) {
        WaitForSingleObject(g_worker, 5000);
        CloseHandle(g_worker);
        g_worker = NULL;
      }
      if (g_handle != NULL) bole_release(g_handle);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmd, int show) {
  (void)prev;
  (void)cmd;
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  resolve_paths(argc, argv);
  if (argv != NULL) LocalFree(argv);

  WNDCLASSA cls;
  memset(&cls, 0, sizeof(cls));
  cls.lpfnWndProc = WndProc;
  cls.hInstance = instance;
  cls.lpszClassName = "BoleSdkGui";
  cls.hCursor = LoadCursor(NULL, IDC_ARROW);
  RegisterClassA(&cls);
  HWND hwnd = CreateWindowA("BoleSdkGui", "Bole SDK", WS_OVERLAPPEDWINDOW,
                            100, 100, 500, 380, NULL, NULL, instance, NULL);
  ShowWindow(hwnd, show);
  MSG msg;
  while (GetMessageA(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return 0;
}
