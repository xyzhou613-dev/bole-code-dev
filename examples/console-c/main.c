/* Bole SDK console example: one tool call, end to end.
 * Usage: console [prompt]
 *        console <runtime_dir> <dsh_home> "prompt"  (legacy override)   */
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "bole_agent.h"
#include "example_paths.h"

static volatile int g_done = 0;
static volatile int g_failed = 0;

static void BOLE_CALL on_event(void* user, const bole_event* event) {
  (void)user;
  switch (event->type) {
    case BOLE_EVENT_RUNTIME_READY: printf("[ready]\n"); break;
    case BOLE_EVENT_RUNTIME_FAILED:
    case BOLE_EVENT_RUNTIME_EXITED:
      printf("[runtime] %s\n", event->payload_json);
      g_failed = 1; g_done = 1;
      break;
    case BOLE_EVENT_TEXT_DELTA: printf("%s", event->payload_json); break;
    case BOLE_EVENT_ASSISTANT_MESSAGE: printf("\n[answer] %s\n", event->payload_json); break;
    case BOLE_EVENT_TOOL_CALL_STARTED: printf("[tool] start %s\n", event->payload_json); break;
    case BOLE_EVENT_TOOL_CALL_FINISHED: printf("[tool] done %s\n", event->payload_json); break;
    case BOLE_EVENT_TASK_FINISHED: printf("[task] finished\n"); g_done = 1; break;
    case BOLE_EVENT_TASK_FAILED:
      printf("[task] failed %s\n", event->payload_json);
      g_failed = 1; g_done = 1;
      break;
    default: break;
  }
}

int main(int argc, char** argv) {
  char runtime[BOLE_EXAMPLE_PATH_CAP];
  char home[BOLE_EXAMPLE_PATH_CAP];
  const char* prompt = BOLE_EXAMPLE_DEFAULT_PROMPT;
  int rsi = 0;
  int index = 1;

  if (argc > 1 && strcmp(argv[1], "--rsi") == 0) {
    rsi = 1;
    index = 2;
  } else if (argc > 1 && strncmp(argv[1], "--rsi=", 6) == 0) {
    const char* value = argv[1] + 6;
    if (strcmp(value, "on") == 0 || strcmp(value, "1") == 0) rsi = 1;
    else if (strcmp(value, "off") != 0 && strcmp(value, "0") != 0) {
      fprintf(stderr, "usage: %s [--rsi|--rsi=on|off] [prompt]\n", argv[0]);
      return 2;
    }
    index = 2;
  }
  int remaining = argc - index;
  int interactive = 0;

  if (remaining >= 3) {
    snprintf(runtime, sizeof(runtime), "%s", argv[index]);
    snprintf(home, sizeof(home), "%s", argv[index + 1]);
    prompt = argv[index + 2];
  } else if (remaining == 2) {
    snprintf(runtime, sizeof(runtime), "%s", argv[index]);
    snprintf(home, sizeof(home), "%s", argv[index + 1]);
  } else {
    if (bole_example_resolve(runtime, sizeof(runtime), home, sizeof(home)) != 0) {
      fprintf(stderr, "cannot find runtime/home; pass <runtime_dir> <dsh_home> <prompt> or run from the package\n");
      return 2;
    }
    if (remaining == 1) prompt = argv[index];
    else interactive = 1;
  }

  bole_config config;
  memset(&config, 0, sizeof(config));
  config.size = sizeof(config);
  config.runtime_dir = runtime;
  config.dsh_home = home;
  config.model_provider = "bole";
  config.model_name = "qwen3.8-27b";
  config.rsi_enabled = rsi;
  config.event_cb = on_event;

  bole_handle* handle = NULL;
  if (bole_init(&config, &handle) != 0) { fprintf(stderr, "init failed\n"); return 1; }
  if (bole_wait_ready(handle, 30000) != 0) { fprintf(stderr, "runtime not ready\n"); bole_release(handle); return 1; }
  if (rsi) {
    bole_rsi_state state;
    memset(&state, 0, sizeof(state));
    state.size = sizeof(state);
    if (bole_rsi_get(handle, &state) == 0) {
      printf("RSI: %s\n", state.enabled ? "on" : "off");
    }
  }

  char session_id[64];
#ifdef _WIN32
  snprintf(session_id, sizeof(session_id), "console-%lu", (unsigned long)GetCurrentProcessId());
#else
  snprintf(session_id, sizeof(session_id), "console-%ld", (long)getpid());
#endif
  bole_session* session = NULL;
  if (bole_session_create(handle, session_id, NULL, &session) != 0) { fprintf(stderr, "create failed\n"); bole_release(handle); return 1; }
  if (bole_submit(session, prompt) != 0) { fprintf(stderr, "submit failed\n"); bole_release(handle); return 1; }

  /* The callback sets g_done on TASK_FINISHED/TASK_FAILED; 60 s bound. */
  for (int i = 0; i < 600 && !g_done; i++) {
#ifdef _WIN32
    Sleep(100);
#else
    usleep(100 * 1000);
#endif
  }
  if (!g_done) { fprintf(stderr, "timeout waiting for the task\n"); }
  bole_release(handle);
#ifdef _WIN32
  if (interactive && getenv("BOLE_NO_PAUSE") == NULL) {
    printf("\nPress Enter to close...");
    fflush(stdout);
    (void)getchar();
  }
#endif
  return g_done && !g_failed ? 0 : 1;
}
