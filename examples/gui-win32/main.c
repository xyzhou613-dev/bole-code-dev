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
#include <stdarg.h>
#include "bole_agent.h"

#define WM_BOLE_EVENT (WM_APP + 1)
#define WM_BOLE_RSI (WM_APP + 2)
#define ID_INPUT 101
#define ID_SUBMIT 102
#define ID_CANCEL 103
#define ID_STATUS 104
#define ID_RESULT 105
#define ID_RSI 106
#define ID_OPEN_LOG 107
#define ID_THINK 108
#define ID_MAX_TOKENS 109
#define PATH_CAP 4096

static HWND g_input, g_submit, g_cancel, g_status, g_result, g_rsi, g_think, g_max_tokens_edit;
static bole_handle* g_handle;
static bole_session* g_session;
static HANDLE g_worker;
static volatile LONG g_running;
static volatile LONG g_rsi_enabled;
static volatile LONG g_rsi_pending;
static volatile LONG g_has_turn;
static char g_runtime_dir[PATH_CAP];
static char g_dsh_home[PATH_CAP];
static char g_path_error[512];
static char g_session_id[64];
static char g_log[256 * 1024];
static char g_log_path[PATH_CAP];
static char g_raw_path[PATH_CAP];
static FILE* g_log_file;
static FILE* g_raw_file;
static unsigned g_turn_no;
static ULONGLONG g_task_tick;
static int g_answer_started;
static int g_reasoning_started;
static char g_pending_prompt[4096];
static char g_reasoning_effort[32] = "off";
static char g_session_reasoning_effort[32] = "";
static int g_max_tokens = 8192;
static int g_session_max_tokens = 0;
static unsigned g_session_seq;

static void open_log_file(void);


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
  size_t length = text == NULL ? 0 : strlen(text);
  /* Keep the newest part instead of silently dropping new events. */
  if (used + length + 2 >= sizeof(g_log)) {
    size_t keep = sizeof(g_log) / 2;
    size_t drop = used > keep ? used - keep : 0;
    if (drop > 0) {
      memmove(g_log, g_log + drop, used - drop);
      used -= drop;
      g_log[used] = '\0';
    }
  }
  size_t room = sizeof(g_log) - used - 1;
  if (room > 2 && length > 0) {
    if (length > room - 2) length = room - 2;
    memcpy(g_log + used, text, length);
    used += length;
    g_log[used++] = '\r';
    g_log[used++] = '\n';
    g_log[used] = '\0';
  }
  wchar_t* wide = utf8_to_wide(g_log);
  if (wide != NULL) {
    SetWindowTextW(hwnd, wide);
    free(wide);
  }
  int wide_length = GetWindowTextLengthW(hwnd);
  SendMessageW(hwnd, EM_SETSEL, (WPARAM)wide_length, (LPARAM)wide_length);
  SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
}

static void open_log_file(void) {
  char dir[PATH_CAP];
  SYSTEMTIME now;
  wchar_t* wide;

  if (g_dsh_home[0] == '\0') return;
  snprintf(dir, sizeof(dir), "%s\\logs", g_dsh_home);
  wide = utf8_to_wide(dir);
  if (wide != NULL) {
    (void)CreateDirectoryW(wide, NULL);
    free(wide);
  }

  GetLocalTime(&now);
  snprintf(g_log_path, sizeof(g_log_path),
           "%s\\gui-%04u%02u%02u-%02u%02u%02u-%lu.log",
           dir, (unsigned)now.wYear, (unsigned)now.wMonth, (unsigned)now.wDay,
           (unsigned)now.wHour, (unsigned)now.wMinute, (unsigned)now.wSecond,
           (unsigned long)GetCurrentProcessId());
  snprintf(g_raw_path, sizeof(g_raw_path),
           "%s\\gui-%04u%02u%02u-%02u%02u%02u-%lu.jsonl",
           dir, (unsigned)now.wYear, (unsigned)now.wMonth, (unsigned)now.wDay,
           (unsigned)now.wHour, (unsigned)now.wMinute, (unsigned)now.wSecond,
           (unsigned long)GetCurrentProcessId());
  wide = utf8_to_wide(g_log_path);
  if (wide != NULL) {
    g_log_file = _wfopen(wide, L"ab");
    free(wide);
  }
  wide = utf8_to_wide(g_raw_path);
  if (wide != NULL) {
    g_raw_file = _wfopen(wide, L"ab");
    free(wide);
  }
  if (g_log_file != NULL) {
    static const unsigned char utf8_bom[] = { 0xEF, 0xBB, 0xBF };
    fwrite(utf8_bom, 1, sizeof(utf8_bom), g_log_file);
    fprintf(g_log_file, "# Bole SDK GUI log\r\n");
    fprintf(g_log_file, "# pid=%lu\r\n# runtime=%s\r\n# home=%s\r\n",
            (unsigned long)GetCurrentProcessId(), g_runtime_dir, g_dsh_home);
    fflush(g_log_file);
  }
}

