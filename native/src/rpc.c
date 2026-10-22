#include "rpc.h"

#include <stdlib.h>
#include <string.h>

struct bole_pending {
  uint64_t id;
  bole_rpc* rpc;
  bole_cond cond;
  int done;
  int32_t code;
  char message[512];
  char* result;
  size_t result_len;
  struct bole_pending* next;
};

int bole_rpc_init(bole_rpc* rpc) {
  if (rpc == NULL) {
    return -1;
  }
  memset(rpc, 0, sizeof(*rpc));
  if (bole_mutex_init(&rpc->lock) != 0) {
    return -1;
  }
  rpc->next_id = 1;
  return 0;
}

void bole_rpc_destroy(bole_rpc* rpc) {
  if (rpc == NULL) {
    return;
  }
  bole_rpc_close(rpc);
  bole_mutex_destroy(&rpc->lock);
}

uint64_t bole_rpc_next_id(bole_rpc* rpc) {
  bole_mutex_lock(&rpc->lock);
  uint64_t id = rpc->next_id++;
  bole_mutex_unlock(&rpc->lock);
  return id;
}

bole_pending* bole_rpc_begin(bole_rpc* rpc, uint64_t id) {
  bole_pending* pending = (bole_pending*)calloc(1, sizeof(bole_pending));
  if (pending == NULL) {
    return NULL;
  }
  pending->id = id;
  pending->rpc = rpc;
  if (bole_cond_init(&pending->cond) != 0) {
    free(pending);
    return NULL;
  }
  bole_mutex_lock(&rpc->lock);
  if (rpc->closed) {
    bole_mutex_unlock(&rpc->lock);
    bole_cond_destroy(&pending->cond);
    free(pending);
    return NULL;
  }
  pending->next = rpc->head;
  rpc->head = pending;
  bole_mutex_unlock(&rpc->lock);
  return pending;
}

static void remove_pending(bole_rpc* rpc, bole_pending* pending) {
  bole_pending** link = &rpc->head;
  while (*link != NULL) {
    if (*link == pending) {
      *link = pending->next;
      pending->next = NULL;
      return;
    }
    link = &(*link)->next;
  }
}

int bole_rpc_wait(bole_pending* pending, int timeout_ms, bole_response* response) {
  if (pending == NULL || response == NULL) {
    return -1;
  }
  memset(response, 0, sizeof(*response));
  bole_rpc* rpc = pending->rpc;
  bole_mutex_lock(&rpc->lock);
  while (!pending->done && !rpc->closed) {
    int wait = bole_cond_wait(&pending->cond, &rpc->lock, timeout_ms);
    if (wait == 1) {
      remove_pending(rpc, pending);
      pending->done = 1;
      pending->code = 2003;
      strcpy(pending->message, "request timed out");
      break;
    }
    if (wait < 0) {
      remove_pending(rpc, pending);
      pending->done = 1;
      pending->code = 2002;
      strcpy(pending->message, "request wait failed");
      break;
    }
  }
  if (!pending->done) {
    remove_pending(rpc, pending);
    pending->done = 1;
    pending->code = 3002;
    strcpy(pending->message, "runtime closed");
  }
  response->code = pending->code;
  strncpy(response->message, pending->message, sizeof(response->message) - 1);
  response->message[sizeof(response->message) - 1] = '\0';
  response->result = pending->result;
  response->result_len = pending->result_len;
  pending->result = NULL;
  pending->result_len = 0;
  int result = response->code == 2003 ? 1 : 0;
  bole_mutex_unlock(&rpc->lock);
  return result;
}

void bole_rpc_end(bole_rpc* rpc, bole_pending* pending) {
  (void)rpc;
  if (pending == NULL) {
    return;
  }
  bole_cond_destroy(&pending->cond);
  free(pending);
}

void bole_rpc_complete(bole_rpc* rpc, uint64_t id, int32_t code,
                       const char* message, const char* result, size_t result_len) {
  bole_mutex_lock(&rpc->lock);
  bole_pending* pending = rpc->head;
  while (pending != NULL) {
    if (pending->id == id) {
      break;
    }
    pending = pending->next;
  }
  if (pending != NULL) {
    remove_pending(rpc, pending);
    pending->code = code;
    if (message != NULL) {
      strncpy(pending->message, message, sizeof(pending->message) - 1);
      pending->message[sizeof(pending->message) - 1] = '\0';
    }
    if (result != NULL && result_len > 0) {
      pending->result = (char*)malloc(result_len + 1);
      if (pending->result != NULL) {
        memcpy(pending->result, result, result_len);
        pending->result[result_len] = '\0';
        pending->result_len = result_len;
      }
    }
    pending->done = 1;
    bole_cond_signal(&pending->cond);
  }
  bole_mutex_unlock(&rpc->lock);
}

void bole_rpc_close(bole_rpc* rpc) {
  bole_mutex_lock(&rpc->lock);
  rpc->closed = 1;
  bole_pending* pending = rpc->head;
  rpc->head = NULL;
  while (pending != NULL) {
    bole_pending* next = pending->next;
    pending->next = NULL;
    pending->code = 3002;
    strcpy(pending->message, "runtime closed");
    pending->done = 1;
    bole_cond_signal(&pending->cond);
    pending = next;
  }
  bole_mutex_unlock(&rpc->lock);
}

void bole_response_free(bole_response* response) {
  if (response != NULL) {
    free(response->result);
    response->result = NULL;
    response->result_len = 0;
  }
}
