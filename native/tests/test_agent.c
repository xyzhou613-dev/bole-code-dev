#ifndef _DARWIN_C_SOURCE
#  define _DARWIN_C_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200809L
#endif

#include "bole_agent.h"
#include "../src/json.h"
#include "platform.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef FAKE_RUNTIME_PATH
#  define FAKE_RUNTIME_PATH "/tmp/bole_fake_runtime"
#endif

#ifndef BOLE_FIXTURE_DIR
#  define BOLE_FIXTURE_DIR "tests/fixtures"
#endif

static int g_failures = 0;

#define CHECK(condition, message)                                                \
  do {                                                                           \
    if (!(condition)) {                                                          \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (message));       \
      g_failures++;                                                              \
    }                                                                            \
  } while (0)

typedef struct test_state {
  bole_mutex lock;
  bole_cond cond;
  int initialized;

  bole_handle* handle;
  int ready;
  int failed;
  int failed_code;
  int exited;
  int finished;
  int finished_count;
  int terminal;
  int task_failed;
  int aborted;
  int tool_started;
  int tool_finished;
  int rsi_session_seen;
  int rsi_default_seen;

  int counts[128];
  char ready_payload[2048];
  char failed_payload[512];
  char finished_payload[512];
  char tool_payload[1024];
  char rsi_payload[1024];
  char status_payload[256];
  char terminal_payload[512];
  int event_type_count;
  int event_types[128];
  int session_ok;

  uint64_t last_event_id;
  int64_t last_ts;
  int order_ok;
  int payload_ok;

  int sleep_on_first;
  int slept;

  int check_forbidden;
  int forbidden[4];
  int allowed_rsi_get;
  int replacement_count;
} test_state;

typedef struct scenario {
  char root[512];
  char runtime_dir[1024];
  char dsh_home[1024];
  char cwd[1024];
  bole_config config;
  test_state state;
  bole_handle* handle;
} scenario;

static void copy_text(char* out, size_t capacity, const char* text) {
  if (text == NULL) {
    out[0] = '\0';
    return;
  }
  strncpy(out, text, capacity - 1);
  out[capacity - 1] = '\0';
}

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

static void BOLE_CALL on_event(void* user_data, const bole_event* event) {
  test_state* state = (test_state*)user_data;
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
    if (state->event_type_count < 128) {
      state->event_types[state->event_type_count++] = (int)event->type;
    }
    int default_rsi = event->type == BOLE_EVENT_RSI_STATE
        && event->payload_json != NULL
        && strstr(event->payload_json, "\"scope\":\"default\"") != NULL;
    if (!default_rsi && strcmp(event->session_id, "s-001") != 0) {
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
      copy_text(state->failed_payload, sizeof(state->failed_payload),
                event->payload_json);
      break;
    case BOLE_EVENT_RUNTIME_EXITED:
      state->exited = 1;
      break;
    case BOLE_EVENT_TASK_FINISHED:
      state->finished = 1;
      state->finished_count++;
      state->terminal = 1;
      if (event->payload_json != NULL
          && strstr(event->payload_json, "\"aborted\"") != NULL) {
        state->aborted = 1;
      }
      copy_text(state->finished_payload, sizeof(state->finished_payload),
                event->payload_json);
      copy_text(state->terminal_payload, sizeof(state->terminal_payload),
                event->payload_json);
      break;
    case BOLE_EVENT_TASK_FAILED:
      state->terminal = 1;
      state->task_failed = 1;
      copy_text(state->terminal_payload, sizeof(state->terminal_payload),
                event->payload_json);
      break;
    case BOLE_EVENT_TOOL_CALL_STARTED:
      state->tool_started = 1;
      copy_text(state->tool_payload, sizeof(state->tool_payload),
                event->payload_json);
      break;
    case BOLE_EVENT_TOOL_CALL_FINISHED:
      state->tool_finished = 1;
      break;
    case BOLE_EVENT_RSI_STATE:
      copy_text(state->rsi_payload, sizeof(state->rsi_payload),
                event->payload_json);
      if (event->payload_json != NULL
          && strstr(event->payload_json, "\"scope\":\"session\"") != NULL) {
        state->rsi_session_seen = 1;
      }
      if (event->payload_json != NULL
          && strstr(event->payload_json, "\"scope\":\"default\"") != NULL) {
        state->rsi_default_seen = 1;
      }
      break;
    case BOLE_EVENT_MODEL_SELECTED:
    case BOLE_EVENT_ASSISTANT_MESSAGE:
      break;
    default:
      break;
  }

  if (state->sleep_on_first && !state->slept
      && event->type == BOLE_EVENT_TASK_STARTED) {
    state->slept = 1;
    /* Hold the event thread long enough for the reader to fill the queue. */
    bole_cond_wait(&state->cond, &state->lock, 250);
  }

  if (state->check_forbidden && event->type == BOLE_EVENT_TASK_STARTED
      && state->handle != NULL) {
    bole_handle* dummy = NULL;
    state->forbidden[0] = bole_init(NULL, &dummy);
    state->forbidden[1] = bole_wait_ready(state->handle, 0);
    state->forbidden[2] = bole_trace_export(state->handle, "s-001", 0,
                                            "/tmp/bole-forbidden.jsonl", NULL);
    state->forbidden[3] = bole_release(state->handle);
    bole_rsi_state rsi;
    memset(&rsi, 0, sizeof(rsi));
    rsi.size = sizeof(rsi);
    state->allowed_rsi_get = bole_rsi_get(state->handle, &rsi);
  }

  bole_cond_broadcast(&state->cond);
  bole_mutex_unlock(&state->lock);
}

