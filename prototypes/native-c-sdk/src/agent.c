#include "bole_agent.h"
#include "error.h"
#include "json.h"
#include "platform.h"
#include "rpc.h"
#include "transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
extern char** environ;
#endif

#define BOLE_DEFAULT_MAX_LINE (64u * 1024u * 1024u)
#define BOLE_EVENT_QUEUE_MAX 4096u
#define BOLE_EVENT_BYTES_MAX (16u * 1024u * 1024u)
#define BOLE_MAX_SESSIONS 256u
#define BOLE_INIT_TIMEOUT_MS 30000
#define BOLE_CONTROL_TIMEOUT_MS 5000
#define BOLE_RESUME_TIMEOUT_MS 10000
#define BOLE_TRACE_TIMEOUT_MS 30000

struct bole_session {
  bole_handle* handle;
  char* id;
  struct bole_session* next;
};

typedef struct bole_event_node {
  struct bole_event_node* next;
  bole_event event;
  char* session_id;
  char* payload;
  size_t bytes;
} bole_event_node;

struct bole_handle {
  bole_config config;

  bole_process process;
  bole_transport transport;
  bole_rpc rpc;

  bole_thread event_thread;
  bole_thread wait_thread;
  int event_thread_started;
  int wait_thread_started;

  bole_mutex state_lock;
  bole_cond state_cond;
  int ready;
  int failed;
  int exited;
  volatile int stopping;
  int32_t failure_code;
  char failure_message[512];
  uint32_t protocol_version;
  uint64_t init_id;
  int running;

  bole_event_cb event_cb;
  void* user_data;

  bole_mutex event_lock;
  bole_cond event_cond;
  bole_event_node* event_head;
  bole_event_node* event_tail;
  size_t event_count;
  size_t event_bytes;
  int event_stop;
  uint64_t next_event_id;

  bole_session* sessions;
  size_t session_count;

  bole_rsi_state rsi;

  int32_t last_code;
  char last_message[512];

  FILE* log;
  bole_mutex log_lock;
};

static char* bole_strdup(const char* text) {
  if (text == NULL) {
    return NULL;
  }
  size_t length = strlen(text) + 1;
  char* copy = (char*)malloc(length);
  if (copy != NULL) {
    memcpy(copy, text, length);
  }
  return copy;
}

static int is_absolute_path(const char* path) {
  if (path == NULL || path[0] == '\0') {
    return 0;
  }
#ifdef _WIN32
  if (path[0] == '\\' && path[1] == '\\') {
    return 1;
  }
  return ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))
      && path[1] == ':';
#else
  return path[0] == '/';
#endif
}

static const char* path_separator(void) {
#ifdef _WIN32
  return "\\";
#else
  return "/";
#endif
}

static char* path_join(const char* base, const char* name) {
  if (base == NULL || name == NULL) {
    return NULL;
  }
  size_t base_len = strlen(base);
  size_t name_len = strlen(name);
  int need_sep = base_len > 0 && base[base_len - 1] != '/' && base[base_len - 1] != '\\';
  char* result = (char*)malloc(base_len + (need_sep ? 1 : 0) + name_len + 1);
  if (result == NULL) {
    return NULL;
  }
  memcpy(result, base, base_len);
  size_t offset = base_len;
  if (need_sep) {
    const char* sep = path_separator();
    result[offset++] = sep[0];
  }
  memcpy(result + offset, name, name_len + 1);
  return result;
}

static void config_free(bole_config* config) {
  free((void*)config->runtime_dir);
  free((void*)config->dsh_home);
  free((void*)config->cwd);
  free((void*)config->model_provider);
  free((void*)config->model_name);
  free((void*)config->reasoning_effort);
  free((void*)config->log_path);
  memset(config, 0, sizeof(*config));
}

static int32_t config_copy(bole_config* target, const bole_config* source) {
  if (source == NULL || source->size < sizeof(bole_config)) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (source->runtime_dir == NULL || source->runtime_dir[0] == '\0'
      || source->dsh_home == NULL || source->dsh_home[0] == '\0'
      || source->model_provider == NULL || source->model_provider[0] == '\0'
      || source->model_name == NULL || source->model_name[0] == '\0') {
    return BOLE_E_INVALID_CONFIG;
  }
  if (source->event_cb == NULL) {
    return BOLE_E_EVENT_CALLBACK_REQUIRED;
  }
  if (source->rsi_enabled != 0 && source->rsi_enabled != 1) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (source->sandbox_mode < BOLE_SANDBOX_DEFAULT
      || source->sandbox_mode > BOLE_SANDBOX_DANGER_FULL_ACCESS) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (source->max_tokens < 0) {
    return BOLE_E_INVALID_CONFIG;
  }
  memset(target, 0, sizeof(*target));
  target->size = sizeof(bole_config);
  target->runtime_dir = bole_strdup(source->runtime_dir);
  target->dsh_home = bole_strdup(source->dsh_home);
  target->cwd = bole_strdup(source->cwd);
  target->model_provider = bole_strdup(source->model_provider);
  target->model_name = bole_strdup(source->model_name);
  target->reasoning_effort = bole_strdup(source->reasoning_effort);
  target->log_path = bole_strdup(source->log_path);
  target->max_tokens = source->max_tokens;
  target->rsi_enabled = source->rsi_enabled;
  target->sandbox_mode = source->sandbox_mode;
  target->max_line_bytes = source->max_line_bytes;
  target->event_cb = source->event_cb;
  target->user_data = source->user_data;
  if (target->runtime_dir == NULL || target->dsh_home == NULL
      || target->model_provider == NULL || target->model_name == NULL
      || (source->cwd != NULL && target->cwd == NULL)
      || (source->reasoning_effort != NULL && target->reasoning_effort == NULL)
      || (source->log_path != NULL && target->log_path == NULL)) {
    config_free(target);
    return BOLE_E_INVALID_CONFIG;
  }
  if (target->cwd == NULL) {
    target->cwd = bole_getcwd();
    if (target->cwd == NULL) {
      config_free(target);
      return BOLE_E_INVALID_CONFIG;
    }
  }
  if (!is_absolute_path(target->cwd)) {
    config_free(target);
    return BOLE_E_INVALID_CONFIG;
  }
  if (target->max_line_bytes == 0) {
    target->max_line_bytes = BOLE_DEFAULT_MAX_LINE;
  }
  if (target->max_line_bytes < 1024) {
    config_free(target);
    return BOLE_E_INVALID_CONFIG;
  }
  return BOLE_OK;
}

static void log_line(bole_handle* handle, const char* text, size_t length) {
  if (handle->log != NULL) {
    bole_mutex_lock(&handle->log_lock);
    fprintf(handle->log, "[bole] %.*s\n", (int)length, text);
    fflush(handle->log);
    bole_mutex_unlock(&handle->log_lock);
  } else {
    fprintf(stderr, "[bole] %.*s\n", (int)length, text);
  }
}

static void record_error(bole_handle* handle, int32_t code, const char* message) {
  bole_mutex_lock(&handle->state_lock);
  handle->last_code = code;
  if (message != NULL) {
    strncpy(handle->last_message, message, sizeof(handle->last_message) - 1);
    handle->last_message[sizeof(handle->last_message) - 1] = '\0';
  } else {
    handle->last_message[0] = '\0';
  }
  bole_mutex_unlock(&handle->state_lock);
  log_line(handle, message == NULL ? bole_error_text(code) : message,
           strlen(message == NULL ? bole_error_text(code) : message));
}

