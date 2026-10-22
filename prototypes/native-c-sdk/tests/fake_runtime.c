/**
 * @file fake_runtime.c
 * @brief Deterministic JSON-RPC runtime used by the native contract tests.
 *
 * It implements just enough of the Bole protocol to exercise the C library:
 * initialize, session/create, session/prompt, session/cancel, session/trace,
 * rsi.set/get, and shutdown. Every prompt emits a fixed event sequence.
 */

#include "../src/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t next_event_id = 1;
static int active = 0;

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

static void emit_delta(const char* session, const char* channel, const char* text) {
  bole_buf params;
  bole_buf_init(&params, 256);
  bole_buf_append_cstr(&params, "{\"sessionId\":");
  bole_buf_append_json_string(&params, session);
  bole_buf_append_cstr(&params, ",\"turn\":1,\"step\":1,\"channel\":\"");
  bole_buf_append_cstr(&params, channel);
  bole_buf_append_cstr(&params, "\",\"attemptId\":\"a1\",\"revision\":1,\"text\":");
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

static void handle_prompt(const char* session) {
  active = 1;
  emit_status(session, "running");
  emit_event(session, 10, 1, 0, 1, "{\"v\":1,\"turn\":1}");
  emit_event(session, 11, 1, 1, 2, "{\"v\":1,\"turn\":1,\"step\":1}");
  emit_delta(session, "text", "Hello ");
  emit_delta(session, "text", "world");
  emit_delta(session, "reasoning", "thinking");
  emit_event(session, 22, 1, 1, 3,
             "{\"v\":1,\"turn\":1,\"step\":1,\"messageId\":\"m-1\","
             "\"text\":\"Hello world\",\"reasoning\":\"thinking\"}");
  emit_event(session, 30, 1, 1, 4,
             "{\"v\":1,\"callId\":\"c1\",\"tool\":\"toy_add\",\"args\":{\"a\":1,\"b\":2}}");
  emit_event(session, 31, 1, 1, 5,
             "{\"v\":1,\"callId\":\"c1\",\"tool\":\"toy_add\",\"isError\":false,\"resultSummary\":\"3\"}");
  emit_event(session, 12, 1, 1, 6, "{\"v\":1,\"turn\":1,\"step\":1}");
  emit_event(session, 50, 1, 1, 7, "{\"v\":1,\"outcome\":\"completed\",\"turn\":1}");
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

int main(void) {
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
      respond_ok(id, "{\"serverInfo\":{\"name\":\"bole-fake-runtime\",\"version\":\"1.0.0\"},"
                     "\"sdkProtocolVersion\":1,"
                     "\"capabilities\":{\"preset\":true,\"sandbox\":\"workspace-write\","
                     "\"persistence\":true,\"trace\":true}}");
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
      respond_ok(id, "{\"messageId\":\"m-1\"}");
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
      emit_event(session, 50, 1, 1, 7, "{\"v\":1,\"outcome\":\"aborted\",\"turn\":1}");
    } else if (strcmp(method, "session/trace") == 0) {
      char path[512];
      if (get_string(params, params_count, "path", path, sizeof(path)) != 0) {
        respond_error(id, -32602, "INVALID_ARGUMENT", "path required");
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
      int enabled = 0;
      const bole_json_member* enabled_member = bole_json_member_get(params, params_count, "enabled");
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
