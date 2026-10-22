/**
 * @file json.h
 * @brief Minimal JSON scanner and builder used by the Bole Agent SDK.
 *
 * The scanner validates a complete JSON text and indexes the members of its
 * top-level object. Nested objects are validated but not indexed; call
 * bole_json_parse_object on a member value to index a nested object. There is
 * no DOM allocation: members point into the caller's buffer.
 */

#ifndef BOLE_JSON_H
#define BOLE_JSON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bole_json_type {
  BOLE_JSON_NULL = 0,
  BOLE_JSON_BOOL,
  BOLE_JSON_NUMBER,
  BOLE_JSON_STRING,
  BOLE_JSON_OBJECT,
  BOLE_JSON_ARRAY
} bole_json_type;

typedef struct bole_json_member {
  /** Key bytes without the surrounding quotes (escapes are not decoded). */
  const char* key;
  size_t key_len;
  /** Raw value span; strings include their quotes, objects include braces. */
  const char* value;
  size_t value_len;
  bole_json_type type;
} bole_json_member;

/**
 * Validate one JSON text and index its top-level object members.
 *
 * @param text     UTF-8 JSON text (no embedded NUL requirement).
 * @param len      Text length in bytes.
 * @param members  Output array; may be NULL only when capacity is 0.
 * @param capacity Maximum members to store.
 * @param out_count Receives the number of top-level members (may exceed capacity).
 * @return 0 on success; -1 invalid JSON; -2 too many members; -3 not an object.
 */
int bole_json_parse_object(const char* text, size_t len,
                           bole_json_member* members, size_t capacity,
                           size_t* out_count);

/** Validate a complete JSON value (any type). Returns 0 or -1. */
int bole_json_validate(const char* text, size_t len);

/** Find a top-level member by exact (undecoded) key. Returns NULL when absent. */
const bole_json_member* bole_json_member_get(const bole_json_member* members,
                                             size_t count, const char* key);

/** Decode a JSON string member into UTF-8. Returns 0, -1 invalid, -2 buffer too small. */
int bole_json_string(const bole_json_member* member,
                     char* out, size_t capacity, size_t* out_len);

/** Copy a raw value span into a NUL-terminated buffer. Returns 0 or -2. */
int bole_json_raw(const bole_json_member* member,
                  char* out, size_t capacity, size_t* out_len);

/** Parse a number member as uint64. Returns 0 or -1. */
int bole_json_u64(const bole_json_member* member, uint64_t* out);

/** Parse a number member as int64. Returns 0 or -1. */
int bole_json_i64(const bole_json_member* member, int64_t* out);

/** Parse a bool member. Returns 0 or -1. */
int bole_json_bool(const bole_json_member* member, int* out);

/** Growable NUL-terminated byte buffer. */
typedef struct bole_buf {
  char* data;
  size_t len;
  size_t cap;
} bole_buf;

int bole_buf_init(bole_buf* buf, size_t initial);
void bole_buf_free(bole_buf* buf);
void bole_buf_reset(bole_buf* buf);
int bole_buf_append(bole_buf* buf, const char* data, size_t len);
int bole_buf_append_cstr(bole_buf* buf, const char* text);
int bole_buf_append_char(bole_buf* buf, char c);
int bole_buf_appendf(bole_buf* buf, const char* format, ...);
int bole_buf_append_json_string(bole_buf* buf, const char* text);

#ifdef __cplusplus
}
#endif

#endif /* BOLE_JSON_H */