static void BOLE_CALL on_replacement_event(void* user_data, const bole_event* event) {
  test_state* state = (test_state*)user_data;
  bole_mutex_lock(&state->lock);
  state->replacement_count++;
  if (event->type == BOLE_EVENT_TASK_FINISHED) {
    state->finished = 1;
    state->terminal = 1;
  }
  bole_cond_broadcast(&state->cond);
  bole_mutex_unlock(&state->lock);
}

typedef struct wait_arg {
  bole_handle* handle;
  int32_t code;
} wait_arg;

static void* wait_ready_thread(void* argument) {
  wait_arg* arg = (wait_arg*)argument;
  arg->code = bole_wait_ready(arg->handle, 5000);
  return NULL;
}

static int wait_flag(test_state* state, int* flag, int timeout_ms) {
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

static void sleep_ms(int milliseconds) {
  struct timespec duration;
  duration.tv_sec = milliseconds / 1000;
  duration.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
  nanosleep(&duration, NULL);
}

static int wait_count(test_state* state, int type, int count, int timeout_ms) {
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

static void write_file(const char* path, const char* text, int executable) {
  FILE* file = fopen(path, "wb");
  assert(file != NULL);
  fputs(text, file);
  fclose(file);
  if (executable) {
    chmod(path, 0755);
  }
}

static int file_contains(const char* path, const char* needle) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    return 0;
  }
  char buffer[4096];
  size_t length = fread(buffer, 1, sizeof(buffer) - 1, file);
  fclose(file);
  buffer[length] = '\0';
  return strstr(buffer, needle) != NULL;
}

static void make_runtime(scenario* scenario) {
  char path[1024];
  snprintf(path, sizeof(path), "%s/node_modules", scenario->runtime_dir);
  assert(mkdir(path, 0755) == 0);
  snprintf(path, sizeof(path), "%s/node_modules/@deepseek-ai", scenario->runtime_dir);
  assert(mkdir(path, 0755) == 0);
  snprintf(path, sizeof(path), "%s/node_modules/@deepseek-ai/dsh", scenario->runtime_dir);
  assert(mkdir(path, 0755) == 0);
  snprintf(path, sizeof(path), "%s/node_modules/@deepseek-ai/dsh/lib", scenario->runtime_dir);
  assert(mkdir(path, 0755) == 0);
  snprintf(path, sizeof(path), "%s/node_modules/@deepseek-ai/dsh/lib/bin.js",
           scenario->runtime_dir);
  write_file(path, "// fake entry, never executed\n", 0);
  snprintf(path, sizeof(path), "%s/cordis.patch.yml", scenario->runtime_dir);
  write_file(path, "[]\n", 0);

  char script[2048];
  snprintf(script, sizeof(script), "#!/bin/sh\nexec \"%s\"\n", FAKE_RUNTIME_PATH);
  snprintf(path, sizeof(path), "%s/node.exe", scenario->runtime_dir);
  write_file(path, script, 1);
}

static void scenario_setup(scenario* scenario, const char* mode) {
  memset(scenario, 0, sizeof(*scenario));
  char root_template[] = "/tmp/bole-agent-test-XXXXXX";
  char* root = mkdtemp(root_template);
  assert(root != NULL);
  snprintf(scenario->root, sizeof(scenario->root), "%s", root);
  snprintf(scenario->runtime_dir, sizeof(scenario->runtime_dir), "%s/runtime", root);
  snprintf(scenario->dsh_home, sizeof(scenario->dsh_home), "%s/home", root);
  snprintf(scenario->cwd, sizeof(scenario->cwd), "%s/work", root);
  assert(mkdir(scenario->runtime_dir, 0755) == 0);
  assert(mkdir(scenario->cwd, 0755) == 0);
  make_runtime(scenario);

  setenv("BOLE_FAKE_MODE", mode, 1);
  memset(&scenario->state, 0, sizeof(scenario->state));
  scenario->state.order_ok = 1;
  scenario->state.payload_ok = 1;
  scenario->state.session_ok = 1;
  assert(bole_mutex_init(&scenario->state.lock) == 0);
  assert(bole_cond_init(&scenario->state.cond) == 0);
  scenario->state.initialized = 1;

  memset(&scenario->config, 0, sizeof(scenario->config));
  scenario->config.size = sizeof(scenario->config);
  scenario->config.runtime_dir = scenario->runtime_dir;
  scenario->config.dsh_home = scenario->dsh_home;
  scenario->config.cwd = scenario->cwd;
  scenario->config.model_provider = "bole";
  scenario->config.model_name = "qwen3.8-27b";
  scenario->config.event_cb = on_event;
  scenario->config.user_data = &scenario->state;
}

static int32_t scenario_init(scenario* scenario) {
  int32_t code = bole_init(&scenario->config, &scenario->handle);
  scenario->state.handle = scenario->handle;
  return code;
}

static void scenario_teardown(scenario* scenario) {
  if (scenario->handle != NULL) {
    bole_release(scenario->handle);
    scenario->handle = NULL;
    scenario->state.handle = NULL;
    sleep_ms(250);
  }
  if (scenario->state.initialized) {
    bole_cond_destroy(&scenario->state.cond);
    bole_mutex_destroy(&scenario->state.lock);
    scenario->state.initialized = 0;
  }
  char command[1024];
  snprintf(command, sizeof(command), "rm -rf '%s'", scenario->root);
  if (system(command) != 0) {
    fprintf(stderr, "warning: could not remove %s\n", scenario->root);
  }
}

