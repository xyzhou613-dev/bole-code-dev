/**
 * @file error.h
 * @brief Wire error-code mapping and human-readable text.
 */

#ifndef BOLE_ERROR_H
#define BOLE_ERROR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Map a stable wire `error.data.code` string and JSON-RPC code to a C code. */
int32_t bole_error_from_wire(const char* wire_code, int rpc_code);

/** Human-readable English text for a C error code. */
const char* bole_error_text(int32_t code);

#ifdef __cplusplus
}
#endif

#endif /* BOLE_ERROR_H */
