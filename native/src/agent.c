/**
 * @file agent.c
 * @brief Public C ABI implementation: runtime process, JSON-RPC control plane,
 * bounded event queue, sessions, RSI projection, trace export, and release.
 *
 * Contracts: docs/bole-sdk/02-protocol.md, 03-events.md, 04-c-abi.md.
 * The wire is newline-delimited JSON-RPC over the runtime's stdio pipes. Only
 * this file interprets the SDK event envelope; it never parses harness event
 * vocabulary beyond the `bole.event` fields defined by 03-events.md.
 */

#include "bole_agent.h"
#include "error.h"
#include "json.h"
#include "platform.h"
#include "rpc.h"
#include "transport.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#  define BOLE_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#  define BOLE_THREAD_LOCAL __thread
#else
#  define BOLE_THREAD_LOCAL
#endif

/** Set on the event thread while a callback is running (04 §5.2). */
static BOLE_THREAD_LOCAL int bole_in_callback = 0;

#define BOLE_DEFAULT_MAX_LINE (64u * 1024u * 1024u)
#define BOLE_EVENT_QUEUE_MAX 4096u
#define BOLE_EVENT_BYTES_MAX (16u * 1024u * 1024u)
#define BOLE_INIT_TIMEOUT_MS 30000
#define BOLE_CONTROL_TIMEOUT_MS 5000
#define BOLE_RESUME_TIMEOUT_MS 10000
#define BOLE_TRACE_TIMEOUT_MS 30000
#define BOLE_RELEASE_GRACE_MS 2000

#define BOLE_RES_STATE_LOCK 0x01u
#define BOLE_RES_STATE_COND 0x02u
#define BOLE_RES_EVENT_LOCK 0x04u
#define BOLE_RES_EVENT_COND 0x08u
#define BOLE_RES_LOG_LOCK 0x10u
#define BOLE_RES_RPC 0x20u

#define BOLE_CONFIG_FIELD_OK(declared, field) \
  ((declared) >= (uint32_t)(offsetof(bole_config, field) + \
                            sizeof(((bole_config*)0)->field)))
#define BOLE_OPTIONS_FIELD_OK(declared, field) \
  ((declared) >= (uint32_t)(offsetof(bole_session_options, field) + \
                            sizeof(((bole_session_options*)0)->field)))

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
  unsigned resources;
  int rpc_ready;

  bole_thread event_thread;
  bole_thread wait_thread;
  bole_thread reaper_thread;
  int event_thread_started;
  int wait_thread_started;
  uintptr_t event_thread_id;

  bole_mutex state_lock;
  bole_cond state_cond;
  int ready;
  int failed;
  int exited;
  volatile int stopping;
  int callback_active;
  int32_t failure_code;
  char failure_message[512];
  uint32_t protocol_version;
  uint64_t init_id;
  int running;
  int first_line;
  uint32_t refs;

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
  uint64_t last_ts;

  bole_session* sessions;
  size_t session_count;

  bole_rsi_state rsi;
  int rsi_seen;

  int32_t last_code;
  char last_message[512];

  FILE* log;
  bole_mutex log_lock;
};

static void reap_and_free(bole_handle* handle);
static void fail_handle(bole_handle* handle, int32_t code, const char* message,
                        const char* phase, int terminate);

/* ── Small helpers ──────────────────────────────────────────────── */

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
      && path[1] == ':' && (path[2] == '\\' || path[2] == '/');
#else
  return path[0] == '/';
#endif
}

static char path_separator(void) {
#ifdef _WIN32
  return '\\';
#else
  return '/';
#endif
}

static char* path_join(const char* base, const char* name) {
  if (base == NULL || name == NULL) {
    return NULL;
  }
  size_t base_len = strlen(base);
  size_t name_len = strlen(name);
  int need_sep = base_len > 0 && base[base_len - 1] != '/' && base[base_len - 1] != '\\';
  char* result = (char*)malloc(base_len + (need_sep ? 1u : 0u) + name_len + 1);
  if (result == NULL) {
    return NULL;
  }
  memcpy(result, base, base_len);
  size_t offset = base_len;
  if (need_sep) {
    result[offset++] = path_separator();
  }
  for (size_t i = 0; i < name_len; i++) {
    char c = name[i];
    result[offset++] = (c == '/' || c == '\\') ? path_separator() : c;
  }
  result[offset] = '\0';
  return result;
}

static int32_t clamp_timeout(uint32_t timeout_ms) {
  if (timeout_ms > 0x7fffffffu) {
    return 0x7fffffff;
  }
  return (int32_t)timeout_ms;
}

/* ── Configuration copy (size-aware) ────────────────────────────── */

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
  if (source == NULL || source->size < sizeof(uint32_t)) {
    return BOLE_E_INVALID_CONFIG;
  }
  uint32_t declared = source->size;
  memset(target, 0, sizeof(*target));
  target->size = sizeof(bole_config);

  if (BOLE_CONFIG_FIELD_OK(declared, runtime_dir)) {
    target->runtime_dir = bole_strdup(source->runtime_dir);
  }
  if (BOLE_CONFIG_FIELD_OK(declared, dsh_home)) {
    target->dsh_home = bole_strdup(source->dsh_home);
  }
  if (BOLE_CONFIG_FIELD_OK(declared, cwd)) {
    target->cwd = bole_strdup(source->cwd);
  }
  if (BOLE_CONFIG_FIELD_OK(declared, model_provider)) {
    target->model_provider = bole_strdup(source->model_provider);
  }
  if (BOLE_CONFIG_FIELD_OK(declared, model_name)) {
    target->model_name = bole_strdup(source->model_name);
  }
  if (BOLE_CONFIG_FIELD_OK(declared, reasoning_effort)) {
    target->reasoning_effort = bole_strdup(source->reasoning_effort);
  }
  if (BOLE_CONFIG_FIELD_OK(declared, max_tokens)) {
    target->max_tokens = source->max_tokens;
  }
  if (BOLE_CONFIG_FIELD_OK(declared, rsi_enabled)) {
    target->rsi_enabled = source->rsi_enabled;
  }
  if (BOLE_CONFIG_FIELD_OK(declared, sandbox_mode)) {
    target->sandbox_mode = source->sandbox_mode;
  }
  if (BOLE_CONFIG_FIELD_OK(declared, max_line_bytes)) {
    target->max_line_bytes = source->max_line_bytes;
  }
  if (BOLE_CONFIG_FIELD_OK(declared, event_cb)) {
    target->event_cb = source->event_cb;
  }
  if (BOLE_CONFIG_FIELD_OK(declared, user_data)) {
    target->user_data = source->user_data;
  }
  if (BOLE_CONFIG_FIELD_OK(declared, log_path)) {
    target->log_path = bole_strdup(source->log_path);
  }

  if (target->runtime_dir == NULL || target->runtime_dir[0] == '\0'
      || target->dsh_home == NULL || target->dsh_home[0] == '\0'
      || target->model_provider == NULL || target->model_provider[0] == '\0'
      || target->model_name == NULL || target->model_name[0] == '\0') {
    config_free(target);
    return BOLE_E_INVALID_CONFIG;
  }
  if (target->event_cb == NULL) {
    config_free(target);
    return BOLE_E_EVENT_CALLBACK_REQUIRED;
  }
  if (target->rsi_enabled != 0 && target->rsi_enabled != 1) {
    config_free(target);
    return BOLE_E_INVALID_CONFIG;
  }
  if (target->sandbox_mode < BOLE_SANDBOX_DEFAULT
      || target->sandbox_mode > BOLE_SANDBOX_DANGER_FULL_ACCESS) {
    config_free(target);
    return BOLE_E_INVALID_CONFIG;
  }
  if (target->max_tokens < 0) {
    config_free(target);
    return BOLE_E_INVALID_CONFIG;
  }
  if (target->cwd == NULL) {
    target->cwd = bole_getcwd();
    if (target->cwd == NULL) {
      config_free(target);
      return BOLE_E_INVALID_CONFIG;
    }
  } else if (!is_absolute_path(target->cwd)) {
    config_free(target);
    return BOLE_E_INVALID_CONFIG;
  }
  if (target->max_line_bytes == 0) {
    target->max_line_bytes = BOLE_DEFAULT_MAX_LINE;
  }
  return BOLE_OK;
}

/* ── Logging and last error ─────────────────────────────────────── */

static void log_line(bole_handle* handle, const char* text, size_t length) {
  if (handle->log == NULL || text == NULL) {
    return;
  }
  bole_mutex_lock(&handle->log_lock);
  fwrite("[bole] ", 1, 7, handle->log);
  fwrite(text, 1, length, handle->log);
  fputc('\n', handle->log);
  fflush(handle->log);
  bole_mutex_unlock(&handle->log_lock);
}

