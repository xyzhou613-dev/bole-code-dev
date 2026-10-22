/**
 * @file bole_agent.h
 * @brief Bole Agent SDK: C ABI for driving a DeepSeek Harness runtime.
 *
 * The SDK starts one runtime process per @ref bole_handle and talks to it over
 * an anonymous pipe using newline-delimited JSON-RPC. The control plane is
 * synchronous only for short local round trips (session create/resume, prompt
 * enqueue, cancel, preset switch, trace export); no control call waits for a
 * model request or a tool execution.
 *
 * The wire protocol is documented in docs/bole-sdk/02-protocol.md. Event
 * payloads are JSON objects with a `"v": 1` member documented in
 * docs/bole-sdk/03-events.md.
 *
 * ABI rules:
 * - Every public struct starts with `uint32_t size`; callers set it to
 *   `sizeof(struct)`. The SDK reads only bytes inside the declared size and
 *   writes only bytes inside the caller-declared size. New fields are appended.
 * - Strings are UTF-8 without a BOM. Windows paths are converted internally.
 * - Handles and event strings are owned by the SDK; see the lifetime notes on
 *   each type and function.
 * - Error codes are stable positive integers; 0 means success. The wire
 *   `error.data.code` string maps to these numbers (see 04-c-abi.md).
 *
 * Threading:
 * - All functions are thread-safe unless stated otherwise.
 * - The event callback runs on one SDK-owned thread per handle, serial and
 *   FIFO. A callback must return quickly (milliseconds).
 * - Callbacks may call bole_submit, bole_submit_blocks, bole_cancel,
 *   bole_rsi_set, bole_rsi_get, and bole_session_set_preset. They must not call
 *   bole_init, bole_wait_ready, bole_release, or bole_trace_export.
 * - bole_release returns immediately; no new callback starts after it returns,
 *   and one in-flight callback may still finish.
 */

#ifndef BOLE_AGENT_H
#define BOLE_AGENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** ABI version: major in the high 16 bits, minor in the low 16 bits. */
#define BOLE_ABI_VERSION 0x00010000u

/** Maximum preset id length including the terminating NUL. */
#define BOLE_PRESET_MAX 64

#if defined(_WIN32)
#  if defined(BOLE_AGENT_STATIC)
#    define BOLE_API
#  elif defined(BOLE_AGENT_BUILD)
#    define BOLE_API __declspec(dllexport)
#  else
#    define BOLE_API __declspec(dllimport)
#  endif
#  define BOLE_CALL __cdecl
#elif defined(__GNUC__)
#  define BOLE_API __attribute__((visibility("default")))
#  define BOLE_CALL
#else
#  define BOLE_API
#  define BOLE_CALL
#endif