static void set_failed(bole_handle* handle, int32_t code, const char* message) {
  bole_mutex_lock(&handle->state_lock);
  if (!handle->failed) {
    handle->failed = 1;
    handle->failure_code = code;
    handle->last_code = code;
    if (message != NULL) {
      strncpy(handle->failure_message, message, sizeof(handle->failure_message) - 1);
      handle->failure_message[sizeof(handle->failure_message) - 1] = '\0';
      strncpy(handle->last_message, message, sizeof(handle->last_message) - 1);
      handle->last_message[sizeof(handle->last_message) - 1] = '\0';
    }
  }
  bole_cond_broadcast(&handle->state_cond);
  bole_mutex_unlock(&handle->state_lock);
}

static void set_ready(bole_handle* handle, uint32_t protocol_version) {
  bole_mutex_lock(&handle->state_lock);
  handle->ready = 1;
  handle->protocol_version = protocol_version;
  bole_cond_broadcast(&handle->state_cond);
  bole_mutex_unlock(&handle->state_lock);
}

static int event_enqueue(bole_handle* handle, uint32_t type, const char* session_id,
                         uint32_t task, uint32_t step, uint64_t seq, int64_t ts_ms,
                         const char* payload, size_t payload_len) {
  if (handle->stopping) {
    return -1;
  }
  bole_event_node* node = (bole_event_node*)calloc(1, sizeof(bole_event_node));
  if (node == NULL) {
    return -1;
  }
  node->session_id = bole_strdup(session_id == NULL ? "" : session_id);
  node->payload = (char*)malloc(payload_len + 1);
  if (node->session_id == NULL || node->payload == NULL) {
    free(node->session_id);
    free(node->payload);
    free(node);
    return -1;
  }
  if (payload_len > 0) {
    memcpy(node->payload, payload, payload_len);
  }
  node->payload[payload_len] = '\0';
  node->bytes = sizeof(*node) + strlen(node->session_id) + payload_len + 2;
  node->event.size = sizeof(bole_event);
  node->event.type = type;
  node->event.session_id = node->session_id;
  node->event.payload_json = node->payload;
  node->event.task = task;
  node->event.step = step;
  node->event.seq = seq;
  node->event.ts_ms = ts_ms != 0 ? ts_ms : bole_wall_ms();

  bole_mutex_lock(&handle->event_lock);
  if (handle->event_stop) {
    bole_mutex_unlock(&handle->event_lock);
    free(node->session_id);
    free(node->payload);
    free(node);
    return -1;
  }
  if (handle->event_count >= BOLE_EVENT_QUEUE_MAX
      || handle->event_bytes + node->bytes > BOLE_EVENT_BYTES_MAX) {
    if (type == BOLE_EVENT_TEXT_DELTA || type == BOLE_EVENT_REASONING_DELTA) {
      bole_event_node** link = &handle->event_head;
      bole_event_node* victim = NULL;
      while (*link != NULL) {
        if ((*link)->event.type == BOLE_EVENT_TEXT_DELTA
            || (*link)->event.type == BOLE_EVENT_REASONING_DELTA) {
          victim = *link;
          *link = victim->next;
          break;
        }
        link = &(*link)->next;
      }
      if (victim == NULL) {
        bole_mutex_unlock(&handle->event_lock);
        free(node->session_id);
        free(node->payload);
        free(node);
        return -1;
      }
      handle->event_count--;
      handle->event_bytes -= victim->bytes;
      free(victim->session_id);
      free(victim->payload);
      free(victim);
    } else {
      bole_mutex_unlock(&handle->event_lock);
      free(node->session_id);
      free(node->payload);
      free(node);
      return -1;
    }
  }
  node->event.event_id = handle->next_event_id++;
  if (handle->event_tail == NULL) {
    handle->event_head = node;
  } else {
    handle->event_tail->next = node;
  }
  handle->event_tail = node;
  handle->event_count++;
  handle->event_bytes += node->bytes;
  bole_cond_signal(&handle->event_cond);
  bole_mutex_unlock(&handle->event_lock);
  return 0;
}

static void event_thread_main(void* argument) {
  bole_handle* handle = (bole_handle*)argument;
  for (;;) {
    bole_mutex_lock(&handle->event_lock);
    while (handle->event_head == NULL && !handle->event_stop) {
      bole_cond_wait(&handle->event_cond, &handle->event_lock, -1);
    }
    if (handle->event_head == NULL && handle->event_stop) {
      bole_mutex_unlock(&handle->event_lock);
      break;
    }
    bole_event_node* node = handle->event_head;
    handle->event_head = node->next;
    if (handle->event_head == NULL) {
      handle->event_tail = NULL;
    }
    handle->event_count--;
    handle->event_bytes -= node->bytes;
    bole_mutex_unlock(&handle->event_lock);

    bole_event_cb callback = NULL;
    void* user_data = NULL;
    bole_mutex_lock(&handle->state_lock);
    callback = handle->event_cb;
    user_data = handle->user_data;
    bole_mutex_unlock(&handle->state_lock);
    if (callback != NULL) {
      callback(user_data, &node->event);
    }
    free(node->session_id);
    free(node->payload);
    free(node);
  }
  return;
}