static void create_session(scenario* scenario, const char* id, bole_session** session) {
  int32_t code = bole_session_create(scenario->handle, id, NULL, session);
  CHECK(code == BOLE_OK, "session create");
}

/* ── Scenarios ──────────────────────────────────────────────────── */

static void test_happy(void) {
  scenario c;
  scenario_setup(&c, "happy");
  char log_path[1024];
  snprintf(log_path, sizeof(log_path), "%s/bole.log", c.cwd);
  c.config.log_path = log_path;
  CHECK(scenario_init(&c) == BOLE_OK, "init");
  CHECK(bole_wait_ready(c.handle, 5000) == BOLE_OK, "wait_ready");
  CHECK(wait_flag(&c.state, &c.state.ready, 2000) == 0, "ready event");
  CHECK(strstr(c.state.ready_payload, "\"capabilities\"") != NULL, "ready payload");
  CHECK(strstr(c.state.ready_payload, "\"model\":{\"provider\":\"bole\"") != NULL,
        "ready model");

  bole_session* session = NULL;
  create_session(&c, "s-001", &session);
  bole_session* resumed = NULL;
  CHECK(bole_session_resume(c.handle, "s-001", &resumed) == BOLE_OK, "resume local");
  CHECK(resumed == session, "resume returns tracked session");

  CHECK(bole_submit(session, "hello") == BOLE_OK, "submit");
  CHECK(wait_flag(&c.state, &c.state.finished, 5000) == 0, "finished");
  CHECK(c.state.counts[BOLE_EVENT_TASK_STARTED] == 1, "task started");
  CHECK(c.state.counts[40] == 0, "verify event filtered");
  CHECK(c.state.counts[99] == 0, "unknown event filtered");
  CHECK(c.state.counts[BOLE_EVENT_STEP_STARTED] == 1, "step started");
  CHECK(c.state.counts[BOLE_EVENT_STEP_FINISHED] == 1, "step finished");
  CHECK(c.state.counts[BOLE_EVENT_TEXT_DELTA] == 2, "text deltas");
  CHECK(c.state.counts[BOLE_EVENT_REASONING_DELTA] == 1, "reasoning deltas");
  CHECK(c.state.counts[BOLE_EVENT_ASSISTANT_MESSAGE] == 1, "assistant message");
  CHECK(c.state.counts[BOLE_EVENT_TOOL_CALL_STARTED] == 1, "tool start");
  CHECK(c.state.counts[BOLE_EVENT_TOOL_CALL_FINISHED] == 1, "tool finish");
  CHECK(c.state.counts[BOLE_EVENT_TASK_FINISHED] == 1, "task finished");
  CHECK(strstr(c.state.tool_payload, "toy_add") != NULL, "tool payload");
  CHECK(strstr(c.state.finished_payload, "\"outcome\":\"completed\"") != NULL,
        "completed outcome");
  CHECK(c.state.order_ok == 1, "event order and timestamps");
  CHECK(c.state.payload_ok == 1, "payload v1");

  bole_session* duplicate = NULL;
  CHECK(bole_session_create(c.handle, "s-001", NULL, &duplicate)
            == BOLE_E_SESSION_EXISTS,
        "duplicate create");
  int32_t last = 0;
  char small[6];
  CHECK(bole_last_error(c.handle, &last, small, sizeof(small)) == BOLE_OK,
        "last_error");
  CHECK(last == BOLE_E_SESSION_EXISTS, "last_error code");
  CHECK(strlen(small) == sizeof(small) - 1, "last_error truncation");

  CHECK(bole_submit_blocks(session, "{}") == BOLE_E_INVALID_CONFIG,
        "invalid blocks");
  int32_t blocks_code = bole_submit_blocks(session,
      "[{\"type\":\"text\",\"text\":\"hi\"}]");
  if (blocks_code != BOLE_OK) {
    int32_t last_code = 0;
    (void)bole_last_error(c.handle, &last_code, NULL, 0);
    fprintf(stderr, "submit blocks rc=%d last=%d\n", (int)blocks_code,
            (int)last_code);
  }
  CHECK(blocks_code == BOLE_OK, "submit blocks");
  CHECK(wait_count(&c.state, BOLE_EVENT_TASK_FINISHED, 2, 5000) == 0,
        "second turn finished");

  CHECK(bole_cancel(session) == BOLE_E_NO_ACTIVE_TASK, "cancel idle");
  CHECK(file_contains(log_path, "no active task"), "log path records errors");
  CHECK(bole_rsi_set(c.handle, 1) == BOLE_OK, "rsi set");
  bole_rsi_state rsi;
  memset(&rsi, 0, sizeof(rsi));
  rsi.size = sizeof(rsi);
  CHECK(bole_rsi_get(c.handle, &rsi) == BOLE_OK, "rsi get");
  CHECK(rsi.enabled == 1 && rsi.composed == 1, "rsi flags");
  CHECK(strcmp(rsi.preset, "rsi-infer") == 0, "rsi preset");

  struct {
    uint32_t size;
    int32_t enabled;
    int32_t composed;
    unsigned char guard[16];
  } partial;
  memset(&partial, 0xAA, sizeof(partial));
  partial.size = (uint32_t)(offsetof(bole_rsi_state, preset));
  CHECK(bole_rsi_get(c.handle, (bole_rsi_state*)&partial) == BOLE_OK,
        "rsi get partial");
  CHECK(partial.enabled == 1 && partial.composed == 1, "partial rsi fields");
  for (size_t i = 0; i < sizeof(partial.guard); i++) {
    CHECK(partial.guard[i] == 0xAA, "rsi get wrote past caller size");
  }

  char trace_path[1024];
  snprintf(trace_path, sizeof(trace_path), "%s/trace.jsonl", c.cwd);
  uint32_t event_count = 0;
  CHECK(bole_trace_export(c.handle, "s-001", 0, trace_path, &event_count) == BOLE_OK,
        "trace export");
  CHECK(event_count == 2, "trace event count");
  CHECK(access(trace_path, F_OK) == 0, "trace file exists");

  int callbacks = c.state.counts[BOLE_EVENT_TASK_STARTED]
      + c.state.counts[BOLE_EVENT_TASK_FINISHED];
  CHECK(bole_release(c.handle) == BOLE_OK, "release");
  c.handle = NULL;
  c.state.handle = NULL;
  sleep_ms(200);
  CHECK(callbacks == c.state.counts[BOLE_EVENT_TASK_STARTED]
              + c.state.counts[BOLE_EVENT_TASK_FINISHED],
        "no callbacks after release");
  scenario_teardown(&c);
}

