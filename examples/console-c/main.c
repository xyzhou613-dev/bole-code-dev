/* Bole SDK console example: one tool call, end to end.
 * Usage: console <runtime_dir> <dsh_home> "prompt"                       */
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "bole_agent.h"

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
  if (argc < 4) { fprintf(stderr, "usage: %s <runtime_dir> <dsh_home> <prompt>\n", argv[0]); return 2; }
  bole_config config;
  memset(&config, 0, sizeof(config));
  config.size = sizeof(config);
  config.runtime_dir = argv[1];
  config.dsh_home = argv[2];
  config.model_provider = "bole";
  config.model_name = "qwen3.8-27b";
  config.event_cb = on_event;

  bole_handle* handle = NULL;
  if (bole_init(&config, &handle) != 0) { fprintf(stderr, "init failed\n"); return 1; }
  if (bole_wait_ready(handle, 30000) != 0) { fprintf(stderr, "runtime not ready\n"); bole_release(handle); return 1; }

  bole_session* session = NULL;
  if (bole_session_create(handle, "console-1", NULL, &session) != 0) { fprintf(stderr, "create failed\n"); bole_release(handle); return 1; }
  if (bole_submit(session, argv[3]) != 0) { fprintf(stderr, "submit failed\n"); bole_release(handle); return 1; }

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
  return g_done && !g_failed ? 0 : 1;
}
