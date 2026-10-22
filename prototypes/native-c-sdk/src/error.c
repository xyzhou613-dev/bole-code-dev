#include "error.h"
#include "bole_agent.h"

#include <string.h>

typedef struct error_entry {
  const char* wire;
  int32_t code;
} error_entry;

static const error_entry ERROR_TABLE[] = {
  {"PRESET_NOT_FOUND", BOLE_E_PRESET_NOT_FOUND},
  {"INVALID_ARGUMENT", BOLE_E_INVALID_ARGUMENT},
  {"PRESET_LOCKED", BOLE_E_PRESET_LOCKED},
  {"SESSION_EXISTS", BOLE_E_SESSION_EXISTS},
  {"SESSION_NOT_FOUND", BOLE_E_SESSION_NOT_FOUND},
  {"SESSION_NOT_RESUMABLE", BOLE_E_SESSION_NOT_RESUMABLE},
  {"PERSISTENCE_UNAVAILABLE", BOLE_E_PERSISTENCE_UNAVAILABLE},
  {"RSI_UNAVAILABLE", BOLE_E_RSI_UNAVAILABLE},
  {"SANDBOX_UNAVAILABLE", BOLE_E_SANDBOX_UNAVAILABLE},
  {"NO_ACTIVE_TASK", BOLE_E_NO_ACTIVE_TASK},
  {"CANCEL_FAILED", BOLE_E_CANCEL_FAILED},
  {"TRACE_WRITE_FAILED", BOLE_E_RUNTIME_REQUEST_FAILED},
  {"LINE_TOO_LONG", BOLE_E_LINE_TOO_LONG},
  {"METHOD_NOT_FOUND", BOLE_E_PROTOCOL_PARSE_FAILED},
  {"PROTOCOL_PARSE_FAILED", BOLE_E_PROTOCOL_PARSE_FAILED},
  {"MODEL_AUTH_FAILED", BOLE_E_MODEL_AUTH_FAILED},
  {"MODEL_RATE_LIMITED", BOLE_E_MODEL_RATE_LIMITED},
  {"MODEL_CONTEXT_EXCEEDED", BOLE_E_MODEL_CONTEXT_EXCEEDED},
  {"MODEL_NOT_FOUND", BOLE_E_MODEL_NOT_FOUND},
  {"MODEL_REQUEST_FAILED", BOLE_E_MODEL_REQUEST_FAILED},
  {"TOOL_NOT_FOUND", BOLE_E_TOOL_NOT_FOUND},
  {"TOOL_TIMEOUT", BOLE_E_TOOL_TIMEOUT},
  {"TOOL_CRASHED", BOLE_E_TOOL_CRASHED},
  {"TOOL_FAILED", BOLE_E_TOOL_FAILED},
};

int32_t bole_error_from_wire(const char* wire_code, int rpc_code) {
  if (wire_code != NULL) {
    for (size_t i = 0; i < sizeof(ERROR_TABLE) / sizeof(ERROR_TABLE[0]); i++) {
      if (strcmp(wire_code, ERROR_TABLE[i].wire) == 0) {
        return ERROR_TABLE[i].code;
      }
    }
  }
  switch (rpc_code) {
    case -32700:
    case -32601:
      return BOLE_E_PROTOCOL_PARSE_FAILED;
    case -32600:
    case -32602:
      return BOLE_E_INVALID_ARGUMENT;
    case -32603:
    default:
      return BOLE_E_RUNTIME_REQUEST_FAILED;
  }
}

const char* bole_error_text(int32_t code) {
  switch (code) {
    case BOLE_OK: return "ok";
    case BOLE_E_RUNTIME_DIR_NOT_FOUND: return "runtime directory or entry not found";
    case BOLE_E_INVALID_CONFIG: return "invalid configuration";
    case BOLE_E_EVENT_CALLBACK_REQUIRED: return "event callback is required";
    case BOLE_E_PRESET_NOT_FOUND: return "preset not found";
    case BOLE_E_CONFIG_PARSE_FAILED: return "configuration parse failed";
    case BOLE_E_INVALID_ARGUMENT: return "invalid argument";
    case BOLE_E_PIPE_FAILED: return "pipe failed";
    case BOLE_E_PROTOCOL_PARSE_FAILED: return "protocol parse failed";
    case BOLE_E_REQUEST_TIMEOUT: return "request timed out";
    case BOLE_E_WAIT_READY_TIMEOUT: return "wait_ready timed out";
    case BOLE_E_LINE_TOO_LONG: return "JSON-RPC line too long";
    case BOLE_E_STDOUT_VIOLATION: return "stdout is not JSON-RPC";
    case BOLE_E_EVENT_QUEUE_OVERFLOW: return "event queue overflow";
    case BOLE_E_PROTOCOL_VERSION_MISMATCH: return "protocol version mismatch";
    case BOLE_E_RUNTIME_START_FAILED: return "runtime failed to start";
    case BOLE_E_RUNTIME_EXITED: return "runtime exited";
    case BOLE_E_RUNTIME_VERSION_MISMATCH: return "runtime version mismatch";
    case BOLE_E_RSI_UNAVAILABLE: return "RSI unavailable: no preset plane";
    case BOLE_E_SANDBOX_UNAVAILABLE: return "sandbox backend unavailable";
    case BOLE_E_PERSISTENCE_UNAVAILABLE: return "session persistence unavailable";
    case BOLE_E_RUNTIME_REQUEST_FAILED: return "runtime request failed";
    case BOLE_E_SESSION_NOT_FOUND: return "session not found";
    case BOLE_E_SESSION_EXISTS: return "session already exists";
    case BOLE_E_PRESET_LOCKED: return "preset locked after the first turn";
    case BOLE_E_SESSION_NOT_RESUMABLE: return "session not resumable";
    case BOLE_E_MODEL_AUTH_FAILED: return "model authentication failed";
    case BOLE_E_MODEL_RATE_LIMITED: return "model rate limited";
    case BOLE_E_MODEL_CONTEXT_EXCEEDED: return "model context exceeded";
    case BOLE_E_MODEL_NOT_FOUND: return "model not found";
    case BOLE_E_MODEL_REQUEST_FAILED: return "model request failed";
    case BOLE_E_TOOL_NOT_FOUND: return "tool not found";
    case BOLE_E_TOOL_TIMEOUT: return "tool timed out";
    case BOLE_E_TOOL_CRASHED: return "tool crashed";
    case BOLE_E_TOOL_FAILED: return "tool failed";
    case BOLE_E_NO_ACTIVE_TASK: return "no active task to cancel";
    case BOLE_E_CANCEL_FAILED: return "cancel failed";
    default: return "unknown error";
  }
}