static void test_config_small(void) {
  scenario c;
  scenario_setup(&c, "happy");
  size_t small_size = offsetof(bole_config, log_path);
  /* Keep the allocation object-sized; `size` tells the SDK which fields are
   * valid, exactly as a forward-compatible caller would. */
  bole_config* partial = (bole_config*)calloc(1, sizeof(bole_config));
  assert(partial != NULL);
  memcpy(partial, &c.config, small_size);
  partial->size = (uint32_t)small_size;
  bole_handle* handle = NULL;
  CHECK(bole_init(partial, &handle) == BOLE_OK, "partial config init");
  free(partial);
  if (handle != NULL) {
    CHECK(bole_wait_ready(handle, 5000) == BOLE_OK, "partial config ready");
  }
  c.handle = handle;
  c.state.handle = handle;
  scenario_teardown(&c);
}

static void test_wait_timeout(void) {
  scenario c;
  scenario_setup(&c, "silent");
  CHECK(scenario_init(&c) == BOLE_OK, "init");
  CHECK(bole_wait_ready(c.handle, 200) == BOLE_E_WAIT_READY_TIMEOUT,
        "wait_ready timeout code");
  int32_t last = 0;
  CHECK(bole_last_error(c.handle, &last, NULL, 0) == BOLE_OK, "last_error");
  CHECK(last == BOLE_E_WAIT_READY_TIMEOUT, "timeout last_error");
  scenario_teardown(&c);
}

static void test_stdout_violation(void) {
  scenario c;
  scenario_setup(&c, "banner");
  CHECK(scenario_init(&c) == BOLE_OK, "init");
  CHECK(bole_wait_ready(c.handle, 3000) == BOLE_E_STDOUT_VIOLATION,
        "stdout violation code");
  CHECK(wait_flag(&c.state, &c.state.failed, 2000) == 0, "failed event");
  CHECK(c.state.failed_code == BOLE_E_STDOUT_VIOLATION, "2006 event code");
  scenario_teardown(&c);
}

static void test_line_too_long(void) {
  scenario c;
  scenario_setup(&c, "long-line");
  c.config.max_line_bytes = 512;
  CHECK(scenario_init(&c) == BOLE_OK, "init");
  CHECK(bole_wait_ready(c.handle, 5000) == BOLE_OK, "ready");
  bole_session* session = NULL;
  create_session(&c, "s-long", &session);
  (void)bole_submit(session, "hello");
  CHECK(wait_flag(&c.state, &c.state.failed, 5000) == 0, "failed event");
  CHECK(c.state.failed_code == BOLE_E_LINE_TOO_LONG, "2005 event code");
  scenario_teardown(&c);
}

static void test_no_preset(void) {
  scenario c;
  scenario_setup(&c, "no-preset");
  CHECK(scenario_init(&c) == BOLE_OK, "init");
  CHECK(bole_wait_ready(c.handle, 5000) == BOLE_OK, "ready");
  CHECK(wait_flag(&c.state, &c.state.rsi_default_seen, 2000) == 0,
        "default rsi event");
  bole_rsi_state rsi;
  memset(&rsi, 0, sizeof(rsi));
  rsi.size = sizeof(rsi);
  CHECK(bole_rsi_get(c.handle, &rsi) == BOLE_OK, "rsi get");
  CHECK(rsi.composed == 0, "not composed");
  CHECK(rsi.preset[0] == '\0', "empty preset");
  CHECK(bole_rsi_set(c.handle, 1) == BOLE_E_RSI_UNAVAILABLE, "rsi unavailable");
  CHECK(bole_rsi_get(c.handle, &rsi) == BOLE_OK, "rsi get again");
  CHECK(rsi.composed == 0 && rsi.enabled == 0, "state unchanged");
  scenario_teardown(&c);
}

