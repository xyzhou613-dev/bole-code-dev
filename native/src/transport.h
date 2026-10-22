/**
 * @file transport.h
 * @brief Newline-delimited JSON transport over the runtime's stdio pipes.
 */

#ifndef BOLE_TRANSPORT_H
#define BOLE_TRANSPORT_H

#include "platform.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bole_transport {
  bole_process* process;
  bole_thread read_thread;
  bole_thread stderr_thread;
  bole_mutex write_lock;
  size_t max_line;
  volatile int stop;
  int started;
  void* user;
  void (*on_line)(void* user, const char* line, size_t len);
  void (*on_stderr_line)(void* user, const char* line, size_t len);
  void (*on_eof)(void* user);
  void (*on_error)(void* user, int32_t code, const char* message);
} bole_transport;

typedef void (*bole_transport_line_fn)(void* user, const char* line, size_t len);
typedef void (*bole_transport_eof_fn)(void* user);
typedef void (*bole_transport_error_fn)(void* user, int32_t code, const char* message);

int bole_transport_start(bole_transport* transport, bole_process* process,
                         size_t max_line, void* user,
                         bole_transport_line_fn on_line,
                         bole_transport_line_fn on_stderr_line,
                         bole_transport_eof_fn on_eof,
                         bole_transport_error_fn on_error);

/** Stop the reader threads and wake a blocked read. Idempotent. */
void bole_transport_stop(bole_transport* transport);

/** Write one line (a trailing newline is added). Returns 0, -1 on I/O error,
 *  or -2 when the encoded line exceeds max_line. */
int bole_transport_send(bole_transport* transport, const char* data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* BOLE_TRANSPORT_H */