/** Error codes returned by the SDK. All values are positive; 0 is success. */
typedef enum bole_error {
  BOLE_OK = 0,

  /* 1xxx: configuration and local arguments. */
  BOLE_E_RUNTIME_DIR_NOT_FOUND = 1001,
  BOLE_E_INVALID_CONFIG = 1002,
  BOLE_E_EVENT_CALLBACK_REQUIRED = 1003,
  BOLE_E_PRESET_NOT_FOUND = 1004,
  BOLE_E_CONFIG_PARSE_FAILED = 1005,
  BOLE_E_INVALID_ARGUMENT = 1006,

  /* 2xxx: transport and protocol. */
  BOLE_E_PIPE_FAILED = 2001,
  BOLE_E_PROTOCOL_PARSE_FAILED = 2002,
  BOLE_E_REQUEST_TIMEOUT = 2003,
  BOLE_E_WAIT_READY_TIMEOUT = 2004,
  BOLE_E_LINE_TOO_LONG = 2005,
  BOLE_E_STDOUT_VIOLATION = 2006,
  BOLE_E_EVENT_QUEUE_OVERFLOW = 2007,
  BOLE_E_PROTOCOL_VERSION_MISMATCH = 2008,

  /* 3xxx: runtime process and capabilities. */
  BOLE_E_RUNTIME_START_FAILED = 3001,
  BOLE_E_RUNTIME_EXITED = 3002,
  BOLE_E_RUNTIME_VERSION_MISMATCH = 3003,
  BOLE_E_RSI_UNAVAILABLE = 3004,
  BOLE_E_SANDBOX_UNAVAILABLE = 3005,
  BOLE_E_PERSISTENCE_UNAVAILABLE = 3006,
  BOLE_E_RUNTIME_REQUEST_FAILED = 3007,

  /* 4xxx: sessions. */
  BOLE_E_SESSION_NOT_FOUND = 4001,
  BOLE_E_SESSION_EXISTS = 4002,
  BOLE_E_PRESET_LOCKED = 4003,
  BOLE_E_SESSION_NOT_RESUMABLE = 4004,

  /* 5xxx: model requests. */
  BOLE_E_MODEL_AUTH_FAILED = 5001,
  BOLE_E_MODEL_RATE_LIMITED = 5002,
  BOLE_E_MODEL_CONTEXT_EXCEEDED = 5003,
  BOLE_E_MODEL_NOT_FOUND = 5004,
  BOLE_E_MODEL_REQUEST_FAILED = 5005,

  /* 6xxx: tools. */
  BOLE_E_TOOL_NOT_FOUND = 6001,
  BOLE_E_TOOL_TIMEOUT = 6002,
  BOLE_E_TOOL_CRASHED = 6003,
  BOLE_E_TOOL_FAILED = 6004,

  /* 7xxx: cancellation. */
  BOLE_E_NO_ACTIVE_TASK = 7001,
  BOLE_E_CANCEL_FAILED = 7002
} bole_error;

/**
 * Event types delivered to @ref bole_event_cb.
 *
 * The numeric values are ABI: they never change. New types are appended in
 * unused ranges; a caller must ignore unknown values instead of failing.
 */
typedef enum bole_event_type {
  BOLE_EVENT_RUNTIME_READY = 1,
  BOLE_EVENT_RUNTIME_FAILED = 2,
  BOLE_EVENT_RUNTIME_EXITED = 3,

  BOLE_EVENT_TASK_STARTED = 10,
  BOLE_EVENT_STEP_STARTED = 11,
  BOLE_EVENT_STEP_FINISHED = 12,
  BOLE_EVENT_PLAN_UPDATED = 13,
  BOLE_EVENT_MODEL_SELECTED = 14,
  BOLE_EVENT_RSI_STATE = 15,

  BOLE_EVENT_TEXT_DELTA = 20,
  BOLE_EVENT_REASONING_DELTA = 21,
  /** Final assistant text/reasoning for one step; deltas are best-effort. */
  BOLE_EVENT_ASSISTANT_MESSAGE = 22,

  BOLE_EVENT_TOOL_CALL_STARTED = 30,
  BOLE_EVENT_TOOL_CALL_FINISHED = 31,

  /** Reserved: this runtime has no verification signal and never emits it. */
  BOLE_EVENT_VERIFY_CHECK = 40,

  BOLE_EVENT_TASK_FINISHED = 50,
  BOLE_EVENT_TASK_FAILED = 51
} bole_event_type;

/**
 * One event delivered to the callback.
 *
 * All pointers are owned by the SDK and are valid only for the duration of the
 * callback. A caller that keeps a value must copy it. `payload_json` is a
 * NUL-terminated UTF-8 JSON object; `session_id` is `""` for runtime-level
 * events. `task`/`step` are 0 when the event is not scoped to one.
 */
