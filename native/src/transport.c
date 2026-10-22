#include "transport.h"
#include "json.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <poll.h>
#  include <unistd.h>
#endif

#define BOLE_STDERR_LINE_MAX (64u * 1024u)

static int write_all(bole_transport* transport, const char* data, size_t len);

/**
 * Tell the peer why the frame was refused, then stop. The runtime is expected
 * to exit after a line-limit violation (02 §1); this best-effort response only
 * makes the cause visible if it does not.
 */
static void reject_line_too_long(bole_transport* transport) {
  static const char RESPONSE[] =
      "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32600,"
      "\"message\":\"line too long\",\"data\":{\"code\":\"LINE_TOO_LONG\"}}}";
  bole_mutex_lock(&transport->write_lock);
  write_all(transport, RESPONSE, sizeof(RESPONSE) - 1);
  write_all(transport, "\n", 1);
  bole_mutex_unlock(&transport->write_lock);
  bole_atomic_store(&transport->stop, 1);
}

static int read_some(bole_transport* transport, char* buffer, size_t capacity) {
  if (bole_atomic_load(&transport->stop)) {
    return -2;
  }
#ifdef _WIN32
  DWORD read = 0;
  if (!ReadFile(transport->process->stdout_read, buffer, (DWORD)capacity, &read, NULL)) {
    DWORD error = GetLastError();
    if (error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF) {
      return 0;
    }
    if (error == ERROR_OPERATION_ABORTED) {
      return -2;
    }
    return -1;
  }
  return (int)read;
#else
  struct pollfd pfd;
  pfd.fd = transport->process->stdout_fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  int ready = poll(&pfd, 1, 100);
  if (ready == 0) {
    return -2;
  }
  if (ready < 0) {
    return errno == EINTR ? -2 : -1;
  }
  for (;;) {
    ssize_t count = read(transport->process->stdout_fd, buffer, capacity);
    if (count > 0) {
      return (int)count;
    }
    if (count == 0) {
      return 0;
    }
    if (errno == EINTR) {
      return -2;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return -2;
    }
    return -1;
  }
#endif
}

static void deliver_line(bole_transport* transport, char* line, size_t len) {
  if (len > 0 && line[len - 1] == '\r') {
    len--;
  }
  if (len > transport->max_line) {
    reject_line_too_long(transport);
    if (transport->on_error != NULL) {
      transport->on_error(transport->user, 2005, "JSON-RPC line exceeds max_line_bytes");
    }
    bole_atomic_store(&transport->stop, 1);
    return;
  }
  if (len > 0 && transport->on_line != NULL) {
    transport->on_line(transport->user, line, len);
  }
}

static void read_main(void* arg) {
  bole_transport* transport = (bole_transport*)arg;
  bole_buf pending;
  if (bole_buf_init(&pending, 8192) != 0) {
    if (transport->on_error != NULL) {
      transport->on_error(transport->user, 2001, "out of memory");
    }
    return;
  }
  char chunk[64 * 1024];
  for (;;) {
    if (bole_atomic_load(&transport->stop)) {
      break;
    }
    int count = read_some(transport, chunk, sizeof(chunk));
    if (count == -2) {
      continue;
    }
    if (count < 0) {
      if (!bole_atomic_load(&transport->stop) && transport->on_error != NULL) {
        transport->on_error(transport->user, 2001, "runtime stdout read failed");
      }
      break;
    }
    if (count == 0) {
      if (pending.len > 0 && transport->on_error != NULL) {
        transport->on_error(transport->user, 2002, "unterminated JSON-RPC line");
      }
      if (transport->on_eof != NULL) {
        transport->on_eof(transport->user);
      }
      break;
    }
    if (bole_buf_append(&pending, chunk, (size_t)count) != 0) {
      if (transport->on_error != NULL) {
        transport->on_error(transport->user, 2001, "out of memory");
      }
      break;
    }

    size_t consumed = 0;
    for (;;) {
      char* start = pending.data + consumed;
      size_t remaining = pending.len - consumed;
      char* newline = (char*)memchr(start, '\n', remaining);
      if (newline == NULL) {
        break;
      }
      size_t line_len = (size_t)(newline - start);
      deliver_line(transport, start, line_len);
      consumed += line_len + 1;
      if (bole_atomic_load(&transport->stop)) {
        break;
      }
    }
    if (bole_atomic_load(&transport->stop)) {
      break;
    }
    if (consumed > 0) {
      memmove(pending.data, pending.data + consumed, pending.len - consumed);
      pending.len -= consumed;
      pending.data[pending.len] = '\0';
    }
    if (pending.len > transport->max_line) {
      reject_line_too_long(transport);
      if (transport->on_error != NULL) {
        transport->on_error(transport->user, 2005, "JSON-RPC line exceeds max_line_bytes");
      }
      break;
    }
  }
  bole_buf_free(&pending);
}