static void test_session_rsi(void) {
  scenario c;
  scenario_setup(&c, "session-rsi");
  CHECK(scenario_init(&c) == BOLE_OK, "init");
  CHECK(bole_wait_ready(c.handle, 5000) == BOLE_OK, "ready");
  bole_session* session = NULL;
  create_session(&c, "s-rsi", &session);
  CHECK(wait_flag(&c.state, &c.state.rsi_session_seen, 2000) == 0,
        "session rsi event");
  bole_rsi_state rsi;
  memset(&rsi, 0, sizeof(rsi));
  rsi.size = sizeof(rsi);
  CHECK(bole_rsi_get(c.handle, &rsi) == BOLE_OK, "rsi get");
  CHECK(rsi.enabled == 0, "session scope did not change default enabled");
  CHECK(strcmp(rsi.preset, "standard") == 0, "default preset stays standard");
  CHECK(rsi.composed == 1, "composed from initialize");
  scenario_teardown(&c);
}

static void test_cancel(void) {
  scenario c;
  scenario_setup(&c, "cancel");
  CHECK(scenario_init(&c) == BOLE_OK, "init");
  CHECK(bole_wait_ready(c.handle, 5000) == BOLE_OK, "ready");
  bole_session* session = NULL;
  create_session(&c, "s-cancel", &session);
  CHECK(bole_submit(session, "run") == BOLE_OK, "submit");
  CHECK(wait_flag(&c.state, &c.state.tool_started, 3000) == 0, "tool started");
  CHECK(bole_cancel(session) == BOLE_OK, "cancel");
  CHECK(wait_flag(&c.state, &c.state.finished, 3000) == 0, "aborted finish");
  CHECK(c.state.aborted == 1, "aborted outcome");
  CHECK(c.state.tool_finished == 0, "no tool result after cancel");
  CHECK(bole_cancel(session) == BOLE_E_NO_ACTIVE_TASK, "cancel again");
  scenario_teardown(&c);
}

static void test_errors(void) {
  scenario c;
  scenario_setup(&c, "happy");
  CHECK(scenario_init(&c) == BOLE_OK, "init");
  CHECK(bole_wait_ready(c.handle, 5000) == BOLE_OK, "ready");

  bole_session* session = NULL;
  CHECK(bole_session_create(c.handle, "exists", NULL, &session) == BOLE_E_SESSION_EXISTS,
        "session exists");
  CHECK(bole_session_resume(c.handle, "missing", &session) == BOLE_E_SESSION_NOT_FOUND,
        "session not found");
  CHECK(bole_session_resume(c.handle, "unavailable", &session)
            == BOLE_E_PERSISTENCE_UNAVAILABLE,
        "persistence unavailable");
  CHECK(bole_session_resume(c.handle, "corrupt", &session)
            == BOLE_E_SESSION_NOT_RESUMABLE,
        "session not resumable");
  create_session(&c, "s-errors", &session);
  CHECK(bole_session_set_preset(session, "bogus") == BOLE_E_PRESET_NOT_FOUND,
        "preset not found");
  CHECK(bole_session_set_preset(session, "locked") == BOLE_E_PRESET_LOCKED,
        "preset locked");
  CHECK(bole_cancel(session) == BOLE_E_NO_ACTIVE_TASK, "no active task");
  char trace_path[1024];
  snprintf(trace_path, sizeof(trace_path), "%s/fail-trace.jsonl", c.cwd);
  CHECK(bole_trace_export(c.handle, "s-errors", 0, trace_path, NULL)
            == BOLE_E_RUNTIME_REQUEST_FAILED,
        "trace write failed");
  CHECK(bole_submit_blocks(session, "{}") == BOLE_E_INVALID_CONFIG,
        "invalid blocks");
  scenario_teardown(&c);
}

static void test_callback_restrictions(void) {
  scenario c;
  scenario_setup(&c, "happy");
  c.state.check_forbidden = 1;
  CHECK(scenario_init(&c) == BOLE_OK, "init");
  CHECK(bole_wait_ready(c.handle, 5000) == BOLE_OK, "ready");
  bole_session* session = NULL;
  create_session(&c, "s-callback", &session);
  CHECK(bole_submit(session, "hello") == BOLE_OK, "submit");
  CHECK(wait_flag(&c.state, &c.state.finished, 5000) == 0, "finished");
  CHECK(c.state.forbidden[0] == BOLE_E_INVALID_CONFIG, "init denied in callback");
  CHECK(c.state.forbidden[1] == BOLE_E_INVALID_CONFIG, "wait_ready denied in callback");
  CHECK(c.state.forbidden[2] == BOLE_E_INVALID_CONFIG, "trace denied in callback");
  CHECK(c.state.forbidden[3] == BOLE_E_INVALID_CONFIG, "release denied in callback");
  CHECK(c.state.allowed_rsi_get == BOLE_OK, "rsi_get allowed in callback");
  scenario_teardown(&c);
}

static void test_delta_flood(void) {
  scenario c;
  scenario_setup(&c, "flood-delta");
  c.state.sleep_on_first = 1;
  CHECK(scenario_init(&c) == BOLE_OK, "init");
  CHECK(bole_wait_ready(c.handle, 5000) == BOLE_OK, "ready");
  bole_session* session = NULL;
  create_session(&c, "s-flood", &session);
  CHECK(bole_submit(session, "flood") == BOLE_OK, "submit");
  CHECK(wait_flag(&c.state, &c.state.finished, 15000) == 0, "finished");
  CHECK(c.state.failed == 0, "deltas do not fail the handle");
  CHECK(c.state.counts[BOLE_EVENT_TASK_FINISHED] == 1, "terminal event");
  CHECK(c.state.counts[BOLE_EVENT_TEXT_DELTA] > 0, "some deltas delivered");
  CHECK(c.state.counts[BOLE_EVENT_TEXT_DELTA] < 5000, "deltas are shed");
  scenario_teardown(&c);
}