static void record_error(bole_handle* handle, int32_t code, const char* message) {
  const char* text = message == NULL ? bole_error_text(code) : message;
  bole_mutex_lock(&handle->state_lock);
  handle->last_code = code;
  strncpy(handle->last_message, text, sizeof(handle->last_message) - 1);
  handle->last_message[sizeof(handle->last_message) - 1] = '\0';
  bole_mutex_unlock(&handle->state_lock);
  log_line(handle, text, strlen(text));
}

/* ── Event queue ────────────────────────────────────────────────── */

static void event_node_free(bole_event_node* node) {
  if (node == NULL) {
    return;
  }
  free(node->session_id);
  free(node->payload);
  free(node);
}

static bole_event_node* event_find_delta_locked(bole_handle* handle) {
  bole_event_node** link = &handle->event_head;
  while (*link != NULL) {
    bole_event_node* node = *link;
    if (node->event.type == BOLE_EVENT_TEXT_DELTA
        || node->event.type == BOLE_EVENT_REASONING_DELTA) {
      return node;
    }
    link = &node->next;
  }
  return NULL;
}

static void event_remove_locked(bole_handle* handle, bole_event_node* victim) {
  bole_event_node** link = &handle->event_head;
  bole_event_node* previous = NULL;
  while (*link != NULL) {
    if (*link == victim) {
      *link = victim->next;
      if (handle->event_tail == victim) {
        handle->event_tail = previous;
      }
      break;
    }
    previous = *link;
    link = &(*link)->next;
  }
  handle->event_count--;
  handle->event_bytes -= victim->bytes;
}

static void event_queue_clear(bole_handle* handle) {
  bole_mutex_lock(&handle->event_lock);
  bole_event_node* node = handle->event_head;
  handle->event_head = NULL;
  handle->event_tail = NULL;
  handle->event_count = 0;
  handle->event_bytes = 0;
  bole_mutex_unlock(&handle->event_lock);
  while (node != NULL) {
    bole_event_node* next = node->next;
    event_node_free(node);
    node = next;
  }
}

/**
 * Queue one event. @p critical bypasses the byte/item budget for the three
 * runtime events (READY/FAILED/EXITED), which must survive a full queue.
 * Deltas are shed first; a structural event that cannot be queued is a
 * protocol failure (2007), never a silent drop (03 §5).
 */
