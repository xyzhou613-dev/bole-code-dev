/**
 * @file fake_runtime.c
 * @brief Deterministic JSON-RPC runtime used by the native contract tests.
 *
 * The mode is selected with BOLE_FAKE_MODE (inherited from the test process):
 *   happy               full turn with text/reasoning deltas and one tool call
 *   banner              writes a non-JSON line before the handshake (2006)
 *   silent              never answers initialize (wait_ready timeout 2004)
 *   long-line           writes a frame larger than a small max_line (2005)
 *   no-preset           capabilities.preset=false; rsi.set -> RSI_UNAVAILABLE
 *   session-rsi         emits a session-scoped rsi.state with rsi-infer
 *   cancel              pauses mid-tool until session/cancel
 *   overflow-structural floods structural events (queue failure 2007)
 *   flood-delta         floods text deltas (deltas shed, turn completes)
 *   handshake-error     initialize returns MODEL_NOT_FOUND
 *   exit-after-ready    answers initialize and exits immediately
 *   replay              answers session/prompt then replays BOLE_FAKE_FIXTURE
 *
 * It implements just enough of the Bole protocol to exercise the C library:
 * initialize, session/create, session/prompt, session/cancel, session/trace,
 * rsi.set/get, and shutdown.
 */

#include "../src/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t next_event_id = 1;
static int active = 0;
static char active_session[256];

static const char* fake_mode(void) {
  const char* mode = getenv("BOLE_FAKE_MODE");
  return mode == NULL ? "happy" : mode;
}

static int mode_is(const char* name) {
  return strcmp(fake_mode(), name) == 0;
}

static void respond_ok(uint64_t id, const char* result_json) {
  printf("{\"jsonrpc\":\"2.0\",\"id\":%llu,\"result\":%s}\n",
         (unsigned long long)id, result_json);
  fflush(stdout);
}

static void respond_error(uint64_t id, int rpc_code, const char* code, const char* message) {
  printf("{\"jsonrpc\":\"2.0\",\"id\":%llu,\"error\":{\"code\":%d,\"message\":\"%s\",\"data\":{\"code\":\"%s\"}}}\n",
         (unsigned long long)id, rpc_code, message, code);
  fflush(stdout);
}

static void notify(const char* method, const char* params_json) {
  printf("{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s}\n", method, params_json);
  fflush(stdout);
}

static void emit_event(const char* session, uint32_t type, uint32_t task, uint32_t step,
                       uint64_t seq, const char* payload_json) {
  bole_buf params;
  bole_buf_init(&params, 512);
  bole_buf_append_cstr(&params, "{\"sessionId\":");
  bole_buf_append_json_string(&params, session);
  bole_buf_appendf(&params,
                   ",\"event\":{\"event_id\":%llu,\"type\":%u,\"session_id\":",
                   (unsigned long long)next_event_id++, type);
  bole_buf_append_json_string(&params, session);
  bole_buf_appendf(&params,
                   ",\"task\":%u,\"step\":%u,\"seq\":%llu,\"ts_ms\":1761100000000,\"payload\":%s}}",
                   task, step, (unsigned long long)seq, payload_json);
  notify("bole.event", params.data);
  bole_buf_free(&params);
}

static void emit_delta(const char* session, const char* channel, const char* text,
                       uint64_t revision) {
  bole_buf params;
  bole_buf_init(&params, 256);
  bole_buf_append_cstr(&params, "{\"sessionId\":");
  bole_buf_append_json_string(&params, session);
  bole_buf_append_cstr(&params, ",\"turn\":1,\"step\":1,\"channel\":\"");
  bole_buf_append_cstr(&params, channel);
  bole_buf_appendf(&params, "\",\"attemptId\":\"a1\",\"revision\":%llu,\"text\":",
                   (unsigned long long)revision);
  bole_buf_append_json_string(&params, text);
  bole_buf_append_cstr(&params, "}");
  notify("assistant.delta", params.data);
  bole_buf_free(&params);
}

static void emit_status(const char* session, const char* status) {
  bole_buf params;
  bole_buf_init(&params, 128);
  bole_buf_append_cstr(&params, "{\"sessionId\":");
  bole_buf_append_json_string(&params, session);
  bole_buf_append_cstr(&params, ",\"status\":\"");
  bole_buf_append_cstr(&params, status);
  bole_buf_append_cstr(&params, "\"}");
  notify("session.status", params.data);
  bole_buf_free(&params);
}

static int get_string(const bole_json_member* members, size_t count,
                      const char* key, char* out, size_t capacity) {
  const bole_json_member* member = bole_json_member_get(members, count, key);
  size_t length = 0;
  if (member == NULL || member->type != BOLE_JSON_STRING) {
    return -1;
  }
  return bole_json_string(member, out, capacity, &length) == 0 ? 0 : -1;
}

