#include "error.h"
#include "bole_agent.h"

#include <string.h>

typedef struct error_entry {
  const char* wire;
  int32_t code;
} error_entry;

static const error_entry ERROR_TABLE[] = {
  {"PRESET_NOT_FOUND", BOLE_E_PRESET_NOT_FOUND},
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

/* Unknown stable names are grouped by their documented 1xxx..7xxx interval
 * (04 §4). EXACT entries above win because they carry a more specific code. */
static int32_t bole_error_from_interval(const char* wire_code) {
  if (wire_code == NULL || wire_code[0] < '1' || wire_code[0] > '7') {
    return 0;
  }
  for (int i = 1; i < 4; i++) {
    if (wire_code[i] < '0' || wire_code[i] > '9') {
      return 0;
    }
  }
  if (wire_code[4] != '\0') {
    return 0;
  }
  switch (wire_code[0]) {
    case '1': return BOLE_E_INVALID_ARGUMENT;
    case '2': return BOLE_E_PROTOCOL_PARSE_FAILED;
    case '3': return BOLE_E_RUNTIME_REQUEST_FAILED;
    case '4': return BOLE_E_SESSION_NOT_FOUND;
    case '5': return BOLE_E_MODEL_REQUEST_FAILED;
    case '6': return BOLE_E_TOOL_FAILED;
    case '7': return BOLE_E_CANCEL_FAILED;
    default: return 0;
  }
}

int32_t bole_error_from_wire(const char* wire_code, int rpc_code) {
  if (wire_code != NULL) {
    /* INVALID_ARGUMENT appears at two ABI numbers: 1002 for a malformed request
     * object (-32600) and 1006 for a bad parameter (-32602). */
    if (strcmp(wire_code, "INVALID_ARGUMENT") == 0) {
      return rpc_code == -32600 ? BOLE_E_INVALID_CONFIG : BOLE_E_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < sizeof(ERROR_TABLE) / sizeof(ERROR_TABLE[0]); i++) {
      if (strcmp(wire_code, ERROR_TABLE[i].wire) == 0) {
        return ERROR_TABLE[i].code;
      }
    }
    int32_t interval = bole_error_from_interval(wire_code);
    if (interval != 0) {
      return interval;
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