static void test_structural_overflow(void) {
  scenario c;
  scenario_setup(&c, "overflow-structural");
  c.state.sleep_on_first = 1;
  CHECK(scenario_init(&c) == BOLE_OK, "init");
  CHECK(bole_wait_ready(c.handle, 5000) == BOLE_OK, "ready");
  bole_session* session = NULL;
  create_session(&c, "s-overflow", &session);
  (void)bole_submit(session, "overflow");
  CHECK(wait_flag(&c.state, &c.state.failed, 10000) == 0, "failed event");
  CHECK(c.state.failed_code == BOLE_E_EVENT_QUEUE_OVERFLOW, "2007 event code");
  scenario_teardown(&c);
}

static void test_fixture_replay(void) {
  static const int cancel_types[] = {10, 15, 11, 14, 12, 50};
  static const int happy_types[] = {15, 10, 15, 11, 14, 22, 30, 31, 12, 11, 20, 22, 12, 50};
  static const int model_error_types[] = {10, 15, 11, 14, 12, 51};
  static const int text_types[] = {10, 15, 11, 14, 20, 22, 12, 50};
  static const int long_output_types[] = {15, 10, 15, 11, 14, 22, 30, 31, 12, 11, 20, 22, 12, 50};
  static const int max_tokens_types[] = {15, 10, 15, 11, 14, 20, 22, 12, 50};
  static const int rsi_types[] = {15, 15, 10, 11, 15, 14, 20, 22, 12, 50};
  static const int tool_error_types[] = {15, 10, 15, 11, 14, 22, 30, 31, 12, 11, 20, 22, 12, 50};
  static const struct {
    const char* file;
    int terminal_type;
    const char* outcome;
    const int* types;
    size_t type_count;
  } cases[] = {
    {"cancel-mid-turn.jsonl", 50, "aborted", cancel_types,
     sizeof(cancel_types) / sizeof(cancel_types[0])},
    {"happy-tool.jsonl", 50, "completed", happy_types,
     sizeof(happy_types) / sizeof(happy_types[0])},
    {"model-error.jsonl", 51, "SERVER", model_error_types,
     sizeof(model_error_types) / sizeof(model_error_types[0])},
    {"text-turn.jsonl", 50, "completed", text_types,
     sizeof(text_types) / sizeof(text_types[0])},
    {"long-tool-output.jsonl", 50, "completed", long_output_types,
     sizeof(long_output_types) / sizeof(long_output_types[0])},
    {"max-tokens.jsonl", 50, "max_tokens", max_tokens_types,
     sizeof(max_tokens_types) / sizeof(max_tokens_types[0])},
    {"rsi-available.jsonl", 50, "completed", rsi_types,
     sizeof(rsi_types) / sizeof(rsi_types[0])},
    {"rsi-delivered.jsonl", 50, "completed", rsi_types,
     sizeof(rsi_types) / sizeof(rsi_types[0])},
    {"rsi-unsupported.jsonl", 50, "completed", rsi_types,
     sizeof(rsi_types) / sizeof(rsi_types[0])},
    {"tool-error.jsonl", 50, "completed", tool_error_types,
     sizeof(tool_error_types) / sizeof(tool_error_types[0])},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    scenario c;
    scenario_setup(&c, "replay");
    char fixture[1024];
    snprintf(fixture, sizeof(fixture), "%s/%s", BOLE_FIXTURE_DIR, cases[i].file);
    setenv("BOLE_FAKE_FIXTURE", fixture, 1);
    CHECK(scenario_init(&c) == BOLE_OK, "fixture init");
    CHECK(bole_wait_ready(c.handle, 5000) == BOLE_OK, "fixture ready");
    bole_session* session = NULL;
    create_session(&c, "s-001", &session);
    CHECK(bole_submit(session, "replay") == BOLE_OK, "fixture submit");
    CHECK(wait_flag(&c.state, &c.state.terminal, 5000) == 0, "fixture terminal");
    CHECK(c.state.event_type_count == (int)cases[i].type_count,
          "fixture event count");
    for (size_t k = 0; k < cases[i].type_count
         && k < (size_t)c.state.event_type_count; k++) {
      CHECK(c.state.event_types[k] == cases[i].types[k], "fixture event order");
    }
    CHECK(c.state.session_ok == 1, "fixture session id");
    CHECK(c.state.payload_ok == 1, "fixture payload v1");
    CHECK(c.state.failed == 0, "fixture did not fail the handle");
    CHECK(c.state.counts[cases[i].terminal_type] == 1, "fixture terminal event");
    CHECK(strstr(c.state.terminal_payload, cases[i].outcome) != NULL,
          "fixture terminal outcome");
    if (cases[i].terminal_type == 51) {
      CHECK(c.state.task_failed == 1, "fixture task failed");
    }
    scenario_teardown(&c);
  }
}