typedef struct bole_event {
  /** `sizeof(bole_event)`; lets the SDK recognize newer callers. */
  uint32_t size;
  /** One of @ref bole_event_type. Unknown values must be ignored. */
  uint32_t type;
  /** Monotonic per-handle event sequence for diagnostics and deduplication. */
  uint64_t event_id;
  /** Source session-log sequence; 0 for transient frames and runtime events. */
  uint64_t seq;
  /** Unix epoch milliseconds; non-decreasing per handle. */
  int64_t ts_ms;
  /** Kernel turn number; 0 when not task-scoped. */
  uint32_t task;
  /** Kernel step number; 0 when not step-scoped. */
  uint32_t step;
  /** Session id (agent id has the same value); empty for runtime events. */
  const char* session_id;
  /** Event payload JSON, schema version 1; see docs/bole-sdk/03-events.md. */
  const char* payload_json;
} bole_event;

/** Event callback. Runs on the SDK's event thread; must return quickly. */
typedef void (BOLE_CALL* bole_event_cb)(void* user_data, const bole_event* event);

/** File-effect boundary for the runtime's tool calls. */
typedef enum bole_sandbox_mode {
  /** Use the SDK default: workspace-write. */
  BOLE_SANDBOX_DEFAULT = 0,
  /** Tools may write only under the session cwd. */
  BOLE_SANDBOX_WORKSPACE_WRITE = 1,
  /** No file sandbox; tools may write anywhere the user can. */
  BOLE_SANDBOX_DANGER_FULL_ACCESS = 2
} bole_sandbox_mode;

/**
 * Initialization configuration.
 *
 * The caller sets `size = sizeof(bole_config)`. Required fields: `runtime_dir`,
 * `dsh_home`, `model_provider`, `model_name`, and `event_cb`. Optional fields
 * may be NULL or zero.
 */
typedef struct bole_config {
  /** `sizeof(bole_config)`. */
  uint32_t size;
  /** Directory containing `node.exe` and the bundled dsh dependency tree. */
  const char* runtime_dir;
  /** Writable runtime home; the SDK sets `DSH_HOME` to this directory. */
  const char* dsh_home;
  /** Default session cwd (absolute). NULL uses the calling process cwd. */
  const char* cwd;
  /** Provider route passed to `initialize`, for example `"bole"`. */
  const char* model_provider;
  /** Model id passed to `initialize`, for example `"qwen3.8-27b"`. */
  const char* model_name;
  /** Optional reasoning effort id inherited by agents. */
  const char* reasoning_effort;
  /** Optional output-token cap; 0 uses the runtime default. */
  int32_t max_tokens;
  /** Default RSI for new sessions: 0 = standard, 1 = rsi-infer. */
  int32_t rsi_enabled;
  /** One of @ref bole_sandbox_mode; `BOLE_SANDBOX_DEFAULT` is workspace-write. */
  int32_t sandbox_mode;
  /** Maximum JSON-RPC line size; 0 uses 64 MiB (67108864). */
  uint32_t max_line_bytes;
  /** Required event callback. */
  bole_event_cb event_cb;
  /** Opaque value passed back to `event_cb`. */
  void* user_data;
  /** Optional UTF-8 log file path; NULL disables the file log. */
  const char* log_path;
} bole_config;

/**
 * Per-session creation options. Set `size = sizeof(bole_session_options)`.
 * A NULL options pointer selects the defaults from @ref bole_config.
 */
typedef struct bole_session_options {
  /** `sizeof(bole_session_options)`. */
  uint32_t size;
  /** Absolute session cwd; NULL uses @ref bole_config.cwd. */
  const char* cwd;
  /** Preset id (`"standard"` or `"rsi-infer"`); NULL uses the current default. */
  const char* preset;
} bole_session_options;

/** Snapshot of the default RSI state. Set `size` before calling bole_rsi_get. */
typedef struct bole_rsi_state {
  /** `sizeof(bole_rsi_state)`. */
  uint32_t size;
  /** 1 when the default preset is rsi-infer. */
  int32_t enabled;
  /** 1 when the runtime composed an agent-preset registry; 0 otherwise. */
  int32_t composed;
  /** NUL-terminated preset id; empty when the preset plane is unavailable. */
  char preset[BOLE_PRESET_MAX];
} bole_rsi_state;

/** Opaque runtime handle, one per runtime process. */
typedef struct bole_handle bole_handle;