static void finish_turn(const char* session, const char* outcome) {
  bole_buf payload;
  bole_buf_init(&payload, 128);
  bole_buf_append_cstr(&payload, "{\"v\":1,\"turn\":1,\"outcome\":");
  bole_buf_append_json_string(&payload, outcome);
  bole_buf_append_cstr(&payload, "}");
  emit_event(session, 50, 1, 1, 7, payload.data);
  bole_buf_free(&payload);
}

static void handle_prompt(const char* session) {
  active = 1;
  strncpy(active_session, session, sizeof(active_session) - 1);
  active_session[sizeof(active_session) - 1] = '\0';
  emit_status(session, "running");

  if (mode_is("cancel")) {
    emit_event(session, 10, 1, 0, 1, "{\"v\":1,\"turn\":1}");
    emit_event(session, 11, 1, 1, 2, "{\"v\":1,\"turn\":1,\"step\":1}");
    emit_event(session, 30, 1, 1, 3,
               "{\"v\":1,\"callId\":\"c1\",\"tool\":\"toy_add\",\"args\":{\"a\":1,\"b\":2}}");
    return;
  }
  if (mode_is("overflow-structural")) {
    emit_event(session, 10, 1, 0, 1, "{\"v\":1,\"turn\":1}");
    for (uint64_t i = 0; i < 5000; i++) {
      emit_event(session, 10, 1, 0, i + 2, "{\"v\":1,\"turn\":1}");
    }
    finish_turn(session, "completed");
    active = 0;
    return;
  }
  if (mode_is("flood-delta")) {
    emit_event(session, 10, 1, 0, 1, "{\"v\":1,\"turn\":1}");
    for (uint64_t i = 0; i < 5000; i++) {
      emit_delta(session, "text", "x", i + 1);
    }
    emit_event(session, 50, 1, 1, 7,
               "{\"v\":1,\"turn\":1,\"outcome\":\"completed\"}");
    active = 0;
    emit_status(session, "idle");
    return;
  }

  emit_event(session, 10, 1, 0, 1, "{\"v\":1,\"turn\":1}");
  /* Reserved/unknown types must never reach the caller. */
  emit_event(session, 40, 1, 0, 2, "{\"v\":1,\"turn\":1}");
  emit_event(session, 99, 1, 0, 3, "{\"v\":1,\"turn\":1}");
  emit_event(session, 11, 1, 1, 4, "{\"v\":1,\"turn\":1,\"step\":1}");
  emit_delta(session, "text", "Hello ", 1);
  emit_delta(session, "text", "world", 2);
  emit_delta(session, "reasoning", "thinking", 3);
  emit_event(session, 22, 1, 1, 3,
             "{\"v\":1,\"turn\":1,\"step\":1,\"messageId\":\"m-1\","
             "\"text\":\"Hello world\",\"reasoning\":\"thinking\"}");
  emit_event(session, 30, 1, 1, 4,
             "{\"v\":1,\"callId\":\"c1\",\"tool\":\"toy_add\",\"args\":{\"a\":1,\"b\":2}}");
  emit_event(session, 31, 1, 1, 5,
             "{\"v\":1,\"callId\":\"c1\",\"tool\":\"toy_add\",\"isError\":false,"
             "\"resultSummary\":\"3\",\"truncated\":false}");
  emit_event(session, 12, 1, 1, 6, "{\"v\":1,\"turn\":1,\"step\":1}");
  emit_event(session, 50, 1, 1, 7,
             "{\"v\":1,\"turn\":1,\"outcome\":\"completed\"}");
  active = 0;
  emit_status(session, "idle");
}

static void write_trace(const char* path) {
  FILE* file = fopen(path, "wb");
  if (file == NULL) {
    return;
  }
  fputs("{\"seq\":1,\"type\":\"turn/start\",\"data\":{\"turn\":1}}\n", file);
  fputs("{\"seq\":2,\"type\":\"turn/end\",\"data\":{\"turn\":1,\"reason\":{\"kind\":\"completed\"}}}\n", file);
  fclose(file);
}

static void write_long_line(void) {
  fputs("{\"jsonrpc\":\"2.0\",\"method\":\"bole.event\",\"params\":{\"pad\":\"", stdout);
  for (int i = 0; i < 600; i++) {
    fputc('x', stdout);
  }
  fputs("\"}}\n", stdout);
  fflush(stdout);
}

