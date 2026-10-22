/**
 * @file test_runtime.c
 * @brief C ABI end-to-end test against the real `--profile sdk` runtime.
 *
 * The runtime tree, DSH_HOME, and working directory are supplied by
 * `scripts/integration-runtime.mjs`; this file never hardcodes a path. It drives
 * handshake -> session/create -> prompt -> projected events -> trace -> cancel
 * -> release through the public ABI and the mock model used by runtime/dev.
 */

#include "bole_agent.h"
#include "../src/json.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_failures = 0;

#define CHECK(condition, message)                                                \
  do {                                                                           \
    if (!(condition)) {                                                          \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (message));       \
      g_failures++;                                                              \
    }                                                                            \
  } while (0)

typedef struct runtime_state {
  bole_mutex lock;
  bole_cond cond;
  bole_handle* handle;
  int ready;
  int terminal_count;
  int aborted;
  int failed;
  int failed_code;
  int rsi_seen;
  int counts[128];
  int order_ok;
  int payload_ok;
  int session_ok;
  uint64_t last_event_id;
  int64_t last_ts;
  char terminal_payload[512];
  char ready_payload[2048];
} runtime_state;

static uint32_t payload_u32(const char* text, const char* key) {
  if (text == NULL) {
    return 0;
  }
  bole_json_member members[16];
  size_t count = 0;
  if (bole_json_parse_object(text, strlen(text), members, 16, &count) != 0) {
    return 0;
  }
  const bole_json_member* member = bole_json_member_get(members, count, key);
  uint64_t value = 0;
  if (member == NULL || bole_json_u64(member, &value) != 0) {
    return 0;
  }
  return (uint32_t)value;
}

static void copy_text(char* out, size_t capacity, const char* text) {
  if (text == NULL) {
    out[0] = '\0';
    return;
  }
  strncpy(out, text, capacity - 1);
  out[capacity - 1] = '\0';
}

static void BOLE_CALL on_event(void* user_data, const bole_event* event) {
  runtime_state* state = (runtime_state*)user_data;
  bole_mutex_lock(&state->lock);
  if (event->type < 128) {
    state->counts[event->type]++;
  }
  if (event->event_id <= state->last_event_id) {
    state->order_ok = 0;
  }
  state->last_event_id = event->event_id;
  if (event->ts_ms < state->last_ts) {
    state->order_ok = 0;
  }
  state->last_ts = event->ts_ms;
  if (event->payload_json == NULL
      || strncmp(event->payload_json, "{\"v\":1", 6) != 0) {
    state->payload_ok = 0;
  }
  if (event->type >= 10 && event->type <= 51) {
    /* Default-scope RSI_STATE is runtime-level and has no session id. */
    int default_rsi = event->type == BOLE_EVENT_RSI_STATE
        && event->payload_json != NULL
        && strstr(event->payload_json, "\"scope\":\"default\"") != NULL;
    if (!default_rsi && strcmp(event->session_id, "c-e2e") != 0) {
      state->session_ok = 0;
    }
  }
  switch (event->type) {
    case BOLE_EVENT_RUNTIME_READY:
      state->ready = 1;
      copy_text(state->ready_payload, sizeof(state->ready_payload),
                event->payload_json);
      break;
    case BOLE_EVENT_RUNTIME_FAILED:
      state->failed = 1;
      state->failed_code = (int)payload_u32(event->payload_json, "code");
      break;
    case BOLE_EVENT_RSI_STATE:
      state->rsi_seen = 1;
      break;
    case BOLE_EVENT_TASK_FINISHED:
      state->terminal_count++;
      if (event->payload_json != NULL
          && strstr(event->payload_json, "\"aborted\"") != NULL) {
        state->aborted = 1;
      }
      copy_text(state->terminal_payload, sizeof(state->terminal_payload),
                event->payload_json);
      break;
    case BOLE_EVENT_TASK_FAILED:
      state->terminal_count++;
      copy_text(state->terminal_payload, sizeof(state->terminal_payload),
                event->payload_json);
      break;
    default:
      break;
  }
  bole_cond_broadcast(&state->cond);
  bole_mutex_unlock(&state->lock);
}

static int wait_until(runtime_state* state, int* flag, int timeout_ms) {
  uint64_t deadline = bole_monotonic_ms() + (uint64_t)timeout_ms;
  int result = 0;
  bole_mutex_lock(&state->lock);
  while (!*flag) {
    uint64_t now = bole_monotonic_ms();
    if (now >= deadline) {
      result = -1;
      break;
    }
    bole_cond_wait(&state->cond, &state->lock, (int)(deadline - now));
  }
  bole_mutex_unlock(&state->lock);
  return result;
}

static int wait_count(runtime_state* state, int type, int count, int timeout_ms) {
  uint64_t deadline = bole_monotonic_ms() + (uint64_t)timeout_ms;
  int result = 0;
  bole_mutex_lock(&state->lock);
  while (state->counts[type] < count) {
    uint64_t now = bole_monotonic_ms();
    if (now >= deadline) {
      result = -1;
      break;
    }
    bole_cond_wait(&state->cond, &state->lock, (int)(deadline - now));
  }
  bole_mutex_unlock(&state->lock);
  return result;
}

static void sleep_ms(int milliseconds) {
  struct timespec duration;
  duration.tv_sec = milliseconds / 1000;
  duration.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
  nanosleep(&duration, NULL);
}

