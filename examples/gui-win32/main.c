/* Bole SDK Win32 GUI example: input, submit, live status, result, cancel.
 * The SDK runs on a worker thread; the event callback only posts messages. */
#include <windows.h>
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

static HWND g_input, g_submit, g_cancel, g_status, g_result;
static bole_handle* g_handle;
static bole_session* g_session;
static volatile LONG g_running;

static void post_text(HWND hwnd, int control, const char* text) {
  size_t n = strlen(text) + 1;
  char* copy = (char*)malloc(n);
  if (copy == NULL) return;
  memcpy(copy, text, n);
  if (!PostMessageA(hwnd, WM_BOLE_EVENT, (WPARAM)control, (LPARAM)copy)) free(copy);
}

static void BOLE_CALL on_event(void* user, const bole_event* event) {
  HWND hwnd = (HWND)user;
  switch (event->type) {
    case BOLE_EVENT_RUNTIME_READY: post_text(hwnd, ID_STATUS, "runtime ready"); break;
    case BOLE_EVENT_TASK_STARTED: post_text(hwnd, ID_STATUS, "task started"); break;
    case BOLE_EVENT_TOOL_CALL_STARTED: post_text(hwnd, ID_STATUS, "tool running"); break;
    case BOLE_EVENT_TOOL_CALL_FINISHED: post_text(hwnd, ID_STATUS, "tool done"); break;
    case BOLE_EVENT_TEXT_DELTA: post_text(hwnd, ID_RESULT, event->payload_json); break;
    case BOLE_EVENT_ASSISTANT_MESSAGE: post_text(hwnd, ID_RESULT, event->payload_json); break;
    case BOLE_EVENT_TASK_FINISHED: post_text(hwnd, ID_STATUS, "finished"); InterlockedExchange(&g_running, 0); break;
    case BOLE_EVENT_TASK_FAILED: post_text(hwnd, ID_STATUS, "failed"); InterlockedExchange(&g_running, 0); break;
    case BOLE_EVENT_RUNTIME_FAILED:
    case BOLE_EVENT_RUNTIME_EXITED: post_text(hwnd, ID_STATUS, "runtime stopped"); InterlockedExchange(&g_running, 0); break;
    default: break;
  }
}

static DWORD WINAPI worker(LPVOID param) {
  HWND hwnd = (HWND)param;
  bole_config config;
  memset(&config, 0, sizeof(config));
  config.size = sizeof(config);
  config.runtime_dir = getenv("BOLE_RUNTIME_DIR");
  config.dsh_home = getenv("BOLE_DSH_HOME");
  config.model_provider = "bole";
  config.model_name = "qwen3.8-27b";
  config.event_cb = on_event;
  config.user_data = hwnd;

  if (config.runtime_dir == NULL || config.dsh_home == NULL) {
    post_text(hwnd, ID_STATUS, "set BOLE_RUNTIME_DIR and BOLE_DSH_HOME");
    return 0;
  }
  if (bole_init(&config, &g_handle) != 0 || bole_wait_ready(g_handle, 30000) != 0) {
    post_text(hwnd, ID_STATUS, "runtime not ready");
    return 0;
  }
  char text[4096];
  GetWindowTextA(g_input, text, sizeof(text));
  if (bole_session_create(g_handle, "gui-1", NULL, &g_session) != 0 ||
      bole_submit(g_session, text) != 0) {
    post_text(hwnd, ID_STATUS, "submit failed");
  }
  return 0;
}

static void on_command(HWND hwnd, int id) {
  if (id == ID_SUBMIT) {
    if (InterlockedCompareExchange(&g_running, 1, 0) != 0) return;
    SetWindowTextA(g_result, "");
    SetWindowTextA(g_status, "starting");
    CloseHandle(CreateThread(NULL, 0, worker, hwnd, 0, NULL));
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
      g_status = CreateWindowA("STATIC", "idle", WS_CHILD | WS_VISIBLE,
                               220, 48, 250, 20, hwnd, (HMENU)ID_STATUS, NULL, NULL);
      g_result = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
                               ES_AUTOVSCROLL | WS_VSCROLL,
                               10, 80, 460, 240, hwnd, (HMENU)ID_RESULT, NULL, NULL);
      return 0;
    case WM_COMMAND: on_command(hwnd, LOWORD(wp)); return 0;
    case WM_BOLE_EVENT: {
      char* text = (char*)lp;
      SetWindowTextA((int)wp == ID_STATUS ? g_status : g_result, text);
      free(text);
      return 0;
    }
    case WM_DESTROY:
      if (g_handle != NULL) bole_release(g_handle);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmd, int show) {
  (void)prev; (void)cmd;
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
    DispatchMessageA(&msg);
  }
  return 0;
}