int main(void) {
  const char* mode = fake_mode();
  if (strcmp(mode, "banner") == 0) {
    fputs("This line is not JSON-RPC\n", stdout);
    fflush(stdout);
    return 0;
  }

  char* line = NULL;
  size_t capacity = 0;
  ssize_t length;
  while ((length = getline(&line, &capacity, stdin)) >= 0) {
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
      length--;
    }
    if (length == 0) {
      continue;
    }
    bole_json_member top[16];
    size_t top_count = 0;
    if (bole_json_parse_object(line, (size_t)length, top, 16, &top_count) != 0) {
      continue;
    }
    uint64_t id = 0;
    const bole_json_member* id_member = bole_json_member_get(top, top_count, "id");
    if (id_member != NULL) {
      bole_json_u64(id_member, &id);
    }
    char method[64];
    if (get_string(top, top_count, "method", method, sizeof(method)) != 0) {
      continue;
    }
    const bole_json_member* params_member = bole_json_member_get(top, top_count, "params");
    bole_json_member params[16];
    size_t params_count = 0;
    if (params_member != NULL && params_member->type == BOLE_JSON_OBJECT) {
      bole_json_parse_object(params_member->value, params_member->value_len,
                             params, 16, &params_count);
    }

    if (strcmp(method, "initialize") == 0) {
      if (strcmp(mode, "silent") == 0) {
        continue;
      }
      if (strcmp(mode, "handshake-error") == 0) {
        respond_error(id, -32603, "MODEL_NOT_FOUND", "model not found");
        continue;
      }
      int preset = strcmp(mode, "no-preset") != 0;
      bole_buf result;
      bole_buf_init(&result, 256);
      bole_buf_append_cstr(&result,
          "{\"serverInfo\":{\"name\":\"bole-fake-runtime\",\"version\":\"1.0.0\"},"
          "\"sdkProtocolVersion\":1,\"capabilities\":{\"preset\":");
      bole_buf_append_cstr(&result, preset ? "true" : "false");
      bole_buf_append_cstr(&result,
          ",\"sandbox\":\"workspace-write\",\"persistence\":true,\"trace\":true}}");
      respond_ok(id, result.data);
      bole_buf_free(&result);
      if (strcmp(mode, "exit-after-ready") == 0) {
        break;
      }
      if (!preset) {
        notify("rsi.state",
               "{\"scope\":\"default\",\"enabled\":false,\"preset\":\"\","
               "\"composed\":false,\"source\":\"config\"}");
      }
    } else if (strcmp(method, "session/create") == 0) {
      char session[128];
      char preset[64] = "standard";
      char cwd[512] = "/tmp";
      if (get_string(params, params_count, "sessionId", session, sizeof(session)) != 0) {
        respond_error(id, -32602, "INVALID_ARGUMENT", "sessionId required");
        continue;
      }
      if (strcmp(session, "exists") == 0) {
        respond_error(id, -32603, "SESSION_EXISTS", "session exists");
        continue;
      }
      get_string(params, params_count, "preset", preset, sizeof(preset));
      get_string(params, params_count, "cwd", cwd, sizeof(cwd));
      bole_buf result;
      bole_buf_init(&result, 256);
      bole_buf_append_cstr(&result, "{\"sessionId\":");
      bole_buf_append_json_string(&result, session);
      bole_buf_append_cstr(&result, ",\"preset\":");
      bole_buf_append_json_string(&result, preset);
      bole_buf_append_cstr(&result, ",\"cwd\":");
      bole_buf_append_json_string(&result, cwd);
      bole_buf_append_cstr(&result, "}");
      respond_ok(id, result.data);
      bole_buf_free(&result);
      if (mode_is("session-rsi")) {
        bole_buf state;
        bole_buf_init(&state, 256);
        bole_buf_append_cstr(&state, "{\"scope\":\"session\",\"sessionId\":");
        bole_buf_append_json_string(&state, session);
        bole_buf_append_cstr(&state,
            ",\"enabled\":true,\"preset\":\"rsi-infer\",\"composed\":true,"
            "\"source\":\"session\"}");
        notify("rsi.state", state.data);
        bole_buf_free(&state);
      }
    } else if (strcmp(method, "session/resume") == 0) {
      char session[128];
      if (get_string(params, params_count, "sessionId", session, sizeof(session)) != 0) {
        respond_error(id, -32602, "INVALID_ARGUMENT", "sessionId required");
        continue;
      }
      if (strcmp(session, "missing") == 0) {
        respond_error(id, -32603, "SESSION_NOT_FOUND", "session not found");
        continue;
      }
      if (strcmp(session, "unavailable") == 0) {
        respond_error(id, -32603, "PERSISTENCE_UNAVAILABLE",
                      "session persistence unavailable");
        continue;
      }
      if (strcmp(session, "corrupt") == 0) {
        respond_error(id, -32603, "SESSION_NOT_RESUMABLE", "session not resumable");
        continue;
      }
      bole_buf result;
      bole_buf_init(&result, 256);
      bole_buf_append_cstr(&result, "{\"sessionId\":");
      bole_buf_append_json_string(&result, session);
      bole_buf_append_cstr(&result, ",\"preset\":\"standard\",\"cwd\":\"/tmp\"}");
      respond_ok(id, result.data);
      bole_buf_free(&result);
    } else if (strcmp(method, "session/setPreset") == 0) {
      char preset[64];
      if (get_string(params, params_count, "preset", preset, sizeof(preset)) != 0) {
        respond_error(id, -32602, "INVALID_ARGUMENT", "preset required");
        continue;
      }
      if (strcmp(preset, "bogus") == 0) {
        respond_error(id, -32603, "PRESET_NOT_FOUND", "preset not found");
        continue;
      }
      if (strcmp(preset, "locked") == 0) {
        respond_error(id, -32603, "PRESET_LOCKED", "preset locked");
        continue;
      }
      bole_buf result;
      bole_buf_init(&result, 128);
      bole_buf_append_cstr(&result, "{\"sessionId\":\"s\",\"preset\":");
      bole_buf_append_json_string(&result, preset);
      bole_buf_append_cstr(&result, "}");
      respond_ok(id, result.data);
      bole_buf_free(&result);
    } else if (strcmp(method, "session/prompt") == 0) {
      char session[128];
      if (get_string(params, params_count, "sessionId", session, sizeof(session)) != 0) {
        respond_error(id, -32602, "INVALID_ARGUMENT", "sessionId required");
        continue;
      }
      if (mode_is("long-line")) {
        write_long_line();
        continue;
      }
      respond_ok(id, "{\"messageId\":\"m-1\"}");
      if (mode_is("replay")) {
        const char* fixture = getenv("BOLE_FAKE_FIXTURE");
        FILE* file = fixture == NULL ? NULL : fopen(fixture, "rb");
        if (file != NULL) {
          char* fixture_line = NULL;
          size_t fixture_capacity = 0;
          ssize_t fixture_length;
          while ((fixture_length = getline(&fixture_line, &fixture_capacity, file)) >= 0) {
            fwrite(fixture_line, 1, (size_t)fixture_length, stdout);
          }
          free(fixture_line);
          fclose(file);
          fflush(stdout);
        }
        active = 0;
        continue;
      }
      handle_prompt(session);
    } else if (strcmp(method, "session/cancel") == 0) {
      char session[128];
      if (get_string(params, params_count, "sessionId", session, sizeof(session)) != 0) {
        respond_error(id, -32602, "INVALID_ARGUMENT", "sessionId required");
        continue;
      }
      if (!active) {
        respond_error(id, -32603, "NO_ACTIVE_TASK", "no active task");
        continue;
      }
      active = 0;
      respond_ok(id, "{}");
      finish_turn(session, "aborted");
      emit_status(session, "idle");
    } else if (strcmp(method, "session/trace") == 0) {
      char path[512];
      if (get_string(params, params_count, "path", path, sizeof(path)) != 0) {
        respond_error(id, -32602, "INVALID_ARGUMENT", "path required");
        continue;
      }
      if (strstr(path, "fail") != NULL) {
        respond_error(id, -32603, "TRACE_WRITE_FAILED", "trace write failed");
        continue;
      }
      write_trace(path);
      bole_buf result;
      bole_buf_init(&result, 256);
      bole_buf_append_cstr(&result, "{\"path\":");
      bole_buf_append_json_string(&result, path);
      bole_buf_append_cstr(&result, ",\"eventCount\":2}");
      respond_ok(id, result.data);
      bole_buf_free(&result);
    } else if (strcmp(method, "rsi.set") == 0) {
      if (mode_is("no-preset")) {
        respond_error(id, -32603, "RSI_UNAVAILABLE", "no preset plane");
        continue;
      }
      int enabled = 0;
      const bole_json_member* enabled_member =
          bole_json_member_get(params, params_count, "enabled");
      if (enabled_member == NULL || bole_json_bool(enabled_member, &enabled) != 0) {
        respond_error(id, -32602, "INVALID_ARGUMENT", "enabled required");
        continue;
      }
      respond_ok(id, enabled
          ? "{\"enabled\":true,\"preset\":\"rsi-infer\",\"scope\":\"default\"}"
          : "{\"enabled\":false,\"preset\":\"standard\",\"scope\":\"default\"}");
    } else if (strcmp(method, "rsi.get") == 0) {
      respond_ok(id, "{\"enabled\":false,\"preset\":\"standard\",\"composed\":true}");
    } else if (strcmp(method, "shutdown") == 0) {
      respond_ok(id, "{}");
      break;
    } else {
      respond_error(id, -32601, "METHOD_NOT_FOUND", "method not found");
    }
  }
  free(line);
  return 0;
}