static void log_file_printf(const char* format, ...) {
  va_list args;
  if (g_log_file == NULL || format == NULL) return;
  va_start(args, format);
  vfprintf(g_log_file, format, args);
  va_end(args);
  fflush(g_log_file);
}


static const char* event_type_name(int type) {
  switch (type) {
    case BOLE_EVENT_RUNTIME_READY: return "RUNTIME_READY";
    case BOLE_EVENT_RUNTIME_FAILED: return "RUNTIME_FAILED";
    case BOLE_EVENT_RUNTIME_EXITED: return "RUNTIME_EXITED";
    case BOLE_EVENT_TASK_STARTED: return "TASK_STARTED";
    case BOLE_EVENT_STEP_STARTED: return "STEP_STARTED";
    case BOLE_EVENT_STEP_FINISHED: return "STEP_FINISHED";
    case BOLE_EVENT_PLAN_UPDATED: return "PLAN_UPDATED";
    case BOLE_EVENT_MODEL_SELECTED: return "MODEL_SELECTED";
    case BOLE_EVENT_RSI_STATE: return "RSI_STATE";
    case BOLE_EVENT_TEXT_DELTA: return "TEXT_DELTA";
    case BOLE_EVENT_REASONING_DELTA: return "REASONING_DELTA";
    case BOLE_EVENT_ASSISTANT_MESSAGE: return "ASSISTANT_MESSAGE";
    case BOLE_EVENT_TOOL_CALL_STARTED: return "TOOL_CALL_STARTED";
    case BOLE_EVENT_TOOL_CALL_FINISHED: return "TOOL_CALL_FINISHED";
    case BOLE_EVENT_VERIFY_CHECK: return "VERIFY_CHECK";
    case BOLE_EVENT_TASK_FINISHED: return "TASK_FINISHED";
    case BOLE_EVENT_TASK_FAILED: return "TASK_FAILED";
    default: return "UNKNOWN";
  }
}

static void log_raw_event(const bole_event* event) {
  SYSTEMTIME now;
  if (g_raw_file == NULL || event == NULL) return;
  GetLocalTime(&now);
  fprintf(g_raw_file,
          "{\"time\":\"%04u-%02u-%02uT%02u:%02u:%02u\",\"type\":%d,\"name\":\"%s\",\"payload\":%s}\n",
          (unsigned)now.wYear, (unsigned)now.wMonth, (unsigned)now.wDay,
          (unsigned)now.wHour, (unsigned)now.wMinute, (unsigned)now.wSecond,
          (int)event->type, event_type_name((int)event->type),
          event->payload_json != NULL ? event->payload_json : "null");
  fflush(g_raw_file);
}