static void test_relative_paths(void) {
  scenario c;
  scenario_setup(&c, "happy");
  char saved[1024];
  CHECK(getcwd(saved, sizeof(saved)) != NULL, "save cwd");
  CHECK(chdir(c.root) == 0, "enter root");
  c.config.runtime_dir = "runtime";
  c.config.dsh_home = "home";
  c.config.log_path = "bole-relative.log";
  int32_t code = scenario_init(&c);
  CHECK(chdir(saved) == 0, "restore cwd");
  CHECK(code == BOLE_OK, "relative paths init");
  if (code == BOLE_OK) {
    CHECK(bole_wait_ready(c.handle, 5000) == BOLE_OK, "relative paths ready");
    bole_session* session = NULL;
    create_session(&c, "s-relative", &session);
    CHECK(bole_cancel(session) == BOLE_E_NO_ACTIVE_TASK, "relative cancel");
    char log_path[1024];
    snprintf(log_path, sizeof(log_path), "%s/bole-relative.log", c.root);
    CHECK(file_contains(log_path, "no active task"), "relative log path");
  }
  scenario_teardown(&c);
}

static void test_callback_replace(void) {
  scenario c;
  scenario_setup(&c, "happy");
  CHECK(scenario_init(&c) == BOLE_OK, "init");
  CHECK(bole_wait_ready(c.handle, 5000) == BOLE_OK, "ready");
  bole_session* session = NULL;
  create_session(&c, "s-replace", &session);
  CHECK(bole_set_event_callback(c.handle, on_replacement_event, &c.state)
            == BOLE_OK,
        "replace callback");
  CHECK(bole_submit(session, "replace") == BOLE_OK, "submit");
  CHECK(wait_flag(&c.state, &c.state.finished, 5000) == 0, "replacement finished");
  CHECK(c.state.replacement_count > 0, "replacement callback used");
  CHECK(bole_set_event_callback(c.handle, NULL, NULL)
            == BOLE_E_EVENT_CALLBACK_REQUIRED,
        "null replacement");
  scenario_teardown(&c);
}

static void test_callback_queued(void) {
  scenario c;
  scenario_setup(&c, "flood-delta");
  c.state.sleep_on_first = 1;
  CHECK(scenario_init(&c) == BOLE_OK, "init");
  CHECK(bole_wait_ready(c.handle, 5000) == BOLE_OK, "ready");
  bole_session* session = NULL;
  create_session(&c, "s-queued", &session);
  CHECK(bole_submit(session, "queued") == BOLE_OK, "submit");
  sleep_ms(50);
  CHECK(bole_set_event_callback(c.handle, on_replacement_event, &c.state)
            == BOLE_OK,
        "replace while queued");
  CHECK(wait_flag(&c.state, &c.state.finished, 15000) == 0,
        "queued replacement finished");
  CHECK(c.state.replacement_count > 0, "queued events use new callback");
  scenario_teardown(&c);
}

static void test_invalid_arguments(void) {
  scenario c;
  scenario_setup(&c, "happy");
  CHECK(scenario_init(&c) == BOLE_OK, "init");
  CHECK(bole_wait_ready(c.handle, 5000) == BOLE_OK, "ready");
  bole_session* session = NULL;
  create_session(&c, "s-invalid", &session);

  bole_rsi_state rsi;
  memset(&rsi, 0, sizeof(rsi));
  rsi.size = sizeof(rsi);
  CHECK(bole_set_event_callback(NULL, on_event, NULL) == BOLE_E_INVALID_CONFIG,
        "null handle callback");
  CHECK(bole_wait_ready(NULL, 0) == BOLE_E_INVALID_CONFIG, "null wait handle");
  CHECK(bole_rsi_get(NULL, &rsi) == BOLE_E_INVALID_CONFIG, "null rsi handle");
  CHECK(bole_rsi_get(c.handle, NULL) == BOLE_E_INVALID_CONFIG, "null rsi output");
  rsi.size = 0;
  CHECK(bole_rsi_get(c.handle, &rsi) == BOLE_E_INVALID_CONFIG, "small rsi output");
  CHECK(bole_rsi_set(c.handle, 2) == BOLE_E_INVALID_CONFIG, "bad rsi value");
  CHECK(bole_rsi_set(NULL, 0) == BOLE_E_INVALID_CONFIG, "null rsi set handle");
  CHECK(bole_last_error(c.handle, NULL, NULL, 1) == BOLE_E_INVALID_CONFIG,
        "null error buffer");
  CHECK(bole_last_error(NULL, NULL, NULL, 0) == BOLE_E_INVALID_CONFIG,
        "null error handle");

  CHECK(bole_session_create(c.handle, "", NULL, &session)
            == BOLE_E_INVALID_CONFIG,
        "empty session id");
  CHECK(bole_session_create(c.handle, NULL, NULL, &session)
            == BOLE_E_INVALID_CONFIG,
        "null session id");
  CHECK(bole_session_resume(c.handle, NULL, &session) == BOLE_E_INVALID_CONFIG,
        "null resume id");
  CHECK(bole_session_set_preset(session, NULL) == BOLE_E_INVALID_CONFIG,
        "null preset");
  CHECK(bole_session_set_preset(session, "") == BOLE_E_INVALID_CONFIG,
        "empty preset");
  CHECK(bole_submit(session, NULL) == BOLE_E_INVALID_CONFIG, "null text");
  CHECK(bole_submit(session, "") == BOLE_E_INVALID_CONFIG, "empty text");
  CHECK(bole_submit_blocks(session, NULL) == BOLE_E_INVALID_CONFIG,
        "null blocks");
  CHECK(bole_submit_blocks(session, "{}") == BOLE_E_INVALID_CONFIG,
        "object blocks");
  CHECK(bole_submit_blocks(session, "[]") == BOLE_E_INVALID_CONFIG,
        "empty blocks");
  CHECK(bole_cancel(NULL) == BOLE_E_INVALID_CONFIG, "null cancel");
  CHECK(bole_trace_export(c.handle, NULL, 0, "trace", NULL)
            == BOLE_E_INVALID_CONFIG,
        "null trace session");
  CHECK(bole_trace_export(c.handle, "s-invalid", 0, NULL, NULL)
            == BOLE_E_INVALID_CONFIG,
        "null trace path");
  CHECK(bole_trace_export(NULL, "s-invalid", 0, "trace", NULL)
            == BOLE_E_INVALID_CONFIG,
        "null trace handle");
  bole_handle* dummy = NULL;
  CHECK(bole_init(NULL, &dummy) == BOLE_E_INVALID_CONFIG, "null config");
  CHECK(bole_init(NULL, NULL) == BOLE_E_INVALID_ARGUMENT, "null out handle");
  CHECK(bole_release(NULL) == BOLE_E_INVALID_CONFIG, "null release");
  scenario_teardown(&c);
}

