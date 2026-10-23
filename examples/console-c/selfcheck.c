/* B8 self-check: C ABI + runtime + toy MCP on an installed package.
 * Usage: bole_selfcheck [prompt]
 *        bole_selfcheck <runtime_dir> <dsh_home> "<prompt>"  (legacy)  */
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "bole_agent.h"
#include "example_paths.h"

static void sleep_ms(int ms) {
#ifdef _WIN32
  Sleep((DWORD)ms);
#else
  usleep((useconds_t)ms * 1000);
#endif
}

typedef struct check_state {
  char session[128];
  volatile int ready;
  volatile int failed;
  volatile int task_started;
  volatile int task_finished;
  volatile int task_aborted;
  volatile int tool_started;
  volatile int tool_finished;
  volatile int rsi_state;
  volatile int rsi_receipt;
  volatile int finishes;
  char rsi_payload[512];
} check_state;

static check_state g;

static void BOLE_CALL on_event(void* user, const bole_event* event) {
  (void)user;
  if (event->type == BOLE_EVENT_RUNTIME_READY) { g.ready = 1; return; }
  if (event->type == BOLE_EVENT_RUNTIME_FAILED || event->type == BOLE_EVENT_RUNTIME_EXITED) {
    g.failed = 1;
    return;
  }
  if (event->session_id == NULL || strcmp(event->session_id, g.session) != 0) return;
  switch (event->type) {
    case BOLE_EVENT_TASK_STARTED: g.task_started = 1; break;
    case BOLE_EVENT_TOOL_CALL_STARTED: g.tool_started = 1; break;
    case BOLE_EVENT_TOOL_CALL_FINISHED: g.tool_finished = 1; break;
    case BOLE_EVENT_RSI_STATE:
      g.rsi_state = 1;
      if (event->payload_json != NULL) {
        snprintf(g.rsi_payload, sizeof(g.rsi_payload), "%s", event->payload_json);
        if (strstr(event->payload_json, "\"source\":\"receipt\"") != NULL
            || strstr(event->payload_json, "\"delivery\":") != NULL) {
          g.rsi_receipt = 1;
        }
      }
      break;
    case BOLE_EVENT_TASK_FINISHED:
      g.task_finished = 1;
      g.finishes++;
      if (event->payload_json != NULL && strstr(event->payload_json, "aborted") != NULL) {
        g.task_aborted = 1;
      }
      break;
    case BOLE_EVENT_TASK_FAILED:
      g.task_finished = 1;
      g.finishes++;
      g.failed = 1;
      break;
    default: break;
  }
}

static int wait_for(volatile int* flag, int timeout_ms) {
  int waited = 0;
  while (!*flag && !g.failed && waited < timeout_ms) {
    sleep_ms(50);
    waited += 50;
  }
  return *flag ? 0 : -1;
}

static int print_error(bole_handle* handle, const char* what, int code) {
  char buffer[512];
  int32_t last = 0;
  bole_last_error(handle, &last, buffer, sizeof(buffer));
  fprintf(stderr, "FAIL %s (rc=%d last=%d %s)\n", what, code, last, buffer);
  return -1;
}

static int run_turn(bole_handle* handle, const char* prompt, const char* preset,
                    const char* session_id, int cancel_after_start) {
  bole_session_options options;
  memset(&options, 0, sizeof(options));
  options.size = sizeof(options);
  options.preset = preset;
  bole_session* session = NULL;
  memset((void*)&g.session, 0, sizeof(g.session));
  snprintf(g.session, sizeof(g.session), "%s", session_id);
  g.task_started = g.task_finished = g.task_aborted = 0;
  g.tool_started = g.tool_finished = g.rsi_state = g.rsi_receipt = 0;
  g.finishes = 0;

  int rc = bole_session_create(handle, session_id, &options, &session);
  if (rc != 0) return print_error(handle, "session_create", rc);
  rc = bole_submit(session, prompt);
  if (rc != 0) return print_error(handle, "submit", rc);

  if (wait_for((volatile int*)&g.task_started, 30000) != 0) {
    fprintf(stderr, "FAIL task did not start\n");
    return -1;
  }
  if (cancel_after_start) {
    sleep_ms(150);
    rc = bole_cancel(session);
    if (rc != 0) return print_error(handle, "cancel", rc);
  }
  if (wait_for((volatile int*)&g.task_finished, 90000) != 0) {
    fprintf(stderr, "FAIL task did not finish\n");
    return -1;
  }
  sleep_ms(250);
  if (g.finishes != 1) {
    fprintf(stderr, "FAIL expected one terminal event, got %d\n", g.finishes);
    return -1;
  }
  if (cancel_after_start) {
    if (!g.task_aborted) {
      fprintf(stderr, "FAIL cancel did not produce outcome=aborted\n");
      return -1;
    }
  } else {
    if (g.task_aborted) {
      fprintf(stderr, "FAIL completed task reported aborted\n");
      return -1;
    }
    if (!g.tool_started || !g.tool_finished) {
      fprintf(stderr, "FAIL toy MCP call missing (started=%d finished=%d)\n",
              g.tool_started, g.tool_finished);
      return -1;
    }
  }
  return 0;
}