static void stderr_main(void* arg) {
  bole_transport* transport = (bole_transport*)arg;
  bole_buf pending;
  if (bole_buf_init(&pending, 4096) != 0) {
    return;
  }
  char chunk[4096];
  for (;;) {
    if (bole_atomic_load(&transport->stop)) {
      break;
    }
#ifdef _WIN32
    DWORD read = 0;
    if (!ReadFile(transport->process->stderr_read, chunk, (DWORD)sizeof(chunk), &read, NULL)) {
      break;
    }
    if (read == 0) {
      break;
    }
    int count = (int)read;
#else
    struct pollfd pfd;
    pfd.fd = transport->process->stderr_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int ready = poll(&pfd, 1, 100);
    if (ready == 0) {
      continue;
    }
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    ssize_t read_count = read(transport->process->stderr_fd, chunk, sizeof(chunk));
    if (read_count <= 0) {
      if (read_count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
        continue;
      }
      break;
    }
    int count = (int)read_count;
#endif
    if (bole_buf_append(&pending, chunk, (size_t)count) != 0) {
      break;
    }
    size_t consumed = 0;
    for (;;) {
      char* start = pending.data + consumed;
      size_t remaining = pending.len - consumed;
      char* newline = (char*)memchr(start, '\n', remaining);
      if (newline == NULL) {
        break;
      }
      size_t line_len = (size_t)(newline - start);
      if (line_len > 0 && start[line_len - 1] == '\r') {
        line_len--;
      }
      if (transport->on_stderr_line != NULL) {
        transport->on_stderr_line(transport->user, start, line_len);
      }
      consumed += (size_t)(newline - start) + 1;
    }
    if (consumed > 0) {
      memmove(pending.data, pending.data + consumed, pending.len - consumed);
      pending.len -= consumed;
      pending.data[pending.len] = '\0';
    }
    if (pending.len > BOLE_STDERR_LINE_MAX && transport->on_stderr_line != NULL) {
      transport->on_stderr_line(transport->user, pending.data, pending.len);
      bole_buf_reset(&pending);
    }
  }
  bole_buf_free(&pending);
}

int bole_transport_start(bole_transport* transport, bole_process* process,
                         size_t max_line, void* user,
                         bole_transport_line_fn on_line,
                         bole_transport_line_fn on_stderr_line,
                         bole_transport_eof_fn on_eof,
                         bole_transport_error_fn on_error) {
  if (transport == NULL || process == NULL || max_line == 0) {
    return -1;
  }
  memset(transport, 0, sizeof(*transport));
  transport->process = process;
  transport->max_line = max_line;
  transport->user = user;
  transport->on_line = on_line;
  transport->on_stderr_line = on_stderr_line;
  transport->on_eof = on_eof;
  transport->on_error = on_error;
  if (bole_mutex_init(&transport->write_lock) != 0) {
    return -1;
  }
  if (bole_thread_start(&transport->read_thread, read_main, transport) != 0) {
    bole_mutex_destroy(&transport->write_lock);
    return -1;
  }
  if (bole_thread_start(&transport->stderr_thread, stderr_main, transport) != 0) {
    bole_atomic_store(&transport->stop, 1);
    bole_thread_join(&transport->read_thread);
    bole_mutex_destroy(&transport->write_lock);
    return -1;
  }
  transport->started = 1;
  return 0;
}

void bole_transport_stop(bole_transport* transport) {
  if (transport == NULL || !transport->started) {
    return;
  }
  bole_atomic_store(&transport->stop, 1);
#ifdef _WIN32
  if (transport->process != NULL) {
    CancelIoEx(transport->process->stdout_read, NULL);
    CancelIoEx(transport->process->stderr_read, NULL);
  }
#endif
  bole_thread_join(&transport->read_thread);
  bole_thread_join(&transport->stderr_thread);
  bole_mutex_destroy(&transport->write_lock);
  transport->started = 0;
}

static int write_all(bole_transport* transport, const char* data, size_t len) {
#ifdef _WIN32
  size_t offset = 0;
  while (offset < len) {
    DWORD written = 0;
    if (!WriteFile(transport->process->stdin_write, data + offset,
                   (DWORD)(len - offset), &written, NULL)) {
      return -1;
    }
    if (written == 0) {
      return -1;
    }
    offset += written;
  }
  return 0;
#else
  size_t offset = 0;
  while (offset < len) {
    ssize_t written = write(transport->process->stdin_fd, data + offset, len - offset);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
    if (written == 0) {
      return -1;
    }
    offset += (size_t)written;
  }
  return 0;
#endif
}

int bole_transport_send(bole_transport* transport, const char* data, size_t len) {
  if (transport == NULL || data == NULL || !transport->started || bole_atomic_load(&transport->stop)) {
    return -1;
  }
  if (len + 1 > transport->max_line) {
    return -2;
  }
  int result = -1;
  bole_mutex_lock(&transport->write_lock);
  if (write_all(transport, data, len) == 0 && write_all(transport, "\n", 1) == 0) {
    result = 0;
  }
  bole_mutex_unlock(&transport->write_lock);
  return result;
}