static int json_string_field(const char* json, const char* key, char* out, size_t capacity) {
  char pattern[64];
  const char* p;
  size_t n = 0;
  if (json == NULL || key == NULL || out == NULL || capacity == 0) return 0;
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  p = strstr(json, pattern);
  if (p == NULL) return 0;
  p += strlen(pattern);
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  if (*p != ':') return 0;
  p++;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  if (*p != '"') return 0;
  p++;
  while (*p != '\0' && *p != '"') {
    unsigned char c = (unsigned char)*p++;
    if (c == '\\') {
      if (*p == '\0') break;
      c = (unsigned char)*p++;
      switch (c) {
        case 'n': c = '\n'; break;
        case 'r': c = '\r'; break;
        case 't': c = '\t'; break;
        case '"': c = '"'; break;
        case '\\': c = '\\'; break;
        case '/': c = '/'; break;
        case 'u': {
          /* Keep non-ASCII text readable enough for a demo log. */
          unsigned value = 0;
          int i;
          for (i = 0; i < 4 && *p != '\0'; i++) {
            char h = *p++;
            value <<= 4;
            if (h >= '0' && h <= '9') value |= (unsigned)(h - '0');
            else if (h >= 'a' && h <= 'f') value |= (unsigned)(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') value |= (unsigned)(h - 'A' + 10);
          }
          c = value < 0x80 ? (unsigned char)value : '?';
          break;
        }
        default: break;
      }
    }
    if (n + 1 < capacity) out[n++] = (char)c;
  }
  out[n] = '\0';
  return 1;
}

static int json_value_field(const char* json, const char* key, char* out, size_t capacity) {
  char pattern[64];
  const char* p;
  size_t n = 0;
  if (json == NULL || key == NULL || out == NULL || capacity == 0) return 0;
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  p = strstr(json, pattern);
  if (p == NULL) return 0;
  p += strlen(pattern);
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  if (*p != ':') return 0;
  p++;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  if (*p == '{' || *p == '[') {
    char open = *p;
    char close = open == '{' ? '}' : ']';
    int depth = 0;
    int in_string = 0;
    int escaped = 0;
    while (*p != '\0') {
      char c = *p++;
      if (in_string) {
        if (escaped) escaped = 0;
        else if (c == '\\') escaped = 1;
        else if (c == '"') in_string = 0;
      } else {
        if (c == '"') in_string = 1;
        else if (c == open) depth++;
        else if (c == close) {
          depth--;
          if (depth == 0) {
            if (n + 1 < capacity) out[n++] = c;
            break;
          }
        }
      }
      if (n + 1 < capacity) out[n++] = c;
    }
  } else {
    while (*p != '\0' && *p != ',' && *p != '}' && *p != '\n' && *p != '\r') {
      if (n + 1 < capacity) out[n++] = *p;
      p++;
    }
  }
  out[n] = '\0';
  return 1;
}

static void log_task_start(void) {
  SYSTEMTIME now;
  if (g_log_file == NULL) return;
  GetLocalTime(&now);
  g_turn_no++;
  g_task_tick = GetTickCount64();
  g_answer_started = 0;
  g_reasoning_started = 0;
  log_file_printf("\r\n============================================================\r\n");
  log_file_printf("任务 #%u  %04u-%02u-%02u %02u:%02u:%02u\r\n",
                  g_turn_no, (unsigned)now.wYear, (unsigned)now.wMonth,
                  (unsigned)now.wDay, (unsigned)now.wHour,
                  (unsigned)now.wMinute, (unsigned)now.wSecond);
  log_file_printf("用户：\r\n%s\r\n", g_pending_prompt[0] != '\0' ? g_pending_prompt : "(空)");
  log_file_printf("RSI：%s\r\n", InterlockedCompareExchange(&g_rsi_enabled, 0, 0) ? "on" : "off");
}

static void log_tool_start(const char* payload) {
  char tool[160];
  char args[512];
  if (g_log_file == NULL || !json_string_field(payload, "tool", tool, sizeof(tool))) return;
  log_file_printf("工具：%s\r\n", tool);
  if (json_value_field(payload, "args", args, sizeof(args))) {
    log_file_printf("参数：%s\r\n", args);
  }
}

static void log_tool_done(const char* payload) {
  char tool[160];
  char result[1024];
  if (g_log_file == NULL) return;
  if (!json_string_field(payload, "tool", tool, sizeof(tool))) return;
  if (json_string_field(payload, "resultSummary", result, sizeof(result))) {
    if (payload != NULL && strstr(payload, "\"isError\":true") != NULL) {
      log_file_printf("结果（错误）：%s\r\n", result);
    } else {
      log_file_printf("结果：%s\r\n", result);
    }
  } else {
    log_file_printf("结果：%s 已完成\r\n", tool);
  }
}

static void log_reasoning(const char* text) {
  if (g_log_file == NULL || text == NULL || text[0] == '\0') return;
  if (!g_reasoning_started) {
    log_file_printf("\r\n思考：\r\n");
    g_reasoning_started = 1;
  }
  log_file_printf("%s\r\n", text);
}

static void log_rsi_state(const char* payload) {
  char status[64];
  char bundle[160];
  if (g_log_file == NULL || payload == NULL || strstr(payload, "\"delivery\"") == NULL) return;
  if (!json_string_field(payload, "status", status, sizeof(status)) || status[0] == '\0') return;
  if (json_string_field(payload, "bundleId", bundle, sizeof(bundle))) {
    log_file_printf("经验：%s  bundle=%s\r\n", status, bundle);
  } else {
    log_file_printf("经验：%s\r\n", status);
  }
}


static void log_answer(const char* text) {
  if (g_log_file == NULL || text == NULL || text[0] == '\0') return;
  if (!g_answer_started) {
    log_file_printf("\r\n回答：\r\n");
    g_answer_started = 1;
  }
  log_file_printf("%s\r\n", text);
}

static void log_task_end(const char* status, const char* payload) {
  char message[512];
  double seconds = 0.0;
  if (g_log_file == NULL) return;
  if (g_task_tick != 0) {
    seconds = (double)(GetTickCount64() - g_task_tick) / 1000.0;
  }
  log_file_printf("\r\n状态：%s    耗时：%.1fs\r\n", status != NULL ? status : "unknown", seconds);
  if (payload != NULL && json_string_field(payload, "message", message, sizeof(message))) {
    log_file_printf("错误：%s\r\n", message);
  }
  log_file_printf("============================================================\r\n");
}

static void post_log(HWND hwnd, const char* prefix, const char* value) {
  size_t size;
  char* line;
  if (value == NULL) return;
  size = strlen(prefix) + strlen(value) + 2;
  line = (char*)malloc(size);
  if (line == NULL) return;
  snprintf(line, size, "%s%s", prefix, value);
  post_text(hwnd, ID_RESULT, line);
  free(line);
}

static void BOLE_CALL on_event(void* user, const bole_event* event) {
  HWND hwnd = (HWND)user;
  char tool[160];
  char result[512];
  char message[512];
  char line[1024];

  log_raw_event(event);
  switch (event->type) {
    case BOLE_EVENT_RUNTIME_READY: post_text(hwnd, ID_STATUS, "runtime ready"); break;
    case BOLE_EVENT_RSI_STATE:
      log_rsi_state(event->payload_json);
      break;
    case BOLE_EVENT_TASK_STARTED:
      log_task_start();
      g_log[0] = '\0';
      SetWindowTextW(g_result, L"");
      InterlockedExchange(&g_has_turn, 1);
      post_text(hwnd, ID_STATUS, "task started");
      post_text(hwnd, ID_RESULT, "[task] started");
      break;
    case BOLE_EVENT_TOOL_CALL_STARTED:
      log_tool_start(event->payload_json);
      post_text(hwnd, ID_STATUS, "tool running");
      if (json_string_field(event->payload_json, "tool", tool, sizeof(tool))) {
        post_log(hwnd, "[tool] ", tool);
      }
      break;
    case BOLE_EVENT_TOOL_CALL_FINISHED:
      log_tool_done(event->payload_json);
      post_text(hwnd, ID_STATUS, "tool done");
      if (json_string_field(event->payload_json, "tool", tool, sizeof(tool))) {
        if (json_string_field(event->payload_json, "resultSummary", result, sizeof(result))) {
          snprintf(line, sizeof(line), "[tool] %s -> %.300s", tool, result);
        } else {
          snprintf(line, sizeof(line), "[tool] %s -> done", tool);
        }
        post_text(hwnd, ID_RESULT, line);
      }
      break;
    case BOLE_EVENT_TEXT_DELTA:
      /* Deltas are intentionally not rendered as raw JSON; the final message is shown. */
      break;
    case BOLE_EVENT_ASSISTANT_MESSAGE: {
      size_t cap = event->payload_json == NULL ? 0 : strlen(event->payload_json) + 1;
      char* answer = cap == 0 ? NULL : (char*)malloc(cap);
      char* reasoning = cap == 0 ? NULL : (char*)malloc(cap);
      if (reasoning != NULL && json_string_field(event->payload_json, "reasoning", reasoning, cap)) {
        log_reasoning(reasoning);
      }
      if (answer != NULL && json_string_field(event->payload_json, "text", answer, cap)) {
        log_answer(answer);
        if (answer[0] != '\0') post_log(hwnd, "[answer] ", answer);
      }
      free(reasoning);
      free(answer);
      break;
    }
    case BOLE_EVENT_TASK_FINISHED:
      if (event->payload_json != NULL && strstr(event->payload_json, "aborted") != NULL) {
        log_task_end("aborted", NULL);
        post_text(hwnd, ID_STATUS, "finished (aborted)");
        post_text(hwnd, ID_RESULT, "[task] finished (aborted)");
      } else {
        log_task_end("finished", NULL);
        post_text(hwnd, ID_STATUS, "finished");
        post_text(hwnd, ID_RESULT, "[task] finished");
      }
      InterlockedExchange(&g_running, 0);
      break;
    case BOLE_EVENT_TASK_FAILED:
      log_task_end("failed", event->payload_json);
      post_text(hwnd, ID_STATUS, "failed");
      if (json_string_field(event->payload_json, "message", message, sizeof(message))) {
        post_log(hwnd, "[task] failed: ", message);
      } else {
        post_text(hwnd, ID_RESULT, "[task] failed");
      }
      InterlockedExchange(&g_running, 0);
      break;
    case BOLE_EVENT_RUNTIME_FAILED:
    case BOLE_EVENT_RUNTIME_EXITED:
      post_text(hwnd, ID_STATUS, "runtime stopped");
      post_text(hwnd, ID_RESULT, "[runtime] stopped");
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
    config.model_name = NULL;   /* use runtime agent-default-model */
    config.rsi_enabled = (int32_t)InterlockedCompareExchange(&g_rsi_enabled, 0, 0);
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
    bole_rsi_state state;
    memset(&state, 0, sizeof(state));
    state.size = sizeof(state);
    int requested = (int)InterlockedCompareExchange(&g_rsi_enabled, 0, 0);
    if (bole_rsi_get(g_handle, &state) == 0) {
      if (requested != state.enabled) {
        if (bole_rsi_set(g_handle, requested) != 0) {
          requested = state.enabled;
          InterlockedExchange(&g_rsi_enabled, requested);
        }
      }
      PostMessageA(hwnd, WM_BOLE_RSI, (WPARAM)requested, 0);
    }
  }

  if (InterlockedCompareExchange(&g_rsi_pending, 0, 0)) {
    (void)bole_rsi_set(g_handle, (int)InterlockedCompareExchange(&g_rsi_enabled, 0, 0));
    InterlockedExchange(&g_rsi_pending, 0);
    g_session = NULL;
    InterlockedExchange(&g_has_turn, 0);
  }

  if (g_session != NULL
      && (strcmp(g_reasoning_effort, g_session_reasoning_effort) != 0
          || g_max_tokens != g_session_max_tokens)) {
    g_session = NULL;
    g_session_seq++;
  }

  if (g_session == NULL) {
    snprintf(g_session_id, sizeof(g_session_id), "gui-%lu-%u",
             (unsigned long)GetCurrentProcessId(), g_session_seq);
    bole_session_options options;
    memset(&options, 0, sizeof(options));
    options.size = sizeof(options);
    options.reasoning_effort = g_reasoning_effort[0] != '\0' ? g_reasoning_effort : NULL;
    options.max_tokens = g_max_tokens;
    if (bole_session_create(g_handle, g_session_id, &options, &g_session) != 0) {
      post_text(hwnd, ID_STATUS, "session create failed");
      InterlockedExchange(&g_running, 0);
      return 0;
    }
    copy_string(g_session_reasoning_effort, sizeof(g_session_reasoning_effort), g_reasoning_effort);
    g_session_max_tokens = g_max_tokens;
    if (InterlockedCompareExchange(&g_rsi_enabled, 0, 0)) {
      (void)bole_session_set_preset(g_session, "rsi-infer");
    }
  }

  wchar_t wide_text[4096];
  GetWindowTextW(g_input, wide_text, 4096);
  char* text = wide_to_utf8(wide_text);
  if (text == NULL || text[0] == '\0') {
    free(text);
    post_text(hwnd, ID_STATUS, "empty input");
    InterlockedExchange(&g_running, 0);
    return 0;
  }
  snprintf(g_pending_prompt, sizeof(g_pending_prompt), "%s", text);
  int submit_result = bole_submit(g_session, text);
  free(text);
  if (submit_result != 0) {
    post_text(hwnd, ID_STATUS, "submit failed");
    InterlockedExchange(&g_running, 0);
  }
  return 0;
}

static void rsi_status(HWND hwnd, int enabled, const char* text) {
  SendMessageA(g_rsi, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
  if (text != NULL) post_text(hwnd, ID_STATUS, text);
}

static void apply_rsi(HWND hwnd, int enabled) {
  if (g_handle == NULL) {
    rsi_status(hwnd, enabled, enabled ? "RSI on (first task)" : "RSI off (first task)");
    return;
  }
  if (g_running) {
    InterlockedExchange(&g_rsi_pending, 1);
    rsi_status(hwnd, enabled, enabled ? "RSI on (next task)" : "RSI off (next task)");
    return;
  }
  if (bole_rsi_set(g_handle, enabled) != 0) {
    rsi_status(hwnd, !enabled, "RSI update failed");
    return;
  }
  if (g_session != NULL && !InterlockedCompareExchange(&g_has_turn, 0, 0)) {
    const char* preset = enabled ? "rsi-infer" : "standard";
    if (bole_session_set_preset(g_session, preset) == 0) {
      rsi_status(hwnd, enabled, enabled ? "RSI on" : "RSI off");
      return;
    }
  }
  if (g_session != NULL) {
    g_session = NULL;
    InterlockedExchange(&g_has_turn, 0);
  }
  rsi_status(hwnd, enabled, enabled ? "RSI on (next task)" : "RSI off (next task)");
}

static void on_command(HWND hwnd, int id) {
  if (id == ID_SUBMIT) {
    if (InterlockedCompareExchange(&g_running, 1, 0) != 0) return;
    if (g_worker != NULL) {
      CloseHandle(g_worker);
      g_worker = NULL;
    }
    int think_index = (int)SendMessageA(g_think, CB_GETCURSEL, 0, 0);
    if (think_index == CB_ERR || SendMessageA(g_think, CB_GETLBTEXT, think_index,
                                              (LPARAM)g_reasoning_effort) == CB_ERR
        || strcmp(g_reasoning_effort, "default") == 0) {
      g_reasoning_effort[0] = '\0';
    }
    char max_text[32];
    GetWindowTextA(g_max_tokens_edit, max_text, sizeof(max_text));
    g_max_tokens = atoi(max_text);
    if (g_max_tokens < 0) g_max_tokens = 0;
    g_log[0] = '\0';
    SetWindowTextW(g_result, L"");
    SetWindowTextA(g_status, "starting");
    g_worker = CreateThread(NULL, 0, worker, hwnd, 0, NULL);
    if (g_worker == NULL) {
      InterlockedExchange(&g_running, 0);
      SetWindowTextA(g_status, "thread failed");
    }
  } else if (id == ID_CANCEL) {
    if (g_session != NULL) bole_cancel(g_session);
  } else if (id == ID_RSI) {
    int enabled = SendMessageA(g_rsi, BM_GETCHECK, 0, 0) == BST_CHECKED;
    InterlockedExchange(&g_rsi_enabled, enabled);
    apply_rsi(hwnd, enabled);
  } else if (id == ID_OPEN_LOG) {
    wchar_t* wide = g_log_path[0] == '\0' ? NULL : utf8_to_wide(g_log_path);
    if (g_log_file == NULL || wide == NULL) {
      SetWindowTextA(g_status, "log unavailable");
      free(wide);
    } else {
      ShellExecuteW(hwnd, L"open", wide, NULL, NULL, SW_SHOWNORMAL);
      free(wide);
    }
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
      g_rsi = CreateWindowA("BUTTON", "RSI", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                            210, 46, 70, 24, hwnd, (HMENU)ID_RSI, NULL, NULL);
      g_status = CreateWindowA("STATIC", g_path_error[0] != '\0' ? g_path_error : "idle",
                               WS_CHILD | WS_VISIBLE, 290, 48, 95, 20,
                               hwnd, (HMENU)ID_STATUS, NULL, NULL);
      CreateWindowA("BUTTON", "Open Log", WS_CHILD | WS_VISIBLE,
                    390, 44, 70, 26, hwnd, (HMENU)ID_OPEN_LOG, NULL, NULL);
      CreateWindowA("STATIC", "Think", WS_CHILD | WS_VISIBLE,
                    10, 78, 40, 20, hwnd, NULL, NULL, NULL);
      g_think = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST,
                              52, 74, 82, 140, hwnd, (HMENU)ID_THINK, NULL, NULL);
      CreateWindowA("STATIC", "Max out", WS_CHILD | WS_VISIBLE,
                    144, 78, 48, 20, hwnd, NULL, NULL, NULL);
      g_max_tokens_edit = CreateWindowA("EDIT", "8192",
                                        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                                        194, 74, 62, 22, hwnd, (HMENU)ID_MAX_TOKENS, NULL, NULL);
      static const char* levels[] = { "default", "off", "low", "medium", "high", "xhigh" };
      for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        SendMessageA(g_think, CB_ADDSTRING, 0, (LPARAM)levels[i]);
      }
      SendMessageA(g_think, CB_SETCURSEL, 0, 0);
      g_result = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
                               ES_AUTOVSCROLL | WS_VSCROLL,
                               10, 108, 460, 252, hwnd, (HMENU)ID_RESULT, NULL, NULL);
      return 0;
    case WM_COMMAND: on_command(hwnd, LOWORD(wp)); return 0;
    case WM_BOLE_RSI:
      InterlockedExchange(&g_rsi_enabled, (LONG)wp);
      SendMessageA(g_rsi, BM_SETCHECK, wp != 0 ? BST_CHECKED : BST_UNCHECKED, 0);
      return 0;
    case WM_CTLCOLORSTATIC: {
      HDC hdc = (HDC)wp;
      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
      return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
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
      if (g_log_file != NULL) {
        fclose(g_log_file);
        g_log_file = NULL;
      }
      if (g_raw_file != NULL) {
        fclose(g_raw_file);
        g_raw_file = NULL;
      }
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
  open_log_file();

  WNDCLASSA cls;
  memset(&cls, 0, sizeof(cls));
  cls.style = CS_HREDRAW | CS_VREDRAW;
  cls.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
  cls.lpfnWndProc = WndProc;
  cls.hInstance = instance;
  cls.lpszClassName = "BoleSdkGui";
  cls.hCursor = LoadCursor(NULL, IDC_ARROW);
  RegisterClassA(&cls);
  HWND hwnd = CreateWindowA("BoleSdkGui", "Bole SDK",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            100, 100, 500, 410, NULL, NULL, instance, NULL);
  ShowWindow(hwnd, show);
  MSG msg;
  while (GetMessageA(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return 0;
}