int main(int argc, char** argv) {
  char runtime[BOLE_EXAMPLE_PATH_CAP];
  char home[BOLE_EXAMPLE_PATH_CAP];
  const char* prompt = BOLE_EXAMPLE_DEFAULT_PROMPT;

  if (argc >= 4) {
    snprintf(runtime, sizeof(runtime), "%s", argv[1]);
    snprintf(home, sizeof(home), "%s", argv[2]);
    prompt = argv[3];
  } else if (argc == 3) {
    snprintf(runtime, sizeof(runtime), "%s", argv[1]);
    snprintf(home, sizeof(home), "%s", argv[2]);
  } else {
    if (bole_example_resolve(runtime, sizeof(runtime), home, sizeof(home)) != 0) {
      fprintf(stderr, "cannot find runtime/home; pass <runtime_dir> <dsh_home> <prompt> or run from the package\n");
      return 2;
    }
    if (argc == 2) prompt = argv[1];
  }

  memset(&g, 0, sizeof(g));
  bole_config config;
  memset(&config, 0, sizeof(config));
  config.size = sizeof(config);
  config.runtime_dir = runtime;
  config.dsh_home = home;
  config.model_provider = "bole";
  config.model_name = "qwen3.8-27b";
  config.event_cb = on_event;

  bole_handle* handle = NULL;
  int rc = bole_init(&config, &handle);
  if (rc != 0) {
    fprintf(stderr, "FAIL init (rc=%d)\n", rc);
    return 1;
  }
  rc = bole_wait_ready(handle, 30000);
  if (rc != 0) {
    print_error(handle, "wait_ready", rc);
    bole_release(handle);
    return 1;
  }
  printf("PASS runtime ready\n");

  bole_rsi_state rsi;
  memset(&rsi, 0, sizeof(rsi));
  rsi.size = sizeof(rsi);
  if (bole_rsi_get(handle, &rsi) != 0 || !rsi.composed || rsi.preset[0] == '\0') {
    fprintf(stderr, "FAIL preset plane not composed\n");
    bole_release(handle);
    return 1;
  }
  printf("PASS preset plane (%s)\n", rsi.preset);

  char standard_id[64];
  char rsi_id[64];
  char cancel_id[64];
#ifdef _WIN32
  snprintf(standard_id, sizeof(standard_id), "selfcheck-standard-%lu", (unsigned long)GetCurrentProcessId());
  snprintf(rsi_id, sizeof(rsi_id), "selfcheck-rsi-%lu", (unsigned long)GetCurrentProcessId());
  snprintf(cancel_id, sizeof(cancel_id), "selfcheck-cancel-%lu", (unsigned long)GetCurrentProcessId());
#else
  snprintf(standard_id, sizeof(standard_id), "selfcheck-standard-%ld", (long)getpid());
  snprintf(rsi_id, sizeof(rsi_id), "selfcheck-rsi-%ld", (long)getpid());
  snprintf(cancel_id, sizeof(cancel_id), "selfcheck-cancel-%ld", (long)getpid());
#endif

  if (run_turn(handle, prompt, "standard", standard_id, 0) != 0) {
    bole_release(handle);
    return 1;
  }
  if (g.rsi_receipt) {
    fprintf(stderr, "FAIL standard session produced an RSI receipt: %s\n", g.rsi_payload);
    bole_release(handle);
    return 1;
  }
  printf("PASS standard tool turn\n");

  if (run_turn(handle, prompt, "rsi-infer", rsi_id, 0) != 0) {
    bole_release(handle);
    return 1;
  }
  if (!g.rsi_receipt
      || (strstr(g.rsi_payload, "\"delivered\"") == NULL
          && strstr(g.rsi_payload, "\"available\"") == NULL)) {
    fprintf(stderr, "FAIL rsi-infer produced no delivered/available receipt: %s\n", g.rsi_payload);
    bole_release(handle);
    return 1;
  }
  printf("PASS rsi-infer receipt\n");

  if (run_turn(handle, prompt, "standard", cancel_id, 1) != 0) {
    bole_release(handle);
    return 1;
  }
  printf("PASS cancel aborted turn\n");

  char trace[1024];
  snprintf(trace, sizeof(trace), "%s/selfcheck-trace.jsonl", home);
  uint32_t count = 0;
  rc = bole_trace_export(handle, standard_id, 0, trace, &count);
  if (rc != 0 || count == 0) {
    print_error(handle, "trace_export", rc);
    bole_release(handle);
    return 1;
  }
  FILE* file = fopen(trace, "rb");
  if (file == NULL) {
    fprintf(stderr, "FAIL trace file not readable: %s\n", trace);
    bole_release(handle);
    return 1;
  }
  fclose(file);
  printf("PASS trace export (%u events)\n", count);

  rc = bole_release(handle);
  if (rc != 0) {
    fprintf(stderr, "FAIL release (rc=%d)\n", rc);
    return 1;
  }
  printf("PASS selfcheck\n");
#ifdef _WIN32
  if (argc == 1 && getenv("BOLE_NO_PAUSE") == NULL) {
    printf("\nPress Enter to close...");
    fflush(stdout);
    (void)getchar();
  }
#endif
  return 0;
}