/** Opaque session handle; invalid after bole_release. */
typedef struct bole_session bole_session;

/**
 * Validate configuration, spawn the runtime process, and return immediately.
 *
 * Readiness is reported by `BOLE_EVENT_RUNTIME_READY` (or
 * `BOLE_EVENT_RUNTIME_FAILED`). Use bole_wait_ready to wait for the handshake.
 * A synchronous validation or spawn failure returns an error and no handle.
 *
 * @param config   Non-NULL configuration with `size` set.
 * @param out_handle Receives the new handle on success; must not be NULL.
 * @return 0, or one of 1001-1006, 3001.
 */
BOLE_API int32_t BOLE_CALL bole_init(const bole_config* config, bole_handle** out_handle);

/**
 * Wait until the runtime handshake completes.
 *
 * Waits only for `initialize`; it never waits for a model or a tool. A timeout
 * of 0 checks the current state once.
 *
 * @param handle     A live handle.
 * @param timeout_ms Timeout in milliseconds.
 * @return 0 when ready; 2004 on timeout; 3001/3002 when the runtime failed or exited.
 */
BOLE_API int32_t BOLE_CALL bole_wait_ready(bole_handle* handle, uint32_t timeout_ms);

/**
 * Replace the event callback.
 *
 * Events already queued are delivered to the new callback. `event_cb` must not
 * be NULL; pass a no-op callback to ignore events.
 *
 * @param handle   A live handle.
 * @param event_cb New callback.
 * @param user_data Opaque value passed to the callback.
 * @return 0, or 1002/1003.
 */
BOLE_API int32_t BOLE_CALL bole_set_event_callback(
    bole_handle* handle, bole_event_cb event_cb, void* user_data);

/**
 * Release the caller's reference to a handle.
 *
 * Returns immediately. No new callback starts after it returns; one in-flight
 * callback may finish. All session handles become invalid. The runtime process
 * and its child tree are reaped in the background.
 *
 * Must not be called from an event callback.
 *
 * @param handle A live handle; NULL returns 1002.
 * @return 0, or 1002.
 */
BOLE_API int32_t BOLE_CALL bole_release(bole_handle* handle);

/**
 * Create a session with an explicit id.
 *
 * Waits for the runtime's local create response (short; no model or tool
 * execution). The id is the agent/session identity and must be unique.
 *
 * @param handle      A live handle.
 * @param session_id  Non-empty UTF-8 id.
 * @param options     Optional cwd/preset; NULL uses defaults.
 * @param out_session Receives the session handle; must not be NULL.
 * @return 0; 1002 invalid argument; 1004 unknown preset; 2003 timeout;
 *         4002 id already exists.
 */
BOLE_API int32_t BOLE_CALL bole_session_create(
    bole_handle* handle, const char* session_id,
    const bole_session_options* options, bole_session** out_session);

/**
 * Resume a persisted session.
 *
 * The preset is restored from the session header. Requires session
 * persistence in the runtime; a missing definition is rejected.
 *
 * @param handle      A live handle.
 * @param session_id  Persisted session id.
 * @param out_session Receives the session handle; must not be NULL.
 * @return 0; 1002; 2003; 3006 persistence unavailable; 4001 not found;
 *         4004 not resumable.
 */
BOLE_API int32_t BOLE_CALL bole_session_resume(
    bole_handle* handle, const char* session_id, bole_session** out_session);

/**
 * Switch the preset of a blank session.
 *
 * Only sessions that have not started their first turn can switch; afterwards
 * the runtime returns BOLE_E_PRESET_LOCKED. Waits for the local response.
 *
 * @param session   A live session.
 * @param preset_id `"standard"` or `"rsi-infer"`.
 * @return 0; 1002; 1004; 2003; 4003 locked.
 */
BOLE_API int32_t BOLE_CALL bole_session_set_preset(
    bole_session* session, const char* preset_id);