int main(void) {
  const char* runtime_dir = getenv("BOLE_TEST_RUNTIME_DIR");
  const char* dsh_home = getenv("BOLE_TEST_DSH_HOME");
  const char* cwd = getenv("BOLE_TEST_CWD");
  if (runtime_dir == NULL || dsh_home == NULL || cwd == NULL) {
    fprintf(stderr, "BOLE_TEST_RUNTIME_DIR, BOLE_TEST_DSH_HOME, BOLE_TEST_CWD are required\n");
    return 2;
  }

  runtime_state state;
  memset(&state, 0, sizeof(state));
  state.order_ok = 1;
  state.payload_ok = 1;
  state.session_ok = 1;
  if (bole_mutex_init(&state.lock) != 0 || bole_cond_init(&state.cond) != 0) {
    fprintf(stderr, "failed to initialize test primitives\n");
    return 2;
  }

  bole_config config;
  memset(&config, 0, sizeof(config));
  config.size = sizeof(config);
  config.runtime_dir = runtime_dir;
  config.dsh_home = dsh_home;
  config.cwd = cwd;
  config.model_provider = "bole";
  config.model_name = getenv("BOLE_TEST_MODEL") == NULL
      ? "qwen3.8-27b" : getenv("BOLE_TEST_MODEL");
  config.event_cb = on_event;
  config.user_data = &state;

  bole_handle* handle = NULL;
  int32_t code = bole_init(&config, &handle);
  CHECK(code == BOLE_OK && handle != NULL, "bole_init");
  if (handle == NULL) {
    goto done;
  }
  state.handle = handle;
  CHECK(bole_wait_ready(handle, 30000) == BOLE_OK, "bole_wait_ready");
  CHECK(wait_until(&state, &state.ready, 30000) == 0, "RUNTIME_READY event");
  CHECK(strstr(state.ready_payload, "\"capabilities\"") != NULL,
        "RUNTIME_READY capabilities");
  CHECK(state.order_ok == 1, "event order/timestamps");
  CHECK(state.payload_ok == 1, "payload v1");

  bole_session* session = NULL;
  CHECK(bole_session_create(handle, "c-e2e", NULL, &session) == BOLE_OK,
        "session/create");
  CHECK(session != NULL, "session handle");
  if (session == NULL) {
    bole_release(handle);
    handle = NULL;
    goto done;
  }
  CHECK(bole_submit(session, "hello from the C SDK") == BOLE_OK, "submit turn 1");
  CHECK(wait_count(&state, BOLE_EVENT_TASK_FINISHED, 1, 30000) == 0,
        "turn 1 terminal");
  CHECK(state.counts[BOLE_EVENT_TASK_STARTED] >= 1, "TASK_STARTED");
  CHECK(state.counts[BOLE_EVENT_STEP_STARTED] >= 1, "STEP_STARTED");
  CHECK(state.counts[BOLE_EVENT_MODEL_SELECTED] >= 1, "MODEL_SELECTED");
  CHECK(state.counts[BOLE_EVENT_STEP_FINISHED] >= 1, "STEP_FINISHED");
  CHECK(state.counts[BOLE_EVENT_ASSISTANT_MESSAGE] >= 1
            || state.counts[BOLE_EVENT_TEXT_DELTA] >= 1,
        "assistant output");
  CHECK(strstr(state.terminal_payload, "\"outcome\":\"completed\"") != NULL,
        "turn 1 completed");
  CHECK(state.rsi_seen >= 1, "RSI_STATE");
  CHECK(state.session_ok == 1, "session ids preserved");

  bole_rsi_state rsi;
  memset(&rsi, 0, sizeof(rsi));
  rsi.size = sizeof(rsi);
  CHECK(bole_rsi_get(handle, &rsi) == BOLE_OK, "rsi_get");
  CHECK(rsi.composed == 1, "preset plane composed");
  CHECK(strcmp(rsi.preset, "standard") == 0, "default preset standard");

  char trace_path[1024];
  snprintf(trace_path, sizeof(trace_path), "%s/c-e2e-trace.jsonl", cwd);
  uint32_t trace_count = 0;
  CHECK(bole_trace_export(handle, "c-e2e", 0, trace_path, &trace_count) == BOLE_OK,
        "trace_export");
  CHECK(trace_count > 0, "trace has events");

  CHECK(bole_cancel(session) == BOLE_E_NO_ACTIVE_TASK, "idle cancel");

  /* The mock model delays its reply, so cancel lands while the turn is active. */
  CHECK(bole_submit(session, "cancel this turn") == BOLE_OK, "submit turn 2");
  CHECK(wait_count(&state, BOLE_EVENT_TASK_STARTED, 2, 10000) == 0,
        "turn 2 started");
  CHECK(bole_cancel(session) == BOLE_OK, "cancel turn 2");
  CHECK(wait_count(&state, BOLE_EVENT_TASK_FINISHED, 2, 10000) == 0,
        "turn 2 terminal");
  CHECK(state.aborted == 1, "turn 2 aborted");

  int callbacks_before = state.terminal_count;
  CHECK(bole_release(handle) == BOLE_OK, "release");
  handle = NULL;
  sleep_ms(250);
  CHECK(state.terminal_count == callbacks_before, "no callbacks after release");

done:
  if (handle != NULL) {
    bole_release(handle);
  }
  bole_cond_destroy(&state.cond);
  bole_mutex_destroy(&state.lock);
  if (g_failures != 0) {
    fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  printf("runtime integration test passed\n");
  return 0;
}