static void event_thread_stop(bole_handle* handle) {
  if (!handle->event_thread_started) {
    return;
  }
  bole_mutex_lock(&handle->event_lock);
  handle->event_stop = 1;
  bole_cond_broadcast(&handle->event_cond);
  bole_mutex_unlock(&handle->event_lock);
  bole_thread_join(&handle->event_thread);
  handle->event_thread_started = 0;

  bole_mutex_lock(&handle->event_lock);
  bole_event_node* node = handle->event_head;
  handle->event_head = NULL;
  handle->event_tail = NULL;
  handle->event_count = 0;
  handle->event_bytes = 0;
  bole_mutex_unlock(&handle->event_lock);
  while (node != NULL) {
    bole_event_node* next = node->next;
    free(node->session_id);
    free(node->payload);
    free(node);
    node = next;
  }
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

static uint32_t get_u32(const bole_json_member* members, size_t count, const char* key) {
  const bole_json_member* member = bole_json_member_get(members, count, key);
  uint64_t value = 0;
  if (member == NULL || bole_json_u64(member, &value) != 0) {
    return 0;
  }
  return (uint32_t)value;
}

static uint64_t get_u64(const bole_json_member* members, size_t count, const char* key) {
  const bole_json_member* member = bole_json_member_get(members, count, key);
  uint64_t value = 0;
  if (member == NULL || bole_json_u64(member, &value) != 0) {
    return 0;
  }
  return value;
}

static int64_t get_i64(const bole_json_member* members, size_t count, const char* key) {
  const bole_json_member* member = bole_json_member_get(members, count, key);
  int64_t value = 0;
  if (member == NULL || bole_json_i64(member, &value) != 0) {
    return 0;
  }
  return value;
}

static int get_bool(const bole_json_member* members, size_t count, const char* key, int fallback) {
  const bole_json_member* member = bole_json_member_get(members, count, key);
  int value = 0;
  if (member == NULL || bole_json_bool(member, &value) != 0) {
    return fallback;
  }
  return value;
}

static const bole_json_member* get_object(const bole_json_member* members, size_t count,
                                          const char* key, bole_json_member* out,
                                          size_t capacity, size_t* out_count) {
  const bole_json_member* member = bole_json_member_get(members, count, key);
  if (member == NULL || member->type != BOLE_JSON_OBJECT) {
    return NULL;
  }
  if (bole_json_parse_object(member->value, member->value_len, out, capacity, out_count) != 0) {
    return NULL;
  }
  return member;
}

static void update_rsi_from_payload(bole_handle* handle, const char* payload, size_t payload_len) {
  bole_json_member members[16];
  size_t count = 0;
  if (bole_json_parse_object(payload, payload_len, members, 16, &count) != 0) {
    return;
  }
  int enabled = get_bool(members, count, "enabled", handle->rsi.enabled);
  int composed = get_bool(members, count, "composed", handle->rsi.composed);
  char preset[BOLE_PRESET_MAX] = "";
  get_string(members, count, "preset", preset, sizeof(preset));
  bole_mutex_lock(&handle->state_lock);
  handle->rsi.size = sizeof(bole_rsi_state);
  handle->rsi.enabled = enabled;
  handle->rsi.composed = composed;
  strncpy(handle->rsi.preset, preset, sizeof(handle->rsi.preset) - 1);
  handle->rsi.preset[sizeof(handle->rsi.preset) - 1] = '\0';
  bole_mutex_unlock(&handle->state_lock);
}

static void handle_bole_event(bole_handle* handle, const bole_json_member* params, size_t count) {
  char session[256] = "";
  get_string(params, count, "sessionId", session, sizeof(session));

  bole_json_member event[16];
  size_t event_count = 0;
  if (get_object(params, count, "event", event, 16, &event_count) == NULL) {
    return;
  }
  uint32_t type = get_u32(event, event_count, "type");
  if (type == 0 || type <= 3) {
    return;
  }
  uint32_t task = get_u32(event, event_count, "task");
  uint32_t step = get_u32(event, event_count, "step");
  uint64_t seq = get_u64(event, event_count, "seq");
  int64_t ts_ms = get_i64(event, event_count, "ts_ms");
  char event_session[256] = "";
  if (get_string(event, event_count, "session_id", event_session, sizeof(event_session)) == 0
      && event_session[0] != '\0') {
    strncpy(session, event_session, sizeof(session) - 1);
    session[sizeof(session) - 1] = '\0';
  }

  const bole_json_member* payload = bole_json_member_get(event, event_count, "payload");
  const char* payload_text = "{}";
  size_t payload_len = 2;
  if (payload != NULL && payload->type == BOLE_JSON_OBJECT) {
    payload_text = payload->value;
    payload_len = payload->value_len;
  }
  if (type == BOLE_EVENT_RSI_STATE) {
    update_rsi_from_payload(handle, payload_text, payload_len);
  }
  event_enqueue(handle, type, session, task, step, seq, ts_ms, payload_text, payload_len);
}

static void handle_delta(bole_handle* handle, const bole_json_member* params, size_t count) {
  char session[256] = "";
  char channel[32] = "text";
  char attempt[128] = "";
  get_string(params, count, "sessionId", session, sizeof(session));
  get_string(params, count, "channel", channel, sizeof(channel));
  get_string(params, count, "attemptId", attempt, sizeof(attempt));
  uint64_t revision = get_u64(params, count, "revision");
  uint32_t task = get_u32(params, count, "turn");
  uint32_t step = get_u32(params, count, "step");

  const bole_json_member* text = bole_json_member_get(params, count, "text");
  if (text == NULL || text->type != BOLE_JSON_STRING || text->value_len <= 2) {
    return;
  }
  char* decoded = (char*)malloc(text->value_len + 1);
  if (decoded == NULL) {
    return;
  }
  size_t decoded_len = 0;
  if (bole_json_string(text, decoded, text->value_len + 1, &decoded_len) != 0) {
    free(decoded);
    return;
  }

  bole_buf payload;
  if (bole_buf_init(&payload, 256) != 0) {
    free(decoded);
    return;
  }
  bole_buf_append_cstr(&payload, "{\"v\":1,\"channel\":");
  bole_buf_append_json_string(&payload, channel);
  bole_buf_append_cstr(&payload, ",\"attemptId\":");
  bole_buf_append_json_string(&payload, attempt);
  bole_buf_appendf(&payload, ",\"revision\":%llu,\"text\":",
                   (unsigned long long)revision);
  bole_buf_append_json_string(&payload, decoded);
  bole_buf_append_cstr(&payload, "}");
  uint32_t type = strcmp(channel, "reasoning") == 0
      ? BOLE_EVENT_REASONING_DELTA : BOLE_EVENT_TEXT_DELTA;
  event_enqueue(handle, type, session, task, step, 0, 0, payload.data, payload.len);
  bole_buf_free(&payload);
  free(decoded);
}

static void handle_rsi_state(bole_handle* handle, const bole_json_member* params, size_t count) {
  char scope[32] = "default";
  char session[256] = "";
  char preset[BOLE_PRESET_MAX] = "";
  char source[32] = "config";
  get_string(params, count, "scope", scope, sizeof(scope));
  get_string(params, count, "sessionId", session, sizeof(session));
  get_string(params, count, "preset", preset, sizeof(preset));
  get_string(params, count, "source", source, sizeof(source));
  int enabled = get_bool(params, count, "enabled", 0);
  int composed = get_bool(params, count, "composed", 0);

  bole_buf payload;
  if (bole_buf_init(&payload, 256) != 0) {
    return;
  }
  bole_buf_append_cstr(&payload, "{\"v\":1,\"scope\":");
  bole_buf_append_json_string(&payload, scope);
  if (session[0] != '\0') {
    bole_buf_append_cstr(&payload, ",\"sessionId\":");
    bole_buf_append_json_string(&payload, session);
  }
  bole_buf_appendf(&payload, ",\"enabled\":%s,\"preset\":",
                   enabled != 0 ? "true" : "false");
  bole_buf_append_json_string(&payload, preset);
  bole_buf_appendf(&payload, ",\"composed\":%s,\"source\":",
                   composed != 0 ? "true" : "false");
  bole_buf_append_json_string(&payload, source);
  const bole_json_member* delivery = bole_json_member_get(params, count, "delivery");
  if (delivery != NULL && delivery->type == BOLE_JSON_OBJECT) {
    bole_buf_append_cstr(&payload, ",\"delivery\":");
    bole_buf_append(&payload, delivery->value, delivery->value_len);
  }
  bole_buf_append_cstr(&payload, "}");

  bole_mutex_lock(&handle->state_lock);
  handle->rsi.size = sizeof(bole_rsi_state);
  handle->rsi.enabled = enabled;
  handle->rsi.composed = composed;
  strncpy(handle->rsi.preset, preset, sizeof(handle->rsi.preset) - 1);
  handle->rsi.preset[sizeof(handle->rsi.preset) - 1] = '\0';
  bole_mutex_unlock(&handle->state_lock);

  event_enqueue(handle, BOLE_EVENT_RSI_STATE, session, 0, 0, 0, 0,
                payload.data, payload.len);
  bole_buf_free(&payload);
}

static void handle_initialize(bole_handle* handle, const char* result, size_t result_len,
                              int32_t code, const char* message) {
  if (code != 0) {
    bole_buf payload;
    if (bole_buf_init(&payload, 256) == 0) {
      bole_buf_appendf(&payload, "{\"v\":1,\"code\":%d,\"message\":", (int)code);
      bole_buf_append_json_string(&payload, message == NULL ? bole_error_text(code) : message);
      bole_buf_append_cstr(&payload, ",\"phase\":\"handshake\"}");
      event_enqueue(handle, BOLE_EVENT_RUNTIME_FAILED, "", 0, 0, 0, 0,
                    payload.data, payload.len);
      bole_buf_free(&payload);
    }
    set_failed(handle, code, message);
    return;
  }

  bole_json_member members[16];
  size_t count = 0;
  if (result == NULL
      || bole_json_parse_object(result, result_len, members, 16, &count) != 0) {
    set_failed(handle, BOLE_E_PROTOCOL_PARSE_FAILED, "invalid initialize result");
    return;
  }
  uint32_t version = get_u32(members, count, "sdkProtocolVersion");
  const bole_json_member* server_info = bole_json_member_get(members, count, "serverInfo");
  const bole_json_member* capabilities = bole_json_member_get(members, count, "capabilities");

  bole_buf payload;
  if (bole_buf_init(&payload, 1024) != 0) {
    set_failed(handle, BOLE_E_RUNTIME_START_FAILED, "out of memory");
    return;
  }
  bole_buf_append_cstr(&payload, "{\"v\":1,\"serverInfo\":");
  if (server_info != NULL && server_info->type == BOLE_JSON_OBJECT) {
    bole_buf_append(&payload, server_info->value, server_info->value_len);
  } else {
    bole_buf_append_cstr(&payload, "{}");
  }
  bole_buf_appendf(&payload, ",\"sdkProtocolVersion\":%u,\"capabilities\":", version);
  if (capabilities != NULL && capabilities->type == BOLE_JSON_OBJECT) {
    bole_buf_append(&payload, capabilities->value, capabilities->value_len);
  } else {
    bole_buf_append_cstr(&payload, "{}");
  }
  bole_buf_append_cstr(&payload, ",\"model\":{\"provider\":");
  bole_buf_append_json_string(&payload, handle->config.model_provider);
  bole_buf_append_cstr(&payload, ",\"model\":");
  bole_buf_append_json_string(&payload, handle->config.model_name);
  bole_buf_append_cstr(&payload, "}}");

  event_enqueue(handle, BOLE_EVENT_RUNTIME_READY, "", 0, 0, 0, 0,
                payload.data, payload.len);
  bole_buf_free(&payload);
  set_ready(handle, version);
  if (version > 1) {
    log_line(handle, "runtime sdkProtocolVersion is newer than the SDK",
             strlen("runtime sdkProtocolVersion is newer than the SDK"));
  }
}

static void handle_response(bole_handle* handle, const bole_json_member* top, size_t count) {
  uint64_t id = get_u64(top, count, "id");
  const bole_json_member* error = bole_json_member_get(top, count, "error");
  int32_t code = 0;
  char message[512] = "";
  char* result = NULL;
  size_t result_len = 0;

  if (error != NULL && error->type == BOLE_JSON_OBJECT) {
    bole_json_member error_members[8];
    size_t error_count = 0;
    int rpc_code = -32603;
    if (bole_json_parse_object(error->value, error->value_len,
                               error_members, 8, &error_count) == 0) {
      const bole_json_member* rpc_member = bole_json_member_get(error_members, error_count, "code");
      int64_t rpc_value = 0;
      if (rpc_member != NULL && bole_json_i64(rpc_member, &rpc_value) == 0) {
        rpc_code = (int)rpc_value;
      }
      get_string(error_members, error_count, "message", message, sizeof(message));
      bole_json_member data_members[8];
      size_t data_count = 0;
      char wire[64] = "";
      if (get_object(error_members, error_count, "data",
                     data_members, 8, &data_count) != NULL) {
        get_string(data_members, data_count, "code", wire, sizeof(wire));
      }
      code = bole_error_from_wire(wire[0] == '\0' ? NULL : wire, rpc_code);
    } else {
      code = BOLE_E_PROTOCOL_PARSE_FAILED;
    }
  } else {
    const bole_json_member* result_member = bole_json_member_get(top, count, "result");
    if (result_member == NULL) {
      code = BOLE_E_PROTOCOL_PARSE_FAILED;
      strcpy(message, "response has neither result nor error");
    } else {
      result = (char*)malloc(result_member->value_len + 1);
      if (result == NULL) {
        code = BOLE_E_RUNTIME_START_FAILED;
      } else {
        memcpy(result, result_member->value, result_member->value_len);
        result[result_member->value_len] = '\0';
        result_len = result_member->value_len;
      }
    }
  }

  if (id == handle->init_id && !handle->ready && !handle->failed) {
    handle_initialize(handle, result, result_len, code, message);
  } else {
    bole_rpc_complete(&handle->rpc, id, code, message, result, result_len);
  }
  free(result);
}

static void handle_notification(bole_handle* handle, const bole_json_member* top, size_t count) {
  char method[64] = "";
  if (get_string(top, count, "method", method, sizeof(method)) != 0) {
    return;
  }
  bole_json_member params[16];
  size_t params_count = 0;
  get_object(top, count, "params", params, 16, &params_count);

  if (strcmp(method, "bole.event") == 0) {
    handle_bole_event(handle, params, params_count);
  } else if (strcmp(method, "assistant.delta") == 0) {
    handle_delta(handle, params, params_count);
  } else if (strcmp(method, "rsi.state") == 0) {
    handle_rsi_state(handle, params, params_count);
  } else if (strcmp(method, "session.status") == 0) {
    char status[32] = "";
    get_string(params, params_count, "status", status, sizeof(status));
    bole_mutex_lock(&handle->state_lock);
    handle->running = strcmp(status, "running") == 0;
    bole_mutex_unlock(&handle->state_lock);
  }
}

static void on_line(void* user, const char* line, size_t len) {
  bole_handle* handle = (bole_handle*)user;
  bole_json_member top[16];
  size_t count = 0;
  if (bole_json_parse_object(line, len, top, 16, &count) != 0) {
    set_failed(handle, BOLE_E_PROTOCOL_PARSE_FAILED, "invalid JSON-RPC frame");
    handle->transport.stop = 1;
    return;
  }
  if (bole_json_member_get(top, count, "id") != NULL) {
    handle_response(handle, top, count);
  } else {
    handle_notification(handle, top, count);
  }
}

static void on_stderr_line(void* user, const char* line, size_t len) {
  bole_handle* handle = (bole_handle*)user;
  log_line(handle, line, len);
}

static void on_eof(void* user) {
  (void)user;
}

static void on_error(void* user, int32_t code, const char* message) {
  bole_handle* handle = (bole_handle*)user;
  bole_buf payload;
  if (bole_buf_init(&payload, 256) == 0) {
    bole_buf_appendf(&payload, "{\"v\":1,\"code\":%d,\"message\":", (int)code);
    bole_buf_append_json_string(&payload, message);
    bole_buf_append_cstr(&payload, ",\"phase\":\"transport\"}");
    event_enqueue(handle, BOLE_EVENT_RUNTIME_FAILED, "", 0, 0, 0, 0,
                  payload.data, payload.len);
    bole_buf_free(&payload);
  }
  set_failed(handle, code, message);
}

static void wait_thread_main(void* argument) {
  bole_handle* handle = (bole_handle*)argument;
  int exit_code = 0;
  bole_process_wait(&handle->process, &exit_code);

  bole_mutex_lock(&handle->state_lock);
  handle->exited = 1;
  int was_ready = handle->ready;
  int was_failed = handle->failed;
  handle->ready = 0;
  if (!was_failed) {
    handle->failed = 1;
    handle->failure_code = BOLE_E_RUNTIME_EXITED;
    strncpy(handle->failure_message, "runtime process exited",
            sizeof(handle->failure_message) - 1);
    strncpy(handle->last_message, "runtime process exited",
            sizeof(handle->last_message) - 1);
    handle->last_code = BOLE_E_RUNTIME_EXITED;
  }
  bole_cond_broadcast(&handle->state_cond);
  bole_mutex_unlock(&handle->state_lock);

  bole_rpc_close(&handle->rpc);

  if (!was_ready && !was_failed) {
    bole_buf payload;
    if (bole_buf_init(&payload, 256) == 0) {
      bole_buf_appendf(&payload, "{\"v\":1,\"code\":%d,\"message\":\"runtime process exited\",\"phase\":\"exit\"}",
                       (int)BOLE_E_RUNTIME_EXITED);
      event_enqueue(handle, BOLE_EVENT_RUNTIME_FAILED, "", 0, 0, 0, 0,
                    payload.data, payload.len);
      bole_buf_free(&payload);
    }
  } else if (was_ready) {
    bole_buf payload;
    if (bole_buf_init(&payload, 128) == 0) {
      bole_buf_appendf(&payload, "{\"v\":1,\"exitCode\":%d,\"hadActiveTasks\":false}",
                       exit_code);
      event_enqueue(handle, BOLE_EVENT_RUNTIME_EXITED, "", 0, 0, 0, 0,
                    payload.data, payload.len);
      bole_buf_free(&payload);
    }
  }
  handle->transport.stop = 1;
  return;
}

static int32_t ensure_ready(bole_handle* handle, int timeout_ms) {
  uint64_t deadline = bole_monotonic_ms() + (uint64_t)(timeout_ms > 0 ? timeout_ms : 0);
  int32_t result = 0;
  char message[512] = "";
  bole_mutex_lock(&handle->state_lock);
  for (;;) {
    if (handle->exited) {
      result = BOLE_E_RUNTIME_EXITED;
      break;
    }
    if (handle->failed) {
      result = handle->failure_code;
      strncpy(message, handle->failure_message, sizeof(message) - 1);
      break;
    }
    if (handle->ready) {
      result = BOLE_OK;
      break;
    }
    uint64_t now = bole_monotonic_ms();
    if (now >= deadline) {
      result = BOLE_E_REQUEST_TIMEOUT;
      break;
    }
    bole_cond_wait(&handle->state_cond, &handle->state_lock, (int)(deadline - now));
  }
  bole_mutex_unlock(&handle->state_lock);
  if (result != BOLE_OK && result != BOLE_E_REQUEST_TIMEOUT) {
    record_error(handle, result, message[0] == '\0' ? bole_error_text(result) : message);
  }
  return result;
}

static int32_t send_request(bole_handle* handle, const char* method, const char* params,
                            int timeout_ms, bole_response* response) {
  memset(response, 0, sizeof(*response));
  uint64_t id = bole_rpc_next_id(&handle->rpc);
  bole_pending* pending = bole_rpc_begin(&handle->rpc, id);
  if (pending == NULL) {
    return BOLE_E_RUNTIME_EXITED;
  }
  bole_buf line;
  if (bole_buf_init(&line, 512) != 0) {
    bole_rpc_end(&handle->rpc, pending);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  bole_buf_appendf(&line, "{\"jsonrpc\":\"2.0\",\"id\":%llu,\"method\":",
                   (unsigned long long)id);
  bole_buf_append_json_string(&line, method);
  if (params != NULL) {
    bole_buf_append_cstr(&line, ",\"params\":");
    bole_buf_append_cstr(&line, params);
  }
  bole_buf_append_cstr(&line, "}");
  int32_t code = BOLE_OK;
  if (bole_transport_send(&handle->transport, line.data, line.len) != 0) {
    code = BOLE_E_PIPE_FAILED;
    response->code = code;
  } else {
    int waited = bole_rpc_wait(pending, timeout_ms, response);
    if (waited == 1) {
      code = BOLE_E_REQUEST_TIMEOUT;
      response->code = code;
    } else {
      code = response->code;
    }
  }
  bole_buf_free(&line);
  bole_rpc_end(&handle->rpc, pending);
  if (code != BOLE_OK) {
    record_error(handle, code, response->message[0] == '\0'
        ? bole_error_text(code) : response->message);
  }
  return code;
}

static int32_t build_session_params(bole_handle* handle, const char* session_id,
                                    const bole_session_options* options,
                                    bole_buf* out) {
  if (session_id == NULL || session_id[0] == '\0' || out == NULL) {
    return BOLE_E_INVALID_ARGUMENT;
  }
  if (options != NULL) {
    if (options->size < sizeof(bole_session_options)) {
      return BOLE_E_INVALID_ARGUMENT;
    }
    if (options->cwd != NULL && !is_absolute_path(options->cwd)) {
      return BOLE_E_INVALID_ARGUMENT;
    }
  }
  if (bole_buf_init(out, 256) != 0) {
    return BOLE_E_RUNTIME_START_FAILED;
  }
  bole_buf_append_cstr(out, "{\"sessionId\":");
  bole_buf_append_json_string(out, session_id);
  if (options != NULL && options->cwd != NULL) {
    bole_buf_append_cstr(out, ",\"cwd\":");
    bole_buf_append_json_string(out, options->cwd);
  }
  if (options != NULL && options->preset != NULL) {
    bole_buf_append_cstr(out, ",\"preset\":");
    bole_buf_append_json_string(out, options->preset);
  }
  bole_buf_append_cstr(out, "}");
  (void)handle;
  return BOLE_OK;
}

static bole_session* find_session(bole_handle* handle, const char* id) {
  for (bole_session* session = handle->sessions; session != NULL; session = session->next) {
    if (strcmp(session->id, id) == 0) {
      return session;
    }
  }
  return NULL;
}

static int32_t add_session(bole_handle* handle, const char* id, bole_session** out) {
  bole_mutex_lock(&handle->state_lock);
  if (handle->session_count >= BOLE_MAX_SESSIONS) {
    bole_mutex_unlock(&handle->state_lock);
    return BOLE_E_INVALID_CONFIG;
  }
  bole_session* session = (bole_session*)calloc(1, sizeof(bole_session));
  if (session == NULL) {
    bole_mutex_unlock(&handle->state_lock);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  session->id = bole_strdup(id);
  if (session->id == NULL) {
    free(session);
    bole_mutex_unlock(&handle->state_lock);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  session->handle = handle;
  session->next = handle->sessions;
  handle->sessions = session;
  handle->session_count++;
  bole_mutex_unlock(&handle->state_lock);
  *out = session;
  return BOLE_OK;
}

static int32_t check_session(bole_session* session) {
  if (session == NULL || session->handle == NULL || session->id == NULL) {
    return BOLE_E_INVALID_ARGUMENT;
  }
  return BOLE_OK;
}

static char** build_environment(bole_handle* handle, char** storage, size_t* storage_count) {
#ifdef _WIN32
  (void)handle;
  *storage = NULL;
  *storage_count = 0;
  return NULL;
#else
  const char* sandbox = handle->config.sandbox_mode == BOLE_SANDBOX_DANGER_FULL_ACCESS
      ? "danger-full-access" : "workspace-write";
  char* home = NULL;
  char* sandbox_entry = NULL;
  char* telemetry = NULL;
  size_t count = 0;
  while (environ[count] != NULL) {
    count++;
  }
  char** env = (char**)calloc(count + 4, sizeof(char*));
  if (env == NULL) {
    return NULL;
  }
  size_t out = 0;
  for (size_t i = 0; i < count; i++) {
    if (strncmp(environ[i], "DSH_HOME=", 9) == 0
        || strncmp(environ[i], "DSH_PERMISSION_MODE=", 20) == 0
        || strncmp(environ[i], "DSH_TELEMETRY_DISABLED=", 24) == 0) {
      continue;
    }
    env[out++] = environ[i];
  }
  size_t home_len = strlen(handle->config.dsh_home) + 10;
  home = (char*)malloc(home_len);
  sandbox_entry = (char*)malloc(strlen(sandbox) + 22);
  telemetry = (char*)malloc(25);
  if (home == NULL || sandbox_entry == NULL || telemetry == NULL) {
    free(home);
    free(sandbox_entry);
    free(telemetry);
    free(env);
    return NULL;
  }
  snprintf(home, home_len, "DSH_HOME=%s", handle->config.dsh_home);
  snprintf(sandbox_entry, strlen(sandbox) + 22, "DSH_PERMISSION_MODE=%s", sandbox);
  strcpy(telemetry, "DSH_TELEMETRY_DISABLED=1");
  env[out++] = home;
  env[out++] = sandbox_entry;
  env[out++] = telemetry;
  env[out] = NULL;
  storage[0] = home;
  storage[1] = sandbox_entry;
  storage[2] = telemetry;
  *storage_count = 3;
  return env;
#endif
}

static void free_environment(char** env, char** storage, size_t storage_count) {
  if (storage != NULL) {
    for (size_t i = 0; i < storage_count; i++) {
      free(storage[i]);
    }
  }
  free(env);
}

static void free_handle(bole_handle* handle) {
  if (handle == NULL) {
    return;
  }
  bole_session* session = handle->sessions;
  while (session != NULL) {
    bole_session* next = session->next;
    free(session->id);
    free(session);
    session = next;
  }
  config_free(&handle->config);
  if (handle->log != NULL) {
    fclose(handle->log);
  }
  bole_mutex_destroy(&handle->state_lock);
  bole_cond_destroy(&handle->state_cond);
  bole_mutex_destroy(&handle->event_lock);
  bole_cond_destroy(&handle->event_cond);
  bole_mutex_destroy(&handle->log_lock);
  bole_rpc_destroy(&handle->rpc);
  free(handle);
}

int32_t BOLE_CALL bole_init(const bole_config* config, bole_handle** out_handle) {
  if (out_handle == NULL) {
    return BOLE_E_INVALID_ARGUMENT;
  }
  *out_handle = NULL;
  bole_config copied;
  int32_t code = config_copy(&copied, config);
  if (code != BOLE_OK) {
    return code;
  }

  bole_handle* handle = (bole_handle*)calloc(1, sizeof(bole_handle));
  if (handle == NULL) {
    config_free(&copied);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  handle->config = copied;
  handle->event_cb = copied.event_cb;
  handle->user_data = copied.user_data;
  handle->rsi.size = sizeof(bole_rsi_state);
  handle->rsi.enabled = copied.rsi_enabled;
  strcpy(handle->rsi.preset, copied.rsi_enabled ? "rsi-infer" : "standard");
  handle->rsi.composed = 0;

  if (bole_mutex_init(&handle->state_lock) != 0
      || bole_cond_init(&handle->state_cond) != 0
      || bole_mutex_init(&handle->event_lock) != 0
      || bole_cond_init(&handle->event_cond) != 0
      || bole_mutex_init(&handle->log_lock) != 0
      || bole_rpc_init(&handle->rpc) != 0) {
    free_handle(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  if (copied.log_path != NULL) {
    handle->log = fopen(copied.log_path, "ab");
  }

  char* node_path = path_join(copied.runtime_dir, "node.exe");
  char* entry_path = path_join(copied.runtime_dir,
                               "node_modules/@deepseek-ai/dsh/lib/bin.js");
  char* patch_path = path_join(copied.runtime_dir, "cordis.patch.yml");
  if (node_path == NULL || entry_path == NULL || patch_path == NULL
      || !bole_path_exists(node_path) || !bole_path_exists(entry_path)
      || !bole_path_exists(patch_path)) {
    free(node_path);
    free(entry_path);
    free(patch_path);
    free_handle(handle);
    return BOLE_E_RUNTIME_DIR_NOT_FOUND;
  }
  if (bole_mkdir_p(copied.dsh_home) != 0) {
    free(node_path);
    free(entry_path);
    free(patch_path);
    free_handle(handle);
    return BOLE_E_INVALID_CONFIG;
  }

  char* argv[8];
  argv[0] = node_path;
  argv[1] = entry_path;
  argv[2] = (char*)"--profile";
  argv[3] = (char*)"sdk";
  argv[4] = (char*)"--patch";
  argv[5] = patch_path;
  argv[6] = NULL;

  char* env_storage[3] = {NULL, NULL, NULL};
  size_t env_storage_count = 0;
  char** env = build_environment(handle, env_storage, &env_storage_count);
  if (env == NULL) {
    free(node_path);
    free(entry_path);
    free(patch_path);
    free_handle(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }

  int spawn = bole_process_spawn(&handle->process, node_path, argv, copied.cwd, env);
  free_environment(env, env_storage, env_storage_count);
  free(node_path);
  free(entry_path);
  free(patch_path);
  if (spawn != 0) {
    free_handle(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }

  if (bole_transport_start(&handle->transport, &handle->process,
                           copied.max_line_bytes, handle) != 0) {
    bole_process_terminate(&handle->process);
    bole_process_close(&handle->process);
    free_handle(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  handle->transport.on_line = on_line;
  handle->transport.on_stderr_line = on_stderr_line;
  handle->transport.on_eof = on_eof;
  handle->transport.on_error = on_error;

  if (bole_thread_start(&handle->wait_thread, wait_thread_main, handle) != 0) {
    bole_transport_stop(&handle->transport);
    bole_process_terminate(&handle->process);
    bole_process_close(&handle->process);
    free_handle(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  handle->wait_thread_started = 1;

  handle->init_id = bole_rpc_next_id(&handle->rpc);
  bole_buf initialize;
  if (bole_buf_init(&initialize, 512) != 0) {
    bole_transport_stop(&handle->transport);
    bole_process_terminate(&handle->process);
    bole_thread_join(&handle->wait_thread);
    bole_process_close(&handle->process);
    free_handle(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  bole_buf_appendf(&initialize,
                   "{\"jsonrpc\":\"2.0\",\"id\":%llu,\"method\":\"initialize\",\"params\":{\"cwd\":",
                   (unsigned long long)handle->init_id);
  bole_buf_append_json_string(&initialize, copied.cwd);
  bole_buf_append_cstr(&initialize, ",\"provider\":");
  bole_buf_append_json_string(&initialize, copied.model_provider);
  bole_buf_append_cstr(&initialize, ",\"model\":");
  bole_buf_append_json_string(&initialize, copied.model_name);
  if (copied.reasoning_effort != NULL) {
    bole_buf_append_cstr(&initialize, ",\"reasoningEffort\":");
    bole_buf_append_json_string(&initialize, copied.reasoning_effort);
  }
  if (copied.max_tokens > 0) {
    bole_buf_appendf(&initialize, ",\"maxTokens\":%d", copied.max_tokens);
  }
  bole_buf_appendf(&initialize, ",\"rsiEnabled\":%s}}",
                   copied.rsi_enabled ? "true" : "false");

  if (bole_transport_send(&handle->transport, initialize.data, initialize.len) != 0) {
    bole_buf_free(&initialize);
    bole_transport_stop(&handle->transport);
    bole_process_terminate(&handle->process);
    bole_thread_join(&handle->wait_thread);
    bole_process_close(&handle->process);
    free_handle(handle);
    return BOLE_E_PIPE_FAILED;
  }
  bole_buf_free(&initialize);

  if (bole_thread_start(&handle->event_thread, event_thread_main, handle) != 0) {
    bole_transport_stop(&handle->transport);
    bole_process_terminate(&handle->process);
    bole_thread_join(&handle->wait_thread);
    bole_process_close(&handle->process);
    free_handle(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  handle->event_thread_started = 1;
  *out_handle = handle;
  return BOLE_OK;
}

int32_t BOLE_CALL bole_wait_ready(bole_handle* handle, uint32_t timeout_ms) {
  if (handle == NULL) {
    return BOLE_E_INVALID_ARGUMENT;
  }
  return ensure_ready(handle, (int)timeout_ms);
}

int32_t BOLE_CALL bole_set_event_callback(bole_handle* handle, bole_event_cb callback,
                                          void* user_data) {
  if (handle == NULL || callback == NULL) {
    return handle == NULL ? BOLE_E_INVALID_ARGUMENT : BOLE_E_EVENT_CALLBACK_REQUIRED;
  }
  bole_mutex_lock(&handle->state_lock);
  handle->event_cb = callback;
  handle->user_data = user_data;
  bole_mutex_unlock(&handle->state_lock);
  return BOLE_OK;
}

int32_t BOLE_CALL bole_session_create(bole_handle* handle, const char* session_id,
                                      const bole_session_options* options,
                                      bole_session** out_session) {
  if (handle == NULL || session_id == NULL || out_session == NULL) {
    return BOLE_E_INVALID_ARGUMENT;
  }
  *out_session = NULL;
  int32_t code = ensure_ready(handle, BOLE_CONTROL_TIMEOUT_MS);
  if (code != BOLE_OK) {
    return code;
  }
  bole_mutex_lock(&handle->state_lock);
  bole_session* existing = find_session(handle, session_id);
  bole_mutex_unlock(&handle->state_lock);
  if (existing != NULL) {
    record_error(handle, BOLE_E_SESSION_EXISTS, "session already exists");
    return BOLE_E_SESSION_EXISTS;
  }
  bole_buf params;
  code = build_session_params(handle, session_id, options, &params);
  if (code != BOLE_OK) {
    record_error(handle, code, bole_error_text(code));
    return code;
  }
  bole_response response;
  code = send_request(handle, "session/create", params.data, BOLE_CONTROL_TIMEOUT_MS, &response);
  bole_buf_free(&params);
  bole_response_free(&response);
  if (code != BOLE_OK) {
    return code;
  }
  return add_session(handle, session_id, out_session);
}

int32_t BOLE_CALL bole_session_resume(bole_handle* handle, const char* session_id,
                                      bole_session** out_session) {
  if (handle == NULL || session_id == NULL || out_session == NULL) {
    return BOLE_E_INVALID_ARGUMENT;
  }
  *out_session = NULL;
  int32_t code = ensure_ready(handle, BOLE_CONTROL_TIMEOUT_MS);
  if (code != BOLE_OK) {
    return code;
  }
  bole_buf params;
  if (bole_buf_init(&params, 128) != 0) {
    return BOLE_E_RUNTIME_START_FAILED;
  }
  bole_buf_append_cstr(&params, "{\"sessionId\":");
  bole_buf_append_json_string(&params, session_id);
  bole_buf_append_cstr(&params, "}");
  bole_response response;
  code = send_request(handle, "session/resume", params.data, BOLE_RESUME_TIMEOUT_MS, &response);
  bole_buf_free(&params);
  bole_response_free(&response);
  if (code != BOLE_OK) {
    return code;
  }
  return add_session(handle, session_id, out_session);
}

int32_t BOLE_CALL bole_session_set_preset(bole_session* session, const char* preset_id) {
  int32_t code = check_session(session);
  if (code != BOLE_OK) {
    return code;
  }
  if (preset_id == NULL || preset_id[0] == '\0') {
    return BOLE_E_INVALID_ARGUMENT;
  }
  bole_handle* handle = session->handle;
  code = ensure_ready(handle, BOLE_CONTROL_TIMEOUT_MS);
  if (code != BOLE_OK) {
    return code;
  }
  bole_buf params;
  if (bole_buf_init(&params, 128) != 0) {
    return BOLE_E_RUNTIME_START_FAILED;
  }
  bole_buf_append_cstr(&params, "{\"sessionId\":");
  bole_buf_append_json_string(&params, session->id);
  bole_buf_append_cstr(&params, ",\"preset\":");
  bole_buf_append_json_string(&params, preset_id);
  bole_buf_append_cstr(&params, "}");
  bole_response response;
  code = send_request(handle, "session/setPreset", params.data,
                      BOLE_CONTROL_TIMEOUT_MS, &response);
  bole_buf_free(&params);
  bole_response_free(&response);
  return code;
}

static int32_t submit_params(bole_handle* handle, bole_session* session,
                             const char* blocks_json, bole_buf* params) {
  (void)handle;
  if (bole_buf_init(params, 256) != 0) {
    return BOLE_E_RUNTIME_START_FAILED;
  }
  bole_buf_append_cstr(params, "{\"sessionId\":");
  bole_buf_append_json_string(params, session->id);
  bole_buf_append_cstr(params, ",\"contentBlocks\":");
  bole_buf_append_cstr(params, blocks_json);
  bole_buf_append_cstr(params, "}");
  return BOLE_OK;
}

int32_t BOLE_CALL bole_submit(bole_session* session, const char* utf8_text) {
  int32_t code = check_session(session);
  if (code != BOLE_OK) {
    return code;
  }
  if (utf8_text == NULL || utf8_text[0] == '\0') {
    return BOLE_E_INVALID_ARGUMENT;
  }
  bole_handle* handle = session->handle;
  code = ensure_ready(handle, BOLE_CONTROL_TIMEOUT_MS);
  if (code != BOLE_OK) {
    return code;
  }
  bole_buf blocks;
  if (bole_buf_init(&blocks, strlen(utf8_text) + 64) != 0) {
    return BOLE_E_RUNTIME_START_FAILED;
  }
  bole_buf_append_cstr(&blocks, "[{\"type\":\"text\",\"text\":");
  bole_buf_append_json_string(&blocks, utf8_text);
  bole_buf_append_cstr(&blocks, "}]");
  bole_buf params;
  code = submit_params(handle, session, blocks.data, &params);
  bole_buf_free(&blocks);
  if (code != BOLE_OK) {
    return code;
  }
  bole_response response;
  code = send_request(handle, "session/prompt", params.data, BOLE_CONTROL_TIMEOUT_MS, &response);
  bole_buf_free(&params);
  bole_response_free(&response);
  return code;
}

int32_t BOLE_CALL bole_submit_blocks(bole_session* session, const char* content_blocks_json) {
  int32_t code = check_session(session);
  if (code != BOLE_OK) {
    return code;
  }
  if (content_blocks_json == NULL || content_blocks_json[0] != '[') {
    return BOLE_E_INVALID_ARGUMENT;
  }
  if (bole_json_validate(content_blocks_json, strlen(content_blocks_json)) != 0) {
    return BOLE_E_INVALID_ARGUMENT;
  }
  bole_handle* handle = session->handle;
  code = ensure_ready(handle, BOLE_CONTROL_TIMEOUT_MS);
  if (code != BOLE_OK) {
    return code;
  }
  bole_buf params;
  code = submit_params(handle, session, content_blocks_json, &params);
  if (code != BOLE_OK) {
    return code;
  }
  bole_response response;
  code = send_request(handle, "session/prompt", params.data, BOLE_CONTROL_TIMEOUT_MS, &response);
  bole_buf_free(&params);
  bole_response_free(&response);
  return code;
}

int32_t BOLE_CALL bole_cancel(bole_session* session) {
  int32_t code = check_session(session);
  if (code != BOLE_OK) {
    return code;
  }
  bole_handle* handle = session->handle;
  code = ensure_ready(handle, BOLE_CONTROL_TIMEOUT_MS);
  if (code != BOLE_OK) {
    return code;
  }
  bole_buf params;
  if (bole_buf_init(&params, 128) != 0) {
    return BOLE_E_RUNTIME_START_FAILED;
  }
  bole_buf_append_cstr(&params, "{\"sessionId\":");
  bole_buf_append_json_string(&params, session->id);
  bole_buf_append_cstr(&params, "}");
  bole_response response;
  code = send_request(handle, "session/cancel", params.data, BOLE_CONTROL_TIMEOUT_MS, &response);
  bole_buf_free(&params);
  bole_response_free(&response);
  return code;
}

int32_t BOLE_CALL bole_rsi_set(bole_handle* handle, int32_t enabled) {
  if (handle == NULL || (enabled != 0 && enabled != 1)) {
    return BOLE_E_INVALID_ARGUMENT;
  }
  int32_t code = ensure_ready(handle, BOLE_CONTROL_TIMEOUT_MS);
  if (code != BOLE_OK) {
    return code;
  }
  bole_response response;
  code = send_request(handle, "rsi.set", enabled ? "{\"enabled\":true}" : "{\"enabled\":false}",
                      BOLE_CONTROL_TIMEOUT_MS, &response);
  if (code == BOLE_OK && response.result != NULL) {
    bole_json_member members[8];
    size_t count = 0;
    if (bole_json_parse_object(response.result, response.result_len, members, 8, &count) == 0) {
      int actual = get_bool(members, count, "enabled", enabled);
      char preset[BOLE_PRESET_MAX] = "";
      get_string(members, count, "preset", preset, sizeof(preset));
      bole_mutex_lock(&handle->state_lock);
      handle->rsi.size = sizeof(bole_rsi_state);
      handle->rsi.enabled = actual;
      handle->rsi.composed = 1;
      strncpy(handle->rsi.preset, preset, sizeof(handle->rsi.preset) - 1);
      handle->rsi.preset[sizeof(handle->rsi.preset) - 1] = '\0';
      bole_mutex_unlock(&handle->state_lock);
    }
  }
  bole_response_free(&response);
  return code;
}

int32_t BOLE_CALL bole_rsi_get(bole_handle* handle, bole_rsi_state* out_state) {
  if (handle == NULL || out_state == NULL || out_state->size < sizeof(bole_rsi_state)) {
    return BOLE_E_INVALID_ARGUMENT;
  }
  bole_mutex_lock(&handle->state_lock);
  bole_rsi_state state = handle->rsi;
  bole_mutex_unlock(&handle->state_lock);
  state.size = sizeof(bole_rsi_state);
  *out_state = state;
  return BOLE_OK;
}

int32_t BOLE_CALL bole_trace_export(bole_handle* handle, const char* session_id,
                                    uint32_t turn, const char* path,
                                    uint32_t* out_event_count) {
  if (handle == NULL || session_id == NULL || path == NULL) {
    return BOLE_E_INVALID_ARGUMENT;
  }
  int32_t code = ensure_ready(handle, BOLE_CONTROL_TIMEOUT_MS);
  if (code != BOLE_OK) {
    return code;
  }
  bole_buf params;
  if (bole_buf_init(&params, 256) != 0) {
    return BOLE_E_RUNTIME_START_FAILED;
  }
  bole_buf_append_cstr(&params, "{\"sessionId\":");
  bole_buf_append_json_string(&params, session_id);
  bole_buf_appendf(&params, ",\"turn\":%u,\"path\":", turn);
  bole_buf_append_json_string(&params, path);
  bole_buf_append_cstr(&params, "}");
  bole_response response;
  code = send_request(handle, "session/trace", params.data, BOLE_TRACE_TIMEOUT_MS, &response);
  bole_buf_free(&params);
  if (code == BOLE_OK && out_event_count != NULL && response.result != NULL) {
    bole_json_member members[8];
    size_t count = 0;
    if (bole_json_parse_object(response.result, response.result_len,
                               members, 8, &count) == 0) {
      *out_event_count = get_u32(members, count, "eventCount");
    }
  }
  bole_response_free(&response);
  return code;
}

int32_t BOLE_CALL bole_last_error(bole_handle* handle, int32_t* out_code,
                                  char* buffer, size_t buffer_len) {
  if (handle == NULL || (buffer == NULL && buffer_len > 0)) {
    return BOLE_E_INVALID_ARGUMENT;
  }
  bole_mutex_lock(&handle->state_lock);
  if (out_code != NULL) {
    *out_code = handle->last_code;
  }
  if (buffer != NULL && buffer_len > 0) {
    strncpy(buffer, handle->last_message, buffer_len - 1);
    buffer[buffer_len - 1] = '\0';
  }
  bole_mutex_unlock(&handle->state_lock);
  return BOLE_OK;
}

int32_t BOLE_CALL bole_release(bole_handle* handle) {
  if (handle == NULL) {
    return BOLE_E_INVALID_ARGUMENT;
  }
  bole_mutex_lock(&handle->state_lock);
  if (handle->stopping) {
    bole_mutex_unlock(&handle->state_lock);
    return BOLE_OK;
  }
  handle->stopping = 1;
  bole_cond_broadcast(&handle->state_cond);
  bole_mutex_unlock(&handle->state_lock);

  uint64_t id = bole_rpc_next_id(&handle->rpc);
  bole_buf shutdown;
  if (bole_buf_init(&shutdown, 128) == 0) {
    bole_buf_appendf(&shutdown, "{\"jsonrpc\":\"2.0\",\"id\":%llu,\"method\":\"shutdown\"}",
                     (unsigned long long)id);
    bole_transport_send(&handle->transport, shutdown.data, shutdown.len);
    bole_buf_free(&shutdown);
  }

  bole_mutex_lock(&handle->process.lock);
  uint64_t deadline = bole_monotonic_ms() + 2000;
  while (!handle->process.exited) {
    uint64_t now = bole_monotonic_ms();
    if (now >= deadline) {
      break;
    }
    bole_cond_wait(&handle->process.cond, &handle->process.lock, (int)(deadline - now));
  }
  bole_mutex_unlock(&handle->process.lock);
  bole_process_terminate(&handle->process);

  bole_transport_stop(&handle->transport);
  bole_rpc_close(&handle->rpc);
  event_thread_stop(handle);
  if (handle->wait_thread_started) {
    bole_thread_join(&handle->wait_thread);
    handle->wait_thread_started = 0;
  }
  bole_process_close(&handle->process);
  free_handle(handle);
  return BOLE_OK;
}