static void test_release_concurrent(void) {
  scenario c;
  scenario_setup(&c, "silent");
  CHECK(scenario_init(&c) == BOLE_OK, "init");
  wait_arg arg;
  arg.handle = c.handle;
  arg.code = 0;
  pthread_t thread;
  CHECK(pthread_create(&thread, NULL, wait_ready_thread, &arg) == 0,
        "start waiter");
  sleep_ms(100);
  uint64_t started = bole_monotonic_ms();
  CHECK(bole_release(c.handle) == BOLE_OK, "concurrent release");
  uint64_t elapsed = bole_monotonic_ms() - started;
  CHECK(elapsed < 500, "release returns immediately");
  c.handle = NULL;
  c.state.handle = NULL;
  pthread_join(thread, NULL);
  CHECK(arg.code == BOLE_E_RUNTIME_EXITED, "waiter sees runtime exit");
  sleep_ms(250);
  scenario_teardown(&c);
}

static void test_handshake_error(void) {
  scenario c;
  scenario_setup(&c, "handshake-error");
  CHECK(scenario_init(&c) == BOLE_OK, "init");
  CHECK(bole_wait_ready(c.handle, 5000) == BOLE_E_MODEL_NOT_FOUND,
        "handshake error code");
  CHECK(wait_flag(&c.state, &c.state.failed, 2000) == 0, "failed event");
  CHECK(c.state.failed_code == BOLE_E_MODEL_NOT_FOUND, "5004 event code");
  CHECK(strstr(c.state.failed_payload, "\"phase\":\"handshake\"") != NULL,
        "handshake phase");
  scenario_teardown(&c);
}

static void test_runtime_exit(void) {
  scenario c;
  scenario_setup(&c, "exit-after-ready");
  CHECK(scenario_init(&c) == BOLE_OK, "init");
  int32_t code = bole_wait_ready(c.handle, 5000);
  CHECK(code == BOLE_OK || code == BOLE_E_RUNTIME_EXITED, "ready or exit");
  if (code == BOLE_OK) {
    CHECK(wait_flag(&c.state, &c.state.exited, 2000) == 0, "runtime exited event");
    CHECK(c.state.counts[BOLE_EVENT_RUNTIME_EXITED] == 1, "one exit event");
    CHECK(c.state.counts[BOLE_EVENT_TASK_FINISHED] == 0,
          "no fabricated terminal event");
  }
  scenario_teardown(&c);
}

static void test_init_validation(void) {
  bole_handle* handle = NULL;
  CHECK(bole_init(NULL, &handle) == BOLE_E_INVALID_CONFIG, "null config");
  CHECK(bole_init(NULL, NULL) == BOLE_E_INVALID_ARGUMENT, "null out handle");

  bole_config config;
  memset(&config, 0, sizeof(config));
  CHECK(bole_init(&config, &handle) == BOLE_E_INVALID_CONFIG, "zero size");

  config.size = sizeof(config);
  config.runtime_dir = "/nonexistent-bole-runtime";
  config.dsh_home = "/tmp/bole-nonexistent-home";
  config.model_provider = "bole";
  config.model_name = "qwen3.8-27b";
  config.event_cb = on_event;
  CHECK(bole_init(&config, &handle) == BOLE_E_RUNTIME_DIR_NOT_FOUND,
        "missing runtime dir");

  config.event_cb = NULL;
  CHECK(bole_init(&config, &handle) == BOLE_E_EVENT_CALLBACK_REQUIRED,
        "missing callback");
}

int main(void) {
  test_happy();
  test_config_small();
  test_wait_timeout();
  test_stdout_violation();
  test_line_too_long();
  test_no_preset();
  test_session_rsi();
  test_cancel();
  test_errors();
  test_callback_restrictions();
  test_delta_flood();
  test_structural_overflow();
  test_fixture_replay();
  test_relative_paths();
  test_callback_replace();
  test_callback_queued();
  test_invalid_arguments();
  test_release_concurrent();
  test_handshake_error();
  test_runtime_exit();
  test_init_validation();

  if (g_failures != 0) {
    fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  printf("agent contract tests passed\n");
  return 0;
}