static int event_enqueue(bole_handle* handle, uint32_t type, const char* session_id,
                         uint32_t task, uint32_t step, uint64_t seq, int64_t ts_ms,
                         const char* payload, size_t payload_len, int critical) {
  if (bole_atomic_load(&handle->stopping)) {
    return -1;
  }
  if (payload == NULL) {
    return -1;
  }
  bole_event_node* node = (bole_event_node*)calloc(1, sizeof(bole_event_node));
  if (node == NULL) {
    return -1;
  }
  const char* session = session_id == NULL ? "" : session_id;
  node->session_id = bole_strdup(session);
  node->payload = (char*)malloc(payload_len + 1);
  if (node->session_id == NULL || node->payload == NULL) {
    event_node_free(node);
    return -1;
  }
  memcpy(node->payload, payload, payload_len);
  node->payload[payload_len] = '\0';
  node->bytes = sizeof(*node) + strlen(session) + payload_len + 2;
  node->event.size = sizeof(bole_event);
  node->event.type = type;
  node->event.session_id = node->session_id;
  node->event.payload_json = node->payload;
  node->event.task = task;
  node->event.step = step;
  node->event.seq = seq;

  bole_mutex_lock(&handle->event_lock);
  if (handle->event_stop) {
    bole_mutex_unlock(&handle->event_lock);
    event_node_free(node);
    return -1;
  }
  if (!critical) {
    for (;;) {
      if (handle->event_count < BOLE_EVENT_QUEUE_MAX
          && handle->event_bytes + node->bytes <= BOLE_EVENT_BYTES_MAX) {
        break;
      }
      bole_event_node* victim = event_find_delta_locked(handle);
      if (victim == NULL) {
        int is_delta = type == BOLE_EVENT_TEXT_DELTA
            || type == BOLE_EVENT_REASONING_DELTA;
        bole_mutex_unlock(&handle->event_lock);
        event_node_free(node);
        if (!is_delta) {
          fail_handle(handle, BOLE_E_EVENT_QUEUE_OVERFLOW,
                      "structural event queue overflow", "transport", 1);
        }
        return -1;
      }
      event_remove_locked(handle, victim);
      event_node_free(victim);
    }
  }

  int64_t now = ts_ms > 0 ? ts_ms : bole_wall_ms();
  if (now < (int64_t)handle->last_ts) {
    now = (int64_t)handle->last_ts;
  }
  handle->last_ts = (uint64_t)now;
  node->event.ts_ms = now;
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
  bole_mutex_lock(&handle->state_lock);
  handle->event_thread_id = bole_thread_current_id();
  bole_cond_broadcast(&handle->state_cond);
  bole_mutex_unlock(&handle->state_lock);

  for (;;) {
    bole_mutex_lock(&handle->event_lock);
    while (handle->event_head == NULL && !handle->event_stop) {
      bole_cond_wait(&handle->event_cond, &handle->event_lock, -1);
    }
    if (handle->event_stop) {
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
    handle->callback_active = 1;
    bole_mutex_unlock(&handle->state_lock);

    if (callback != NULL) {
      bole_in_callback = 1;
      callback(user_data, &node->event);
      bole_in_callback = 0;
    }

    bole_mutex_lock(&handle->state_lock);
    handle->callback_active = 0;
    bole_mutex_unlock(&handle->state_lock);
    event_node_free(node);
  }
  event_queue_clear(handle);
}

static void event_thread_stop(bole_handle* handle) {
  bole_mutex_lock(&handle->event_lock);
  handle->event_stop = 1;
  bole_cond_broadcast(&handle->event_cond);
  bole_mutex_unlock(&handle->event_lock);
  if (handle->event_thread_started) {
    bole_thread_join(&handle->event_thread);
    handle->event_thread_started = 0;
  }
  event_queue_clear(handle);
}

static int is_event_thread(bole_handle* handle) {
  bole_mutex_lock(&handle->state_lock);
  uintptr_t event_id = handle->event_thread_id;
  bole_mutex_unlock(&handle->state_lock);
  return event_id != 0 && bole_thread_current_id() == event_id;
}

/* ── Handle reference counting ──────────────────────────────────── */

static int handle_acquire(bole_handle* handle) {
  int acquired = 0;
  bole_mutex_lock(&handle->state_lock);
  if (!bole_atomic_load(&handle->stopping)) {
    handle->refs++;
    acquired = 1;
  }
  bole_mutex_unlock(&handle->state_lock);
  return acquired;
}

static void handle_drop(bole_handle* handle) {
  bole_mutex_lock(&handle->state_lock);
  if (handle->refs > 0) {
    handle->refs--;
  }
  if (handle->refs == 0) {
    bole_cond_broadcast(&handle->state_cond);
  }
  bole_mutex_unlock(&handle->state_lock);
}

/* ── Failure path ───────────────────────────────────────────────── */

static void fail_handle(bole_handle* handle, int32_t code, const char* message,
                        const char* phase, int terminate) {
  const char* text = message == NULL ? bole_error_text(code) : message;
  int emit = 0;
  bole_mutex_lock(&handle->state_lock);
  if (!bole_atomic_load(&handle->stopping) && !handle->failed && !handle->exited) {
    handle->failed = 1;
    handle->failure_code = code;
    strncpy(handle->failure_message, text, sizeof(handle->failure_message) - 1);
    handle->failure_message[sizeof(handle->failure_message) - 1] = '\0';
    handle->last_code = code;
    strncpy(handle->last_message, text, sizeof(handle->last_message) - 1);
    handle->last_message[sizeof(handle->last_message) - 1] = '\0';
    emit = 1;
  }
  bole_cond_broadcast(&handle->state_cond);
  bole_mutex_unlock(&handle->state_lock);
  if (!emit) {
    return;
  }

  /* A terminal transport/protocol failure cannot deliver the backlog in order;
   * drop it so RUNTIME_FAILED is the next callback instead of queueing behind
   * thousands of already-read events. Structural events are never dropped on a
   * healthy path; this is the documented disconnect path (03 §5). */
  if (terminate) {
    event_queue_clear(handle);
  }

  bole_buf payload;
  if (bole_buf_init(&payload, 256) == 0) {
    bole_buf_appendf(&payload, "{\"v\":1,\"code\":%d,\"message\":", (int)code);
    bole_buf_append_json_string(&payload, text);
    bole_buf_append_cstr(&payload, ",\"phase\":");
    bole_buf_append_json_string(&payload, phase == NULL ? "transport" : phase);
    bole_buf_append_cstr(&payload, "}");
    event_enqueue(handle, BOLE_EVENT_RUNTIME_FAILED, "", 0, 0, 0, 0,
                  payload.data, payload.len, 1);
    bole_buf_free(&payload);
  }
  log_line(handle, text, strlen(text));
  if (terminate) {
    bole_atomic_store(&handle->transport.stop, 1);
    bole_process_terminate(&handle->process);
  }
}

/* ── JSON field helpers ─────────────────────────────────────────── */

static int get_string(const bole_json_member* members, size_t count,
                      const char* key, char* out, size_t capacity) {
  const bole_json_member* member = bole_json_member_get(members, count, key);
  size_t length = 0;
  if (member == NULL || member->type != BOLE_JSON_STRING) {
    return -1;
  }
  return bole_json_string(member, out, capacity, &length) == 0 ? 0 : -1;
}

static char* get_string_alloc(const bole_json_member* members, size_t count,
                              const char* key) {
  const bole_json_member* member = bole_json_member_get(members, count, key);
  if (member == NULL || member->type != BOLE_JSON_STRING) {
    return NULL;
  }
  char* out = (char*)malloc(member->value_len + 1);
  if (out == NULL) {
    return NULL;
  }
  size_t length = 0;
  if (bole_json_string(member, out, member->value_len + 1, &length) != 0) {
    free(out);
    return NULL;
  }
  return out;
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

static int get_bool(const bole_json_member* members, size_t count, const char* key,
                    int fallback) {
  const bole_json_member* member = bole_json_member_get(members, count, key);
  int value = 0;
  if (member == NULL || bole_json_bool(member, &value) != 0) {
    return fallback;
  }
  return value;
}

static int get_object(const bole_json_member* members, size_t count, const char* key,
                      bole_json_member* out, size_t capacity, size_t* out_count) {
  const bole_json_member* member = bole_json_member_get(members, count, key);
  if (member == NULL || member->type != BOLE_JSON_OBJECT) {
    return 0;
  }
  if (bole_json_parse_object(member->value, member->value_len,
                             out, capacity, out_count) != 0) {
    return 0;
  }
  return 1;
}

/* ── Event projection from wire notifications ───────────────────── */

static void update_default_rsi(bole_handle* handle, int enabled, int composed,
                               const char* preset) {
  bole_mutex_lock(&handle->state_lock);
  handle->rsi.size = sizeof(bole_rsi_state);
  handle->rsi.enabled = enabled != 0 ? 1 : 0;
  handle->rsi.composed = composed != 0 ? 1 : 0;
  if (!handle->rsi.composed || preset == NULL) {
    handle->rsi.preset[0] = '\0';
  } else {
    strncpy(handle->rsi.preset, preset, sizeof(handle->rsi.preset) - 1);
    handle->rsi.preset[sizeof(handle->rsi.preset) - 1] = '\0';
  }
  handle->rsi_seen = 1;
  bole_mutex_unlock(&handle->state_lock);
}

static int is_deliverable_event_type(uint32_t type) {
  switch (type) {
    case BOLE_EVENT_TASK_STARTED:
    case BOLE_EVENT_STEP_STARTED:
    case BOLE_EVENT_STEP_FINISHED:
    case BOLE_EVENT_PLAN_UPDATED:
    case BOLE_EVENT_MODEL_SELECTED:
    case BOLE_EVENT_RSI_STATE:
    case BOLE_EVENT_TEXT_DELTA:
    case BOLE_EVENT_REASONING_DELTA:
    case BOLE_EVENT_ASSISTANT_MESSAGE:
    case BOLE_EVENT_TOOL_CALL_STARTED:
    case BOLE_EVENT_TOOL_CALL_FINISHED:
    case BOLE_EVENT_TASK_FINISHED:
    case BOLE_EVENT_TASK_FAILED:
      return 1;
    default:
      /* VERIFY_CHECK (40) has no source in this runtime; future or unknown
       * values are ignored until the library knows their payload contract. */
      return 0;
  }
}

static void handle_bole_event(bole_handle* handle, const bole_json_member* params,
                              size_t count) {
  char* session = get_string_alloc(params, count, "sessionId");
  bole_json_member event[16];
  size_t event_count = 0;
  if (!get_object(params, count, "event", event, 16, &event_count)) {
    free(session);
    return;
  }
  uint32_t type = get_u32(event, event_count, "type");
  if (!is_deliverable_event_type(type)) {
    /* RUNTIME_* are produced by this library, never by the bridge plugin;
     * VERIFY_CHECK and unknown types are ignored (03 §1/§2). */
    free(session);
    return;
  }

  char* event_session = get_string_alloc(event, event_count, "session_id");
  if (event_session != NULL && event_session[0] != '\0'
      && (session == NULL || session[0] == '\0')) {
    free(session);
    session = event_session;
    event_session = NULL;
  }
  free(event_session);

  const bole_json_member* payload = bole_json_member_get(event, event_count, "payload");
  if (payload == NULL || payload->type != BOLE_JSON_OBJECT) {
    free(session);
    return;
  }
  bole_json_member payload_members[16];
  size_t payload_count = 0;
  if (bole_json_parse_object(payload->value, payload->value_len,
                             payload_members, 16, &payload_count) != 0
      || get_u32(payload_members, payload_count, "v") != 1) {
    free(session);
    return;
  }

  if (type == BOLE_EVENT_RSI_STATE) {
    char scope[16] = "";
    char preset[BOLE_PRESET_MAX] = "";
    get_string(payload_members, payload_count, "scope", scope, sizeof(scope));
    get_string(payload_members, payload_count, "preset", preset, sizeof(preset));
    if (strcmp(scope, "default") == 0) {
      update_default_rsi(handle,
                         get_bool(payload_members, payload_count, "enabled", 0),
                         get_bool(payload_members, payload_count, "composed", 0),
                         preset);
    }
  }

  uint32_t task = get_u32(event, event_count, "task");
  uint32_t step = get_u32(event, event_count, "step");
  uint64_t seq = get_u64(event, event_count, "seq");
  int64_t ts_ms = get_i64(event, event_count, "ts_ms");
  event_enqueue(handle, type, session == NULL ? "" : session, task, step, seq, ts_ms,
                payload->value, payload->value_len, 0);
  free(session);
}

static void handle_delta(bole_handle* handle, const bole_json_member* params,
                         size_t count) {
  char channel[16] = "";
  if (get_string(params, count, "channel", channel, sizeof(channel)) != 0) {
    return;
  }
  uint32_t type;
  if (strcmp(channel, "text") == 0) {
    type = BOLE_EVENT_TEXT_DELTA;
  } else if (strcmp(channel, "reasoning") == 0) {
    type = BOLE_EVENT_REASONING_DELTA;
  } else {
    return;
  }

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
  if (decoded_len == 0) {
    free(decoded);
    return;
  }

  char* session = get_string_alloc(params, count, "sessionId");
  char* attempt = get_string_alloc(params, count, "attemptId");
  uint32_t task = get_u32(params, count, "turn");
  uint32_t step = get_u32(params, count, "step");
  uint64_t revision = get_u64(params, count, "revision");

  bole_buf payload;
  if (bole_buf_init(&payload, 256) != 0) {
    free(decoded);
    free(session);
    free(attempt);
    return;
  }
  bole_buf_append_cstr(&payload, "{\"v\":1,\"channel\":");
  bole_buf_append_json_string(&payload, channel);
  if (attempt != NULL && attempt[0] != '\0') {
    bole_buf_append_cstr(&payload, ",\"attemptId\":");
    bole_buf_append_json_string(&payload, attempt);
  }
  bole_buf_appendf(&payload, ",\"revision\":%llu,\"text\":",
                   (unsigned long long)revision);
  bole_buf_append_json_string(&payload, decoded);
  bole_buf_append_cstr(&payload, "}");

  event_enqueue(handle, type, session == NULL ? "" : session, task, step, 0, 0,
                payload.data, payload.len, 0);
  bole_buf_free(&payload);
  free(decoded);
  free(session);
  free(attempt);
}

static void handle_rsi_state(bole_handle* handle, const bole_json_member* params,
                             size_t count) {
  char scope[16] = "";
  if (get_string(params, count, "scope", scope, sizeof(scope)) != 0
      || (strcmp(scope, "default") != 0 && strcmp(scope, "session") != 0)) {
    return;
  }
  char session[256] = "";
  char preset[BOLE_PRESET_MAX] = "";
  char source[16] = "";
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

  if (strcmp(scope, "default") == 0) {
    update_default_rsi(handle, enabled, composed, preset);
  }
  event_enqueue(handle, BOLE_EVENT_RSI_STATE, session, 0, 0, 0, 0,
                payload.data, payload.len, 0);
  bole_buf_free(&payload);
}

static void handle_status(bole_handle* handle, const bole_json_member* params,
                          size_t count) {
  char status[16] = "";
  get_string(params, count, "status", status, sizeof(status));
  bole_mutex_lock(&handle->state_lock);
  handle->running = strcmp(status, "running") == 0;
  bole_mutex_unlock(&handle->state_lock);
}

static void handle_notification(bole_handle* handle, const bole_json_member* top,
                                size_t count) {
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
    handle_status(handle, params, params_count);
  }
  /* session.event, subagent.*, and future unknown notifications are ignored. */
}

/* ── initialize response and JSON-RPC responses ─────────────────── */

static void handle_initialize(bole_handle* handle, const char* result, size_t result_len,
                              int32_t code, const char* message) {
  if (code != BOLE_OK) {
    fail_handle(handle, code, message, "handshake", 1);
    return;
  }

  bole_json_member members[16];
  size_t count = 0;
  if (result == NULL
      || bole_json_parse_object(result, result_len, members, 16, &count) != 0) {
    fail_handle(handle, BOLE_E_PROTOCOL_PARSE_FAILED,
                "invalid initialize result", "handshake", 1);
    return;
  }
  uint32_t version = get_u32(members, count, "sdkProtocolVersion");
  const bole_json_member* server_info = bole_json_member_get(members, count, "serverInfo");
  const bole_json_member* capabilities = bole_json_member_get(members, count, "capabilities");
  int composed = 0;
  if (capabilities != NULL && capabilities->type == BOLE_JSON_OBJECT) {
    bole_json_member cap[8];
    size_t cap_count = 0;
    if (bole_json_parse_object(capabilities->value, capabilities->value_len,
                               cap, 8, &cap_count) == 0) {
      composed = get_bool(cap, cap_count, "preset", 0);
    }
  }

  bole_mutex_lock(&handle->state_lock);
  handle->rsi.size = sizeof(bole_rsi_state);
  handle->rsi.composed = composed;
  if (!handle->rsi_seen) {
    if (composed) {
      handle->rsi.enabled = handle->config.rsi_enabled ? 1 : 0;
      strncpy(handle->rsi.preset,
              handle->rsi.enabled ? "rsi-infer" : "standard",
              sizeof(handle->rsi.preset) - 1);
      handle->rsi.preset[sizeof(handle->rsi.preset) - 1] = '\0';
    } else {
      handle->rsi.enabled = 0;
      handle->rsi.preset[0] = '\0';
    }
    handle->rsi_seen = 1;
  }
  bole_mutex_unlock(&handle->state_lock);

  bole_buf payload;
  if (bole_buf_init(&payload, 1024) != 0) {
    fail_handle(handle, BOLE_E_RUNTIME_START_FAILED, "out of memory", "handshake", 1);
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

  bole_mutex_lock(&handle->state_lock);
  if (handle->exited || handle->failed) {
    /* The process exited while the final response was in flight; do not
     * resurrect readiness after the exit fact was published. */
    bole_mutex_unlock(&handle->state_lock);
    bole_buf_free(&payload);
    return;
  }
  handle->ready = 1;
  handle->protocol_version = version;
  /* Publish READY while holding the state lock so an immediate runtime exit
   * cannot enqueue RUNTIME_EXITED before RUNTIME_READY. */
  event_enqueue(handle, BOLE_EVENT_RUNTIME_READY, "", 0, 0, 0, 0,
                payload.data, payload.len, 1);
  bole_cond_broadcast(&handle->state_cond);
  bole_mutex_unlock(&handle->state_lock);
  bole_buf_free(&payload);

  if (version > 1) {
    static const char WARNING[] = "runtime sdkProtocolVersion is newer than the SDK";
    log_line(handle, WARNING, sizeof(WARNING) - 1);
  }
}

static void handle_response(bole_handle* handle, const bole_json_member* top, size_t count) {
  uint64_t id = get_u64(top, count, "id");
  int32_t code = 0;
  char message[512] = "";
  char* result = NULL;
  size_t result_len = 0;

  const bole_json_member* error = bole_json_member_get(top, count, "error");
  if (error != NULL && error->type == BOLE_JSON_OBJECT) {
    bole_json_member error_members[8];
    size_t error_count = 0;
    int rpc_code = -32603;
    if (bole_json_parse_object(error->value, error->value_len,
                               error_members, 8, &error_count) == 0) {
      const bole_json_member* rpc_member =
          bole_json_member_get(error_members, error_count, "code");
      int64_t rpc_value = 0;
      if (rpc_member != NULL && bole_json_i64(rpc_member, &rpc_value) == 0) {
        rpc_code = (int)rpc_value;
      }
      get_string(error_members, error_count, "message", message, sizeof(message));
      bole_json_member data_members[8];
      size_t data_count = 0;
      char wire[64] = "";
      if (get_object(error_members, error_count, "data",
                     data_members, 8, &data_count)) {
        get_string(data_members, data_count, "code", wire, sizeof(wire));
      }
      code = bole_error_from_wire(wire[0] == '\0' ? NULL : wire, rpc_code);
      if (message[0] == '\0') {
        strncpy(message, bole_error_text(code), sizeof(message) - 1);
      }
    } else {
      code = BOLE_E_PROTOCOL_PARSE_FAILED;
      strncpy(message, "malformed JSON-RPC error", sizeof(message) - 1);
    }
  } else {
    const bole_json_member* result_member = bole_json_member_get(top, count, "result");
    if (result_member == NULL) {
      code = BOLE_E_PROTOCOL_PARSE_FAILED;
      strncpy(message, "response has neither result nor error", sizeof(message) - 1);
    } else {
      result = (char*)malloc(result_member->value_len + 1);
      if (result == NULL) {
        code = BOLE_E_RUNTIME_START_FAILED;
        strncpy(message, "out of memory", sizeof(message) - 1);
      } else {
        memcpy(result, result_member->value, result_member->value_len);
        result[result_member->value_len] = '\0';
        result_len = result_member->value_len;
      }
    }
  }

  if (id == handle->init_id) {
    handle_initialize(handle, result, result_len, code, message);
  } else if (id != 0) {
    bole_rpc_complete(&handle->rpc, id, code, message, result, result_len);
  }
  free(result);
}

/* ── Transport callbacks ────────────────────────────────────────── */

static void on_line(void* user, const char* line, size_t len) {
  bole_handle* handle = (bole_handle*)user;
  bole_json_member top[16];
  size_t count = 0;
  if (bole_json_parse_object(line, len, top, 16, &count) != 0) {
    int32_t code = handle->first_line ? BOLE_E_STDOUT_VIOLATION
                                      : BOLE_E_PROTOCOL_PARSE_FAILED;
    const char* text = handle->first_line
        ? "stdout does not start with a JSON-RPC frame"
        : "invalid JSON-RPC frame";
    fail_handle(handle, code, text, "transport", 1);
    bole_atomic_store(&handle->transport.stop, 1);
    return;
  }
  if (handle->first_line) {
    char protocol[8] = "";
    if (get_string(top, count, "jsonrpc", protocol, sizeof(protocol)) != 0
        || strcmp(protocol, "2.0") != 0) {
      fail_handle(handle, BOLE_E_STDOUT_VIOLATION,
                  "stdout does not start with a JSON-RPC 2.0 frame",
                  "transport", 1);
      bole_atomic_store(&handle->transport.stop, 1);
      return;
    }
    handle->first_line = 0;
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
  fail_handle(handle, code, message, "transport", 1);
}

/* ── Wait thread ────────────────────────────────────────────────── */

static void wait_thread_main(void* argument) {
  bole_handle* handle = (bole_handle*)argument;
  int exit_code = 0;
  bole_process_wait(&handle->process, &exit_code);

  int was_ready;
  int was_failed;
  bole_mutex_lock(&handle->state_lock);
  handle->exited = 1;
  was_ready = handle->ready;
  was_failed = handle->failed;
  handle->ready = 0;
  if (!was_failed) {
    handle->failed = 1;
    handle->failure_code = BOLE_E_RUNTIME_EXITED;
    strncpy(handle->failure_message, "runtime process exited",
            sizeof(handle->failure_message) - 1);
    handle->failure_message[sizeof(handle->failure_message) - 1] = '\0';
    strncpy(handle->last_message, "runtime process exited",
            sizeof(handle->last_message) - 1);
    handle->last_message[sizeof(handle->last_message) - 1] = '\0';
    handle->last_code = BOLE_E_RUNTIME_EXITED;
  }
  bole_cond_broadcast(&handle->state_cond);
  bole_mutex_unlock(&handle->state_lock);

  bole_rpc_close(&handle->rpc);

  if (!was_ready && !was_failed) {
    bole_buf payload;
    if (bole_buf_init(&payload, 256) == 0) {
      bole_buf_appendf(
          &payload,
          "{\"v\":1,\"code\":%d,\"message\":\"runtime process exited\","
          "\"phase\":\"exit\"}",
          (int)BOLE_E_RUNTIME_EXITED);
      event_enqueue(handle, BOLE_EVENT_RUNTIME_FAILED, "", 0, 0, 0, 0,
                    payload.data, payload.len, 1);
      bole_buf_free(&payload);
    }
  } else if (was_ready && !was_failed) {
    bole_buf payload;
    if (bole_buf_init(&payload, 128) == 0) {
      bole_buf_appendf(&payload,
                       "{\"v\":1,\"exitCode\":%d,\"hadActiveTasks\":false}",
                       exit_code);
      event_enqueue(handle, BOLE_EVENT_RUNTIME_EXITED, "", 0, 0, 0, 0,
                    payload.data, payload.len, 1);
      bole_buf_free(&payload);
    }
  }
  bole_atomic_store(&handle->transport.stop, 1);
}

/* ── Ready wait and request helper ──────────────────────────────── */

static int32_t ensure_ready(bole_handle* handle, int timeout_ms, int32_t timeout_code) {
  uint64_t deadline = bole_monotonic_ms()
      + (uint64_t)(timeout_ms > 0 ? timeout_ms : 0);
  int32_t result = BOLE_OK;
  char message[512] = "";
  bole_mutex_lock(&handle->state_lock);
  for (;;) {
    if (bole_atomic_load(&handle->stopping)) {
      result = BOLE_E_RUNTIME_EXITED;
      break;
    }
    if (handle->exited) {
      result = BOLE_E_RUNTIME_EXITED;
      break;
    }
    if (handle->failed) {
      result = handle->failure_code;
      strncpy(message, handle->failure_message, sizeof(message) - 1);
      message[sizeof(message) - 1] = '\0';
      break;
    }
    if (handle->ready) {
      result = BOLE_OK;
      break;
    }
    uint64_t now = bole_monotonic_ms();
    if (now >= deadline) {
      result = timeout_code;
      break;
    }
    bole_cond_wait(&handle->state_cond, &handle->state_lock, (int)(deadline - now));
  }
  bole_mutex_unlock(&handle->state_lock);
  if (result != BOLE_OK) {
    const char* text = message[0] == '\0' ? bole_error_text(result) : message;
    record_error(handle, result, text);
  }
  return result;
}

static int32_t send_request(bole_handle* handle, const char* method, const char* params,
                            int timeout_ms, bole_response* response) {
  memset(response, 0, sizeof(*response));
  if (bole_atomic_load(&handle->stopping)) {
    response->code = BOLE_E_RUNTIME_EXITED;
    return BOLE_E_RUNTIME_EXITED;
  }
  uint64_t id = bole_rpc_next_id(&handle->rpc);
  bole_pending* pending = bole_rpc_begin(&handle->rpc, id);
  if (pending == NULL) {
    response->code = BOLE_E_RUNTIME_EXITED;
    record_error(handle, BOLE_E_RUNTIME_EXITED, "runtime transport is closed");
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
  int sent = bole_transport_send(&handle->transport, line.data, line.len);
  if (sent != 0) {
    code = sent == -2 ? BOLE_E_LINE_TOO_LONG : BOLE_E_PIPE_FAILED;
    response->code = code;
    if (!bole_atomic_load(&handle->stopping)) {
      fail_handle(handle, code,
                  sent == -2 ? "outgoing JSON-RPC line exceeds max_line_bytes"
                             : "runtime stdin write failed",
                  "transport", 1);
    }
  } else {
    int waited = bole_rpc_wait(pending, timeout_ms, response);
    if (waited == 1) {
      code = BOLE_E_REQUEST_TIMEOUT;
      response->code = code;
      strncpy(response->message, "request timed out", sizeof(response->message) - 1);
    } else {
      code = response->code;
    }
  }
  bole_buf_free(&line);
  bole_rpc_end(&handle->rpc, pending);
  if (code != BOLE_OK) {
    const char* text = response->message[0] == '\0'
        ? bole_error_text(code) : response->message;
    record_error(handle, code, text);
  }
  return code;
}

/* ── Sessions ───────────────────────────────────────────────────── */

static bole_session* find_session_locked(bole_handle* handle, const char* id) {
  for (bole_session* session = handle->sessions; session != NULL; session = session->next) {
    if (strcmp(session->id, id) == 0) {
      return session;
    }
  }
  return NULL;
}

static int32_t add_session(bole_handle* handle, const char* id, bole_session** out) {
  bole_mutex_lock(&handle->state_lock);
  if (find_session_locked(handle, id) != NULL) {
    bole_mutex_unlock(&handle->state_lock);
    return BOLE_E_SESSION_EXISTS;
  }
  bole_session* session = (bole_session*)calloc(1, sizeof(bole_session));
  if (session == NULL) {
    bole_mutex_unlock(&handle->state_lock);
    return BOLE_E_RUNTIME_REQUEST_FAILED;
  }
  session->id = bole_strdup(id);
  if (session->id == NULL) {
    free(session);
    bole_mutex_unlock(&handle->state_lock);
    return BOLE_E_RUNTIME_REQUEST_FAILED;
  }
  session->handle = handle;
  session->next = handle->sessions;
  handle->sessions = session;
  handle->session_count++;
  bole_mutex_unlock(&handle->state_lock);
  *out = session;
  return BOLE_OK;
}

static int32_t session_handle(bole_session* session, bole_handle** out) {
  if (session == NULL || session->handle == NULL || session->id == NULL) {
    return BOLE_E_INVALID_CONFIG;
  }
  *out = session->handle;
  return BOLE_OK;
}

static int32_t build_session_params(const char* session_id,
                                    const bole_session_options* options,
                                    bole_buf* out) {
  if (session_id == NULL || session_id[0] == '\0' || out == NULL) {
    return BOLE_E_INVALID_CONFIG;
  }
  const char* cwd = NULL;
  const char* preset = NULL;
  if (options != NULL) {
    if (options->size < sizeof(uint32_t)) {
      return BOLE_E_INVALID_CONFIG;
    }
    uint32_t declared = options->size;
    if (BOLE_OPTIONS_FIELD_OK(declared, cwd)) {
      cwd = options->cwd;
    }
    if (BOLE_OPTIONS_FIELD_OK(declared, preset)) {
      preset = options->preset;
    }
    if (cwd != NULL && !is_absolute_path(cwd)) {
      return BOLE_E_INVALID_CONFIG;
    }
    if (preset != NULL && preset[0] == '\0') {
      return BOLE_E_INVALID_CONFIG;
    }
  }
  if (bole_buf_init(out, 256) != 0) {
    return BOLE_E_RUNTIME_START_FAILED;
  }
  bole_buf_append_cstr(out, "{\"sessionId\":");
  bole_buf_append_json_string(out, session_id);
  if (cwd != NULL) {
    bole_buf_append_cstr(out, ",\"cwd\":");
    bole_buf_append_json_string(out, cwd);
  }
  if (preset != NULL) {
    bole_buf_append_cstr(out, ",\"preset\":");
    bole_buf_append_json_string(out, preset);
  }
  bole_buf_append_cstr(out, "}");
  return BOLE_OK;
}

static int32_t submit_params(bole_session* session, const char* blocks_json,
                             bole_buf* params) {
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

static int validate_content_blocks(const char* json) {
  if (json == NULL) {
    return 0;
  }
  const char* p = json;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
    p++;
  }
  if (*p != '[') {
    return 0;
  }
  p++;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
    p++;
  }
  if (*p == ']') {
    return 0;
  }
  return bole_json_validate(json, strlen(json)) == 0;
}

/* ── Environment and process ────────────────────────────────────── */

static char* make_env_entry(const char* key, const char* value) {
  size_t key_len = strlen(key);
  size_t value_len = strlen(value);
  char* entry = (char*)malloc(key_len + value_len + 2);
  if (entry == NULL) {
    return NULL;
  }
  memcpy(entry, key, key_len);
  entry[key_len] = '=';
  memcpy(entry + key_len + 1, value, value_len + 1);
  return entry;
}

static int starts_with(const char* text, const char* prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}

static void free_environment(char** environment) {
  if (environment == NULL) {
    return;
  }
  for (size_t i = 0; environment[i] != NULL; i++) {
    free(environment[i]);
  }
  free(environment);
}

static char** build_environment(bole_handle* handle) {
  char** base = bole_environment_list();
  if (base == NULL) {
    return NULL;
  }
  size_t count = 0;
  while (base[count] != NULL) {
    count++;
  }
  char** environment = (char**)calloc(count + 4, sizeof(char*));
  if (environment == NULL) {
    bole_environment_free(base);
    return NULL;
  }
  size_t out = 0;
  for (size_t i = 0; i < count; i++) {
    if (starts_with(base[i], "DSH_HOME=")
        || starts_with(base[i], "DSH_PERMISSION_MODE=")
        || starts_with(base[i], "DSH_TELEMETRY_DISABLED=")) {
      continue;
    }
    environment[out] = bole_strdup(base[i]);
    if (environment[out] == NULL) {
      free_environment(environment);
      bole_environment_free(base);
      return NULL;
    }
    out++;
  }
  bole_environment_free(base);

  const char* sandbox = handle->config.sandbox_mode == BOLE_SANDBOX_DANGER_FULL_ACCESS
      ? "danger-full-access" : "workspace-write";
  environment[out] = make_env_entry("DSH_HOME", handle->config.dsh_home);
  environment[out + 1] = make_env_entry("DSH_PERMISSION_MODE", sandbox);
  environment[out + 2] = make_env_entry("DSH_TELEMETRY_DISABLED", "1");
  if (environment[out] == NULL || environment[out + 1] == NULL
      || environment[out + 2] == NULL) {
    free_environment(environment);
    return NULL;
  }
  return environment;
}

/* ── Handle lifetime ────────────────────────────────────────────── */

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
  if (handle->resources & BOLE_RES_RPC) {
    bole_rpc_destroy(&handle->rpc);
  }
  if (handle->resources & BOLE_RES_LOG_LOCK) {
    bole_mutex_destroy(&handle->log_lock);
  }
  if (handle->resources & BOLE_RES_EVENT_COND) {
    bole_cond_destroy(&handle->event_cond);
  }
  if (handle->resources & BOLE_RES_EVENT_LOCK) {
    bole_mutex_destroy(&handle->event_lock);
  }
  if (handle->resources & BOLE_RES_STATE_COND) {
    bole_cond_destroy(&handle->state_cond);
  }
  if (handle->resources & BOLE_RES_STATE_LOCK) {
    bole_mutex_destroy(&handle->state_lock);
  }
  if (handle->process.spawned) {
    bole_mutex_destroy(&handle->process.lock);
    bole_cond_destroy(&handle->process.cond);
  }
  free(handle);
}

static void reap_and_free(bole_handle* handle) {
  if (handle == NULL) {
    return;
  }
  bole_atomic_store(&handle->stopping, 1);
  bole_mutex_lock(&handle->event_lock);
  handle->event_stop = 1;
  bole_cond_broadcast(&handle->event_cond);
  bole_mutex_unlock(&handle->event_lock);

  if (handle->process.spawned && handle->transport.started
      && !bole_atomic_load(&handle->transport.stop)) {
    /* Ask the runtime to exit first; the grace window below then kills the
     * process tree. This runs on the reaper thread so bole_release stays
     * immediate even when the runtime is not reading stdin. */
    uint64_t id = bole_rpc_next_id(&handle->rpc);
    bole_buf shutdown;
    if (bole_buf_init(&shutdown, 128) == 0) {
      bole_buf_appendf(&shutdown,
                       "{\"jsonrpc\":\"2.0\",\"id\":%llu,\"method\":\"shutdown\"}",
                       (unsigned long long)id);
      bole_transport_send(&handle->transport, shutdown.data, shutdown.len);
      bole_buf_free(&shutdown);
    }
  }

  if (handle->process.spawned) {
    if (handle->wait_thread_started) {
      bole_mutex_lock(&handle->process.lock);
      uint64_t deadline = bole_monotonic_ms() + BOLE_RELEASE_GRACE_MS;
      while (!handle->process.exited) {
        uint64_t now = bole_monotonic_ms();
        if (now >= deadline) {
          break;
        }
        bole_cond_wait(&handle->process.cond, &handle->process.lock,
                       (int)(deadline - now));
      }
      bole_mutex_unlock(&handle->process.lock);
    }
    bole_process_terminate(&handle->process);
    if (!handle->wait_thread_started) {
      int exit_code = 0;
      bole_process_wait(&handle->process, &exit_code);
    }
  }

  bole_transport_stop(&handle->transport);
  if (handle->rpc_ready) {
    bole_rpc_close(&handle->rpc);
  }
  event_thread_stop(handle);
  if (handle->wait_thread_started) {
    bole_thread_join(&handle->wait_thread);
    handle->wait_thread_started = 0;
  }
  if (handle->process.spawned) {
    bole_process_close(&handle->process);
  }
  free_handle(handle);
}

static void reaper_main(void* argument) {
  bole_handle* handle = (bole_handle*)argument;
  bole_mutex_lock(&handle->state_lock);
  while (handle->refs > 0) {
    bole_cond_wait(&handle->state_cond, &handle->state_lock, -1);
  }
  bole_mutex_unlock(&handle->state_lock);
  reap_and_free(handle);
}

/* ── Public ABI ─────────────────────────────────────────────────── */

int32_t BOLE_CALL bole_init(const bole_config* config, bole_handle** out_handle) {
  if (bole_in_callback) {
    return BOLE_E_INVALID_CONFIG;
  }
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
  handle->first_line = 1;
  handle->refs = 1;
  handle->next_event_id = 1;
  handle->event_cb = copied.event_cb;
  handle->user_data = copied.user_data;
  handle->rsi.size = sizeof(bole_rsi_state);
  handle->rsi.enabled = 0;
  handle->rsi.composed = 0;
  handle->rsi.preset[0] = '\0';

  if (bole_mutex_init(&handle->state_lock) != 0) {
    free_handle(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  handle->resources |= BOLE_RES_STATE_LOCK;
  if (bole_cond_init(&handle->state_cond) != 0) {
    free_handle(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  handle->resources |= BOLE_RES_STATE_COND;
  if (bole_mutex_init(&handle->event_lock) != 0) {
    free_handle(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  handle->resources |= BOLE_RES_EVENT_LOCK;
  if (bole_cond_init(&handle->event_cond) != 0) {
    free_handle(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  handle->resources |= BOLE_RES_EVENT_COND;
  if (bole_mutex_init(&handle->log_lock) != 0) {
    free_handle(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  handle->resources |= BOLE_RES_LOG_LOCK;
  if (bole_rpc_init(&handle->rpc) != 0) {
    free_handle(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  handle->resources |= BOLE_RES_RPC;
  handle->rpc_ready = 1;
  /* Reserve the initialize request id before any reader thread starts, so the
   * response path never observes a concurrently written id. */
  handle->init_id = bole_rpc_next_id(&handle->rpc);

  if (handle->config.log_path != NULL) {
    handle->log = bole_fopen_utf8(handle->config.log_path, "ab");
  }

  char* node_path = path_join(handle->config.runtime_dir, "node.exe");
  char* entry_path = path_join(handle->config.runtime_dir,
                               "node_modules/@deepseek-ai/dsh/lib/bin.js");
  char* patch_path = path_join(handle->config.runtime_dir, "cordis.patch.yml");
  if (node_path == NULL || entry_path == NULL || patch_path == NULL
      || !bole_path_exists(node_path) || !bole_path_exists(entry_path)
      || !bole_path_exists(patch_path)) {
    free(node_path);
    free(entry_path);
    free(patch_path);
    free_handle(handle);
    return BOLE_E_RUNTIME_DIR_NOT_FOUND;
  }
  if (bole_mkdir_p(handle->config.dsh_home) != 0) {
    free(node_path);
    free(entry_path);
    free(patch_path);
    free_handle(handle);
    return BOLE_E_INVALID_CONFIG;
  }

  char* argv[7];
  argv[0] = node_path;
  argv[1] = entry_path;
  argv[2] = (char*)"--profile";
  argv[3] = (char*)"sdk";
  argv[4] = (char*)"--patch";
  argv[5] = patch_path;
  argv[6] = NULL;

  char** environment = build_environment(handle);
  if (environment == NULL) {
    free(node_path);
    free(entry_path);
    free(patch_path);
    free_handle(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  int spawn = bole_process_spawn(&handle->process, node_path, argv,
                                 handle->config.cwd, environment);
  free_environment(environment);
  free(node_path);
  free(entry_path);
  free(patch_path);
  if (spawn != 0) {
    free_handle(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }

  if (bole_transport_start(&handle->transport, &handle->process,
                           handle->config.max_line_bytes, handle,
                           on_line, on_stderr_line, on_eof, on_error) != 0) {
    reap_and_free(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }

  if (bole_thread_start(&handle->wait_thread, wait_thread_main, handle) != 0) {
    reap_and_free(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  handle->wait_thread_started = 1;

  if (bole_thread_start(&handle->event_thread, event_thread_main, handle) != 0) {
    reap_and_free(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  handle->event_thread_started = 1;
  /* Wait until the event thread has published its identity so callback-only
   * restrictions can be enforced from the first delivered event. */
  bole_mutex_lock(&handle->state_lock);
  uint64_t id_deadline = bole_monotonic_ms() + 1000;
  while (handle->event_thread_id == 0) {
    uint64_t now = bole_monotonic_ms();
    if (now >= id_deadline) {
      break;
    }
    bole_cond_wait(&handle->state_cond, &handle->state_lock, (int)(id_deadline - now));
  }
  bole_mutex_unlock(&handle->state_lock);

  bole_buf initialize;
  if (bole_buf_init(&initialize, 512) != 0) {
    reap_and_free(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  bole_buf_appendf(&initialize,
                   "{\"jsonrpc\":\"2.0\",\"id\":%llu,\"method\":\"initialize\",\"params\":{\"cwd\":",
                   (unsigned long long)handle->init_id);
  bole_buf_append_json_string(&initialize, handle->config.cwd);
  bole_buf_append_cstr(&initialize, ",\"provider\":");
  bole_buf_append_json_string(&initialize, handle->config.model_provider);
  bole_buf_append_cstr(&initialize, ",\"model\":");
  bole_buf_append_json_string(&initialize, handle->config.model_name);
  if (handle->config.reasoning_effort != NULL) {
    bole_buf_append_cstr(&initialize, ",\"reasoningEffort\":");
    bole_buf_append_json_string(&initialize, handle->config.reasoning_effort);
  }
  if (handle->config.max_tokens > 0) {
    bole_buf_appendf(&initialize, ",\"maxTokens\":%d", (int)handle->config.max_tokens);
  }
  bole_buf_appendf(&initialize, ",\"rsiEnabled\":%s}}",
                   handle->config.rsi_enabled ? "true" : "false");

  int sent = bole_transport_send(&handle->transport, initialize.data, initialize.len);
  bole_buf_free(&initialize);
  if (sent != 0) {
    reap_and_free(handle);
    return sent == -2 ? BOLE_E_LINE_TOO_LONG : BOLE_E_RUNTIME_START_FAILED;
  }
  *out_handle = handle;
  return BOLE_OK;
}

int32_t BOLE_CALL bole_wait_ready(bole_handle* handle, uint32_t timeout_ms) {
  if (handle == NULL) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (bole_in_callback || is_event_thread(handle)) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (!handle_acquire(handle)) {
    return BOLE_E_RUNTIME_EXITED;
  }
  int32_t code = ensure_ready(handle, clamp_timeout(timeout_ms),
                              BOLE_E_WAIT_READY_TIMEOUT);
  handle_drop(handle);
  return code;
}

int32_t BOLE_CALL bole_set_event_callback(bole_handle* handle, bole_event_cb callback,
                                          void* user_data) {
  if (handle == NULL) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (callback == NULL) {
    return BOLE_E_EVENT_CALLBACK_REQUIRED;
  }
  if (!handle_acquire(handle)) {
    return BOLE_E_RUNTIME_EXITED;
  }
  bole_mutex_lock(&handle->state_lock);
  handle->event_cb = callback;
  handle->user_data = user_data;
  bole_mutex_unlock(&handle->state_lock);
  handle_drop(handle);
  return BOLE_OK;
}

int32_t BOLE_CALL bole_release(bole_handle* handle) {
  if (handle == NULL) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (bole_in_callback || is_event_thread(handle)) {
    return BOLE_E_INVALID_CONFIG;
  }
  bole_mutex_lock(&handle->state_lock);
  if (bole_atomic_load(&handle->stopping)) {
    bole_mutex_unlock(&handle->state_lock);
    return BOLE_OK;
  }
  bole_atomic_store(&handle->stopping, 1);
  bole_cond_broadcast(&handle->state_cond);
  bole_mutex_unlock(&handle->state_lock);

  bole_mutex_lock(&handle->event_lock);
  handle->event_stop = 1;
  bole_cond_broadcast(&handle->event_cond);
  bole_mutex_unlock(&handle->event_lock);

  if (bole_thread_start(&handle->reaper_thread, reaper_main, handle) == 0) {
    bole_thread_detach(&handle->reaper_thread);
    /* Drop the caller's reference only after the reaper handle is detached, so
     * the reaper cannot free the handle under this function. */
    handle_drop(handle);
    return BOLE_OK;
  }
  handle_drop(handle);
  reaper_main(handle);
  return BOLE_OK;
}

int32_t BOLE_CALL bole_session_create(bole_handle* handle, const char* session_id,
                                      const bole_session_options* options,
                                      bole_session** out_session) {
  if (handle == NULL) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (session_id == NULL || session_id[0] == '\0' || out_session == NULL) {
    return BOLE_E_INVALID_CONFIG;
  }
  *out_session = NULL;
  if (!handle_acquire(handle)) {
    return BOLE_E_RUNTIME_EXITED;
  }
  int32_t code = ensure_ready(handle, BOLE_CONTROL_TIMEOUT_MS, BOLE_E_REQUEST_TIMEOUT);
  if (code != BOLE_OK) {
    handle_drop(handle);
    return code;
  }
  bole_mutex_lock(&handle->state_lock);
  int exists = find_session_locked(handle, session_id) != NULL;
  bole_mutex_unlock(&handle->state_lock);
  if (exists) {
    record_error(handle, BOLE_E_SESSION_EXISTS, "session already exists");
    handle_drop(handle);
    return BOLE_E_SESSION_EXISTS;
  }

  bole_buf params;
  code = build_session_params(session_id, options, &params);
  if (code != BOLE_OK) {
    record_error(handle, code, bole_error_text(code));
    handle_drop(handle);
    return code;
  }
  bole_response response;
  code = send_request(handle, "session/create", params.data,
                      BOLE_CONTROL_TIMEOUT_MS, &response);
  bole_buf_free(&params);
  bole_response_free(&response);
  if (code == BOLE_OK) {
    code = add_session(handle, session_id, out_session);
    if (code != BOLE_OK) {
      record_error(handle, code, bole_error_text(code));
    }
  }
  handle_drop(handle);
  return code;
}

int32_t BOLE_CALL bole_session_resume(bole_handle* handle, const char* session_id,
                                      bole_session** out_session) {
  if (handle == NULL) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (session_id == NULL || session_id[0] == '\0' || out_session == NULL) {
    return BOLE_E_INVALID_CONFIG;
  }
  *out_session = NULL;
  if (!handle_acquire(handle)) {
    return BOLE_E_RUNTIME_EXITED;
  }
  int32_t code = ensure_ready(handle, BOLE_CONTROL_TIMEOUT_MS, BOLE_E_REQUEST_TIMEOUT);
  if (code != BOLE_OK) {
    handle_drop(handle);
    return code;
  }
  bole_mutex_lock(&handle->state_lock);
  bole_session* existing = find_session_locked(handle, session_id);
  bole_mutex_unlock(&handle->state_lock);
  if (existing != NULL) {
    *out_session = existing;
    handle_drop(handle);
    return BOLE_OK;
  }

  bole_buf params;
  if (bole_buf_init(&params, 128) != 0) {
    handle_drop(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  bole_buf_append_cstr(&params, "{\"sessionId\":");
  bole_buf_append_json_string(&params, session_id);
  bole_buf_append_cstr(&params, "}");
  bole_response response;
  code = send_request(handle, "session/resume", params.data,
                      BOLE_RESUME_TIMEOUT_MS, &response);
  bole_buf_free(&params);
  bole_response_free(&response);
  if (code == BOLE_OK) {
    code = add_session(handle, session_id, out_session);
    if (code != BOLE_OK) {
      record_error(handle, code, bole_error_text(code));
    }
  }
  handle_drop(handle);
  return code;
}

int32_t BOLE_CALL bole_session_set_preset(bole_session* session, const char* preset_id) {
  bole_handle* handle = NULL;
  int32_t code = session_handle(session, &handle);
  if (code != BOLE_OK) {
    return code;
  }
  if (preset_id == NULL || preset_id[0] == '\0') {
    return BOLE_E_INVALID_CONFIG;
  }
  if (!handle_acquire(handle)) {
    return BOLE_E_RUNTIME_EXITED;
  }
  code = ensure_ready(handle, BOLE_CONTROL_TIMEOUT_MS, BOLE_E_REQUEST_TIMEOUT);
  if (code != BOLE_OK) {
    handle_drop(handle);
    return code;
  }
  bole_buf params;
  if (bole_buf_init(&params, 128) != 0) {
    handle_drop(handle);
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
  handle_drop(handle);
  return code;
}

int32_t BOLE_CALL bole_submit(bole_session* session, const char* utf8_text) {
  bole_handle* handle = NULL;
  int32_t code = session_handle(session, &handle);
  if (code != BOLE_OK) {
    return code;
  }
  if (utf8_text == NULL || utf8_text[0] == '\0') {
    return BOLE_E_INVALID_CONFIG;
  }
  if (!handle_acquire(handle)) {
    return BOLE_E_RUNTIME_EXITED;
  }
  code = ensure_ready(handle, BOLE_CONTROL_TIMEOUT_MS, BOLE_E_REQUEST_TIMEOUT);
  if (code != BOLE_OK) {
    handle_drop(handle);
    return code;
  }
  bole_buf blocks;
  if (bole_buf_init(&blocks, strlen(utf8_text) + 64) != 0) {
    handle_drop(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  bole_buf_append_cstr(&blocks, "[{\"type\":\"text\",\"text\":");
  bole_buf_append_json_string(&blocks, utf8_text);
  bole_buf_append_cstr(&blocks, "}]");
  bole_buf params;
  code = submit_params(session, blocks.data, &params);
  bole_buf_free(&blocks);
  if (code != BOLE_OK) {
    handle_drop(handle);
    return code;
  }
  bole_response response;
  code = send_request(handle, "session/prompt", params.data,
                      BOLE_CONTROL_TIMEOUT_MS, &response);
  bole_buf_free(&params);
  bole_response_free(&response);
  handle_drop(handle);
  return code;
}

int32_t BOLE_CALL bole_submit_blocks(bole_session* session, const char* content_blocks_json) {
  bole_handle* handle = NULL;
  int32_t code = session_handle(session, &handle);
  if (code != BOLE_OK) {
    return code;
  }
  if (!validate_content_blocks(content_blocks_json)) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (!handle_acquire(handle)) {
    return BOLE_E_RUNTIME_EXITED;
  }
  code = ensure_ready(handle, BOLE_CONTROL_TIMEOUT_MS, BOLE_E_REQUEST_TIMEOUT);
  if (code != BOLE_OK) {
    handle_drop(handle);
    return code;
  }
  bole_buf params;
  code = submit_params(session, content_blocks_json, &params);
  if (code != BOLE_OK) {
    handle_drop(handle);
    return code;
  }
  bole_response response;
  code = send_request(handle, "session/prompt", params.data,
                      BOLE_CONTROL_TIMEOUT_MS, &response);
  bole_buf_free(&params);
  bole_response_free(&response);
  handle_drop(handle);
  return code;
}

int32_t BOLE_CALL bole_cancel(bole_session* session) {
  bole_handle* handle = NULL;
  int32_t code = session_handle(session, &handle);
  if (code != BOLE_OK) {
    return code;
  }
  if (!handle_acquire(handle)) {
    return BOLE_E_RUNTIME_EXITED;
  }
  code = ensure_ready(handle, BOLE_CONTROL_TIMEOUT_MS, BOLE_E_REQUEST_TIMEOUT);
  if (code != BOLE_OK) {
    handle_drop(handle);
    return code;
  }
  bole_buf params;
  if (bole_buf_init(&params, 128) != 0) {
    handle_drop(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  bole_buf_append_cstr(&params, "{\"sessionId\":");
  bole_buf_append_json_string(&params, session->id);
  bole_buf_append_cstr(&params, "}");
  bole_response response;
  code = send_request(handle, "session/cancel", params.data,
                      BOLE_CONTROL_TIMEOUT_MS, &response);
  bole_buf_free(&params);
  bole_response_free(&response);
  handle_drop(handle);
  return code;
}

int32_t BOLE_CALL bole_rsi_set(bole_handle* handle, int32_t enabled) {
  if (handle == NULL) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (enabled != 0 && enabled != 1) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (!handle_acquire(handle)) {
    return BOLE_E_RUNTIME_EXITED;
  }
  int32_t code = ensure_ready(handle, BOLE_CONTROL_TIMEOUT_MS, BOLE_E_REQUEST_TIMEOUT);
  if (code != BOLE_OK) {
    handle_drop(handle);
    return code;
  }
  bole_response response;
  code = send_request(handle, "rsi.set",
                      enabled ? "{\"enabled\":true}" : "{\"enabled\":false}",
                      BOLE_CONTROL_TIMEOUT_MS, &response);
  if (code == BOLE_OK) {
    int actual = enabled;
    char preset[BOLE_PRESET_MAX] = "";
    if (response.result != NULL) {
      bole_json_member members[8];
      size_t count = 0;
      if (bole_json_parse_object(response.result, response.result_len,
                                 members, 8, &count) == 0) {
        actual = get_bool(members, count, "enabled", enabled);
        get_string(members, count, "preset", preset, sizeof(preset));
      }
    }
    if (preset[0] == '\0') {
      strncpy(preset, actual ? "rsi-infer" : "standard", sizeof(preset) - 1);
      preset[sizeof(preset) - 1] = '\0';
    }
    update_default_rsi(handle, actual, 1, preset);
  }
  bole_response_free(&response);
  handle_drop(handle);
  return code;
}

int32_t BOLE_CALL bole_rsi_get(bole_handle* handle, bole_rsi_state* out_state) {
  if (handle == NULL) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (out_state == NULL || out_state->size < sizeof(uint32_t)) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (!handle_acquire(handle)) {
    return BOLE_E_RUNTIME_EXITED;
  }
  bole_rsi_state state;
  bole_mutex_lock(&handle->state_lock);
  state = handle->rsi;
  bole_mutex_unlock(&handle->state_lock);
  handle_drop(handle);

  size_t copy = out_state->size < sizeof(state) ? out_state->size : sizeof(state);
  state.size = (uint32_t)copy;
  memcpy(out_state, &state, copy);
  return BOLE_OK;
}

int32_t BOLE_CALL bole_trace_export(bole_handle* handle, const char* session_id,
                                    uint32_t turn, const char* path,
                                    uint32_t* out_event_count) {
  if (out_event_count != NULL) {
    *out_event_count = 0;
  }
  if (handle == NULL) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (session_id == NULL || session_id[0] == '\0' || path == NULL || path[0] == '\0') {
    return BOLE_E_INVALID_CONFIG;
  }
  if (bole_in_callback || is_event_thread(handle)) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (!handle_acquire(handle)) {
    return BOLE_E_RUNTIME_EXITED;
  }
  int32_t code = ensure_ready(handle, BOLE_TRACE_TIMEOUT_MS, BOLE_E_REQUEST_TIMEOUT);
  if (code != BOLE_OK) {
    handle_drop(handle);
    return code;
  }
  bole_buf params;
  if (bole_buf_init(&params, 256) != 0) {
    handle_drop(handle);
    return BOLE_E_RUNTIME_START_FAILED;
  }
  bole_buf_append_cstr(&params, "{\"sessionId\":");
  bole_buf_append_json_string(&params, session_id);
  bole_buf_appendf(&params, ",\"turn\":%u,\"path\":", turn);
  bole_buf_append_json_string(&params, path);
  bole_buf_append_cstr(&params, "}");
  bole_response response;
  code = send_request(handle, "session/trace", params.data,
                      BOLE_TRACE_TIMEOUT_MS, &response);
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
  handle_drop(handle);
  return code;
}

int32_t BOLE_CALL bole_last_error(bole_handle* handle, int32_t* out_code,
                                  char* buffer, size_t buffer_len) {
  if (handle == NULL) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (buffer == NULL && buffer_len > 0) {
    return BOLE_E_INVALID_CONFIG;
  }
  if (!handle_acquire(handle)) {
    return BOLE_E_RUNTIME_EXITED;
  }
  bole_mutex_lock(&handle->state_lock);
  int32_t code = handle->last_code;
  char message[512];
  memcpy(message, handle->last_message, sizeof(message));
  bole_mutex_unlock(&handle->state_lock);
  handle_drop(handle);

  if (out_code != NULL) {
    *out_code = code;
  }
  if (buffer != NULL && buffer_len > 0) {
    strncpy(buffer, message, buffer_len - 1);
    buffer[buffer_len - 1] = '\0';
  }
  return BOLE_OK;
}
