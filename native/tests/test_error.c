/**
 * @file test_error.c
 * @brief Unit tests for the stable-string -> numeric ABI mapping (04 §4).
 */

#include "bole_agent.h"
#include "error.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(actual, expected)                                                \
  do {                                                                         \
    int32_t value = (int32_t)(actual);                                         \
    int32_t want = (int32_t)(expected);                                        \
    if (value != want) {                                                       \
      fprintf(stderr, "FAIL %s:%d: got %d, want %d\n", __FILE__, __LINE__,     \
              (int)value, (int)want);                                          \
      g_failures++;                                                            \
    }                                                                          \
  } while (0)

int main(void) {
  CHECK(bole_error_from_wire("PRESET_NOT_FOUND", -32603), BOLE_E_PRESET_NOT_FOUND);
  CHECK(bole_error_from_wire("PRESET_LOCKED", -32603), BOLE_E_PRESET_LOCKED);
  CHECK(bole_error_from_wire("SESSION_EXISTS", -32603), BOLE_E_SESSION_EXISTS);
  CHECK(bole_error_from_wire("SESSION_NOT_FOUND", -32603), BOLE_E_SESSION_NOT_FOUND);
  CHECK(bole_error_from_wire("SESSION_NOT_RESUMABLE", -32603),
        BOLE_E_SESSION_NOT_RESUMABLE);
  CHECK(bole_error_from_wire("PERSISTENCE_UNAVAILABLE", -32603),
        BOLE_E_PERSISTENCE_UNAVAILABLE);
  CHECK(bole_error_from_wire("RSI_UNAVAILABLE", -32603), BOLE_E_RSI_UNAVAILABLE);
  CHECK(bole_error_from_wire("SANDBOX_UNAVAILABLE", -32603),
        BOLE_E_SANDBOX_UNAVAILABLE);
  CHECK(bole_error_from_wire("NO_ACTIVE_TASK", -32603), BOLE_E_NO_ACTIVE_TASK);
  CHECK(bole_error_from_wire("CANCEL_FAILED", -32603), BOLE_E_CANCEL_FAILED);
  CHECK(bole_error_from_wire("TRACE_WRITE_FAILED", -32603),
        BOLE_E_RUNTIME_REQUEST_FAILED);
  CHECK(bole_error_from_wire("LINE_TOO_LONG", -32600), BOLE_E_LINE_TOO_LONG);
  CHECK(bole_error_from_wire("METHOD_NOT_FOUND", -32601),
        BOLE_E_PROTOCOL_PARSE_FAILED);
  CHECK(bole_error_from_wire("PROTOCOL_PARSE_FAILED", -32700),
        BOLE_E_PROTOCOL_PARSE_FAILED);

  /* -32600 is a malformed request object (1002); -32602 is a bad parameter. */
  CHECK(bole_error_from_wire("INVALID_ARGUMENT", -32600), BOLE_E_INVALID_CONFIG);
  CHECK(bole_error_from_wire("INVALID_ARGUMENT", -32602), BOLE_E_INVALID_ARGUMENT);
  CHECK(bole_error_from_wire("INVALID_ARGUMENT", -32603), BOLE_E_INVALID_ARGUMENT);

  /* Unknown stable names fall back to the documented interval. */
  CHECK(bole_error_from_wire("1009", -32603), BOLE_E_INVALID_ARGUMENT);
  CHECK(bole_error_from_wire("2099", -32603), BOLE_E_PROTOCOL_PARSE_FAILED);
  CHECK(bole_error_from_wire("3999", -32603), BOLE_E_RUNTIME_REQUEST_FAILED);
  CHECK(bole_error_from_wire("4999", -32603), BOLE_E_SESSION_NOT_FOUND);
  CHECK(bole_error_from_wire("5999", -32603), BOLE_E_MODEL_REQUEST_FAILED);
  CHECK(bole_error_from_wire("6999", -32603), BOLE_E_TOOL_FAILED);
  CHECK(bole_error_from_wire("7999", -32603), BOLE_E_CANCEL_FAILED);

  /* Unknown non-interval names use the JSON-RPC code. */
  CHECK(bole_error_from_wire("WHATEVER", -32700), BOLE_E_PROTOCOL_PARSE_FAILED);
  CHECK(bole_error_from_wire("WHATEVER", -32600), BOLE_E_INVALID_ARGUMENT);
  CHECK(bole_error_from_wire("WHATEVER", -32602), BOLE_E_INVALID_ARGUMENT);
  CHECK(bole_error_from_wire("WHATEVER", -32603), BOLE_E_RUNTIME_REQUEST_FAILED);
  CHECK(bole_error_from_wire(NULL, -32603), BOLE_E_RUNTIME_REQUEST_FAILED);

  if (bole_error_text(BOLE_E_SESSION_EXISTS) == NULL) {
    fprintf(stderr, "FAIL: error text is NULL\n");
    g_failures++;
  }
  if (g_failures != 0) {
    fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  printf("error mapping tests passed\n");
  return 0;
}
