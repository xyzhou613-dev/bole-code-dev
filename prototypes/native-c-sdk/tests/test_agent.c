#include "bole_agent.h"
#include "platform.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef FAKE_RUNTIME_PATH
#define FAKE_RUNTIME_PATH "/tmp/fake_runtime"
#endif

typedef struct test_state {
  bole_mutex lock;
  bole_cond cond;
  int ready;
  int failed;
  int finished;
  int counts[64];
  char tool_args[512];
  char tool_summary[512];
  char ready_payload[1024];
} test_state;

static void BOLE_CALL on_event(void* user_data, const bole_event* event) {
  test_state* state = (test_state*)user_data;
  bole_mutex_lock(&state->lock);
  if (event->type < 64) {
    state->counts[event->type]++;
  }
  if (event->type == BOLE_EVENT_RUNTIME_READY) {
    state->ready = 1;
    if (event->payload_json != NULL) {
      strncpy(state->ready_payload, event->payload_json, sizeof(state->ready_payload) - 1);
    }
  } else if (event->type == BOLE_EVENT_RUNTIME_FAILED) {
    state->failed = 1;
  } else if (event->type == BOLE_EVENT_TASK_FINISHED) {
    state->finished = 1;
  } else if (event->type == BOLE_EVENT_TOOL_CALL_STARTED) {
    if (event->payload_json != NULL) {
      strncpy(state->tool_args, event->payload_json, sizeof(state->tool_args) - 1);
    }
  } else if (event->type == BOLE_EVENT_TOOL_CALL_FINISHED) {
    if (event->payload_json != NULL) {
      strncpy(state->tool_summary, event->payload_json, sizeof(state->tool_summary) - 1);
    }
  }
  bole_cond_broadcast(&state->cond);
  bole_mutex_unlock(&state->lock);
}

static int wait_flag(test_state* state, int* flag, int timeout_ms) {
  uint64_t deadline = bole_monotonic_ms() + (uint64_t)timeout_ms;
  bole_mutex_lock(&state->lock);
  while (!*flag) {
    uint64_t now = bole_monotonic_ms();
    if (now >= deadline) {
      bole_mutex_unlock(&state->lock);
      return -1;
    }
    bole_cond_wait(&state->cond, &state->lock, (int)(deadline - now));
  }
  bole_mutex_unlock(&state->lock);
  return 0;
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

int main(void) {
  char root_template[] = "/tmp/bole-agent-test-XXXXXX";
  char* root = mkdtemp(root_template);
  assert(root != NULL);

  char runtime_dir[512];
  char dsh_home[512];
  char cwd[512];
  char path[1024];
  snprintf(runtime_dir, sizeof(runtime_dir), "%s/runtime", root);
  snprintf(dsh_home, sizeof(dsh_home), "%s/home", root);
  snprintf(cwd, sizeof(cwd), "%s/work", root);
  mkdir(runtime_dir, 0755);
  mkdir(cwd, 0755);

  snprintf(path, sizeof(path), "%s/node_modules", runtime_dir);
  mkdir(path, 0755);
  snprintf(path, sizeof(path), "%s/node_modules/@deepseek-ai", runtime_dir);
  mkdir(path, 0755);
  snprintf(path, sizeof(path), "%s/node_modules/@deepseek-ai/dsh", runtime_dir);
  mkdir(path, 0755);
  snprintf(path, sizeof(path), "%s/node_modules/@deepseek-ai/dsh/lib", runtime_dir);
  mkdir(path, 0755);
  snprintf(path, sizeof(path), "%s/node_modules/@deepseek-ai/dsh/lib/bin.js", runtime_dir);
  write_file(path, "// fake entry, never executed\n", 0);
  snprintf(path, sizeof(path), "%s/cordis.patch.yml", runtime_dir);
  write_file(path, "[]\n", 0);
  snprintf(path, sizeof(path), "%s/node.exe", runtime_dir);
  {
    char script[1024];
    snprintf(script, sizeof(script), "#!/bin/sh\nexec %s\n", FAKE_RUNTIME_PATH);
    write_file(path, script, 1);
  }

  test_state state;
  memset(&state, 0, sizeof(state));
  assert(bole_mutex_init(&state.lock) == 0);
  assert(bole_cond_init(&state.cond) == 0);

  bole_config config;
  memset(&config, 0, sizeof(config));
  config.size = sizeof(config);
  config.runtime_dir = runtime_dir;
  config.dsh_home = dsh_home;
  config.cwd = cwd;
  config.model_provider = "bole";
  config.model_name = "qwen3.8-27b";
  config.event_cb = on_event;
  config.user_data = &state;
  config.rsi_enabled = 0;

  bole_handle* handle = NULL;
  int32_t code = bole_init(&config, &handle);
  assert(code == BOLE_OK && handle != NULL);

  code = bole_wait_ready(handle, 5000);
  if (code != BOLE_OK) {
    fprintf(stderr, "wait_ready failed: %d\n", (int)code);
    return 1;
  }
  assert(wait_flag(&state, &state.ready, 2000) == 0);
  printf("ready payload: %s\n", state.ready_payload);

  bole_session* session = NULL;
  code = bole_session_create(handle, "s-001", NULL, &session);
  assert(code == BOLE_OK && session != NULL);

  code = bole_submit(session, "hello");
  assert(code == BOLE_OK);
  assert(wait_flag(&state, &state.finished, 5000) == 0);

  assert(state.counts[BOLE_EVENT_TASK_STARTED] == 1);
  assert(state.counts[BOLE_EVENT_STEP_STARTED] == 1);
  assert(state.counts[BOLE_EVENT_STEP_FINISHED] == 1);
  assert(state.counts[BOLE_EVENT_TEXT_DELTA] == 2);
  assert(state.counts[BOLE_EVENT_REASONING_DELTA] == 1);
  assert(state.counts[BOLE_EVENT_ASSISTANT_MESSAGE] == 1);
  assert(state.counts[BOLE_EVENT_TOOL_CALL_STARTED] == 1);
  assert(state.counts[BOLE_EVENT_TOOL_CALL_FINISHED] == 1);
  assert(state.counts[BOLE_EVENT_TASK_FINISHED] == 1);
  assert(strstr(state.tool_args, "toy_add") != NULL);
  assert(strstr(state.tool_summary, "\"isError\":false") != NULL);

  code = bole_cancel(session);
  assert(code == BOLE_E_NO_ACTIVE_TASK);

  code = bole_rsi_set(handle, 1);
  assert(code == BOLE_OK);
  bole_rsi_state rsi;
  memset(&rsi, 0, sizeof(rsi));
  rsi.size = sizeof(rsi);
  assert(bole_rsi_get(handle, &rsi) == BOLE_OK);
  assert(rsi.enabled == 1);
  assert(rsi.composed == 1);
  assert(strcmp(rsi.preset, "rsi-infer") == 0);

  snprintf(path, sizeof(path), "%s/trace.jsonl", cwd);
  uint32_t event_count = 0;
  code = bole_trace_export(handle, "s-001", 0, path, &event_count);
  assert(code == BOLE_OK);
  assert(event_count == 2);
  assert(access(path, F_OK) == 0);

  code = bole_release(handle);
  assert(code == BOLE_OK);

  bole_cond_destroy(&state.cond);
  bole_mutex_destroy(&state.lock);
  printf("agent integration test passed\n");
  return 0;
}