/**
 * Enqueue one text user message.
 *
 * Waits only for the runtime to accept the message into the session inbox; the
 * turn runs asynchronously and is reported through events.
 *
 * @param session  A live session.
 * @param utf8_text Non-empty UTF-8 text.
 * @return 0; 1002; 2003; 3002 runtime exited.
 */
BOLE_API int32_t BOLE_CALL bole_submit(bole_session* session, const char* utf8_text);

/**
 * Enqueue one user message from JSON content blocks.
 *
 * `content_blocks_json` is a UTF-8 JSON array using the official SDK block
 * schema: `{"type":"text","text":...}` and
 * `{"type":"image","data":"<base64>","mimeType":"image/png|image/jpeg|image/webp|image/gif"}`.
 * Images are admitted into the runtime's durable attachment store.
 *
 * @param session             A live session.
 * @param content_blocks_json Non-empty JSON array.
 * @return 0; 1002 invalid JSON/argument; 2003; 3002.
 */
BOLE_API int32_t BOLE_CALL bole_submit_blocks(
    bole_session* session, const char* content_blocks_json);

/**
 * Cancel the session's active turn immediately.
 *
 * Maps to the kernel `Agent.cancel`: the model request and started tools are
 * aborted, queued messages are dropped, and the turn ends with
 * `TASK_FINISHED(outcome=aborted)`. No step-end cancellation is supported.
 *
 * @param session A live session.
 * @return 0; 1002; 2003; 7001 when no turn is active.
 */
BOLE_API int32_t BOLE_CALL bole_cancel(bole_session* session);

/**
 * Set the default RSI for new sessions.
 *
 * 1 selects the `rsi-infer` preset, 0 selects `standard`. Running turns keep
 * their preset. Returns BOLE_E_RSI_UNAVAILABLE when the runtime has no preset
 * plane; the state is not changed in that case.
 *
 * @param handle  A live handle.
 * @param enabled 0 or 1.
 * @return 0; 1002; 2003; 3004.
 */
BOLE_API int32_t BOLE_CALL bole_rsi_set(bole_handle* handle, int32_t enabled);

/**
 * Read the default RSI state.
 *
 * Reads the SDK projection; does not contact the runtime. `composed` is 0 when
 * the runtime did not compose an agent-preset registry.
 *
 * @param handle   A live handle.
 * @param out_state Receives the state; `size` must be set.
 * @return 0; 1002.
 */
BOLE_API int32_t BOLE_CALL bole_rsi_get(bole_handle* handle, bole_rsi_state* out_state);

/**
 * Export a session's raw event log as JSONL.
 *
 * `turn` selects one kernel turn; 0 exports the whole session. The runtime
 * writes one raw session event per line. The parent directory must exist.
 *
 * This call waits for the file write (up to 30 seconds) and must not be called
 * from an event callback.
 *
 * @param handle          A live handle.
 * @param session_id      Session id.
 * @param turn            Turn number, or 0 for all.
 * @param path            UTF-8 destination path.
 * @param out_event_count Optional; receives the number of exported events.
 * @return 0; 1002; 2003; 4001.
 */
BOLE_API int32_t BOLE_CALL bole_trace_export(
    bole_handle* handle, const char* session_id, uint32_t turn,
    const char* path, uint32_t* out_event_count);

/**
 * Read the last error of this handle.
 *
 * The error is handle-scoped, not thread-local. `buffer` is always
 * NUL-terminated when `buffer_len > 0`; longer messages are truncated. With no
 * recorded error, `*out_code` is 0 and the buffer is empty.
 *
 * @param handle    A live handle.
 * @param out_code  Optional; receives the numeric error code.
 * @param buffer    Optional text buffer.
 * @param buffer_len Buffer size in bytes.
 * @return 0; 1002 when `buffer_len > 0` but `buffer` is NULL.
 */
BOLE_API int32_t BOLE_CALL bole_last_error(
    bole_handle* handle, int32_t* out_code, char* buffer, size_t buffer_len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BOLE_AGENT_H */
