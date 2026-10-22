/**
 * @file rpc.h
 * @brief Request/response correlation for the newline-delimited JSON-RPC pipe.
 */

#ifndef BOLE_RPC_H
#define BOLE_RPC_H

#include "platform.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bole_pending bole_pending;

typedef struct bole_rpc {
  bole_mutex lock;
  uint64_t next_id;
  bole_pending* head;
  int closed;
} bole_rpc;

typedef struct bole_response {
  int32_t code;
  char message[512];
  char* result;
  size_t result_len;
} bole_response;

int bole_rpc_init(bole_rpc* rpc);
void bole_rpc_destroy(bole_rpc* rpc);
uint64_t bole_rpc_next_id(bole_rpc* rpc);
bole_pending* bole_rpc_begin(bole_rpc* rpc, uint64_t id);
/** Wait for the response. Returns 0 on response, 1 on timeout. */
int bole_rpc_wait(bole_pending* pending, int timeout_ms, bole_response* response);
void bole_rpc_end(bole_rpc* rpc, bole_pending* pending);
void bole_rpc_complete(bole_rpc* rpc, uint64_t id, int32_t code,
                       const char* message, const char* result, size_t result_len);
void bole_rpc_close(bole_rpc* rpc);
void bole_response_free(bole_response* response);

#ifdef __cplusplus
}
#endif

#endif /* BOLE_RPC_H */
