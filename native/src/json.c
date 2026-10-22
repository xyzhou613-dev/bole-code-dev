#include "json.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOLE_JSON_MAX_DEPTH 64

static void skip_ws(const char* text, size_t len, size_t* index) {
  size_t i = *index;
  while (i < len) {
    char c = text[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      i++;
    } else {
      break;
    }
  }
  *index = i;
}

static int skip_string(const char* text, size_t len, size_t* index) {
  size_t i = *index;
  if (i >= len || text[i] != '"') {
    return -1;
  }
  i++;
  while (i < len) {
    unsigned char c = (unsigned char)text[i];
    if (c == '"') {
      *index = i + 1;
      return 0;
    }
    if (c < 0x20) {
      return -1;
    }
    if (c == '\\') {
      i++;
      if (i >= len) {
        return -1;
      }
      switch (text[i]) {
        case '"':
        case '\\':
        case '/':
        case 'b':
        case 'f':
        case 'n':
        case 'r':
        case 't':
          i++;
          break;
        case 'u': {
          if (i + 4 >= len) {
            return -1;
          }
          for (size_t k = 1; k <= 4; k++) {
            if (!isxdigit((unsigned char)text[i + k])) {
              return -1;
            }
          }
          i += 5;
          break;
        }
        default:
          return -1;
      }
    } else {
      i++;
    }
  }
  return -1;
}

static int skip_number(const char* text, size_t len, size_t* index) {
  size_t i = *index;
  if (i < len && text[i] == '-') {
    i++;
  }
  if (i >= len) {
    return -1;
  }
  if (text[i] == '0') {
    i++;
  } else if (text[i] >= '1' && text[i] <= '9') {
    do {
      i++;
    } while (i < len && isdigit((unsigned char)text[i]));
  } else {
    return -1;
  }
  if (i < len && text[i] == '.') {
    i++;
    if (i >= len || !isdigit((unsigned char)text[i])) {
      return -1;
    }
    do {
      i++;
    } while (i < len && isdigit((unsigned char)text[i]));
  }
  if (i < len && (text[i] == 'e' || text[i] == 'E')) {
    i++;
    if (i < len && (text[i] == '+' || text[i] == '-')) {
      i++;
    }
    if (i >= len || !isdigit((unsigned char)text[i])) {
      return -1;
    }
    do {
      i++;
    } while (i < len && isdigit((unsigned char)text[i]));
  }
  *index = i;
  return 0;
}

static int skip_value(const char* text, size_t len, size_t* index, int depth,
                      bole_json_type* type) {
  if (depth > BOLE_JSON_MAX_DEPTH) {
    return -1;
  }
  skip_ws(text, len, index);
  if (*index >= len) {
    return -1;
  }
  char c = text[*index];
  if (c == '"') {
    if (type != NULL) {
      *type = BOLE_JSON_STRING;
    }
    return skip_string(text, len, index);
  }
  if (c == '{') {
    if (type != NULL) {
      *type = BOLE_JSON_OBJECT;
    }
    (*index)++;
    skip_ws(text, len, index);
    if (*index < len && text[*index] == '}') {
      (*index)++;
      return 0;
    }
    for (;;) {
      if (skip_string(text, len, index) != 0) {
        return -1;
      }
      skip_ws(text, len, index);
      if (*index >= len || text[*index] != ':') {
        return -1;
      }
      (*index)++;
      if (skip_value(text, len, index, depth + 1, NULL) != 0) {
        return -1;
      }
      skip_ws(text, len, index);
      if (*index >= len) {
        return -1;
      }
      if (text[*index] == ',') {
        (*index)++;
        skip_ws(text, len, index);
        continue;
      }
      if (text[*index] == '}') {
        (*index)++;
        return 0;
      }
      return -1;
    }
  }
  if (c == '[') {
    if (type != NULL) {
      *type = BOLE_JSON_ARRAY;
    }
    (*index)++;
    skip_ws(text, len, index);
    if (*index < len && text[*index] == ']') {
      (*index)++;
      return 0;
    }
    for (;;) {
      if (skip_value(text, len, index, depth + 1, NULL) != 0) {
        return -1;
      }
      skip_ws(text, len, index);
      if (*index >= len) {
        return -1;
      }
      if (text[*index] == ',') {
        (*index)++;
        skip_ws(text, len, index);
        continue;
      }
      if (text[*index] == ']') {
        (*index)++;
        return 0;
      }
      return -1;
    }
  }
  if (c == 't') {
    if (len - *index >= 4 && memcmp(text + *index, "true", 4) == 0) {
      *index += 4;
      if (type != NULL) {
        *type = BOLE_JSON_BOOL;
      }
      return 0;
    }
    return -1;
  }
  if (c == 'f') {
    if (len - *index >= 5 && memcmp(text + *index, "false", 5) == 0) {
      *index += 5;
      if (type != NULL) {
        *type = BOLE_JSON_BOOL;
      }
      return 0;
    }
    return -1;
  }
  if (c == 'n') {
    if (len - *index >= 4 && memcmp(text + *index, "null", 4) == 0) {
      *index += 4;
      if (type != NULL) {
        *type = BOLE_JSON_NULL;
      }
      return 0;
    }
    return -1;
  }
  if (c == '-' || isdigit((unsigned char)c)) {
    if (type != NULL) {
      *type = BOLE_JSON_NUMBER;
    }
    return skip_number(text, len, index);
  }
  return -1;
}

int bole_json_parse_object(const char* text, size_t len,
                           bole_json_member* members, size_t capacity,
                           size_t* out_count) {
  if (text == NULL || out_count == NULL || (members == NULL && capacity > 0)) {
    return -1;
  }
  size_t i = 0;
  size_t count = 0;
  skip_ws(text, len, &i);
  if (i >= len || text[i] != '{') {
    return -3;
  }
  i++;
  skip_ws(text, len, &i);
  if (i < len && text[i] == '}') {
    i++;
  } else {
    for (;;) {
      size_t key_start = i + 1;
      if (skip_string(text, len, &i) != 0) {
        return -1;
      }
      size_t key_end = i - 1;
      skip_ws(text, len, &i);
      if (i >= len || text[i] != ':') {
        return -1;
      }
      i++;
      skip_ws(text, len, &i);
      size_t value_start = i;
      bole_json_type type = BOLE_JSON_NULL;
      if (skip_value(text, len, &i, 0, &type) != 0) {
        return -1;
      }
      if (count < capacity) {
        members[count].key = text + key_start;
        members[count].key_len = key_end - key_start;
        members[count].value = text + value_start;
        members[count].value_len = i - value_start;
        members[count].type = type;
      }
      count++;
      skip_ws(text, len, &i);
      if (i >= len) {
        return -1;
      }
      if (text[i] == ',') {
        i++;
        skip_ws(text, len, &i);
        continue;
      }
      if (text[i] == '}') {
        i++;
        break;
      }
      return -1;
    }
  }
  skip_ws(text, len, &i);
  if (i != len) {
    return -1;
  }
  *out_count = count;
  if (count > capacity) {
    return -2;
  }
  return 0;
}

int bole_json_validate(const char* text, size_t len) {
  if (text == NULL) {
    return -1;
  }
  size_t i = 0;
  if (skip_value(text, len, &i, 0, NULL) != 0) {
    return -1;
  }
  skip_ws(text, len, &i);
  return i == len ? 0 : -1;
}

const bole_json_member* bole_json_member_get(const bole_json_member* members,
                                             size_t count, const char* key) {
  if (members == NULL || key == NULL) {
    return NULL;
  }
  size_t key_len = strlen(key);
  for (size_t i = 0; i < count; i++) {
    if (members[i].key_len == key_len && memcmp(members[i].key, key, key_len) == 0) {
      return &members[i];
    }
  }
  return NULL;
}

static int hex_value(unsigned char c) {
  if (c >= '0' && c <= '9') {
    return (int)(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return (int)(c - 'a') + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return (int)(c - 'A') + 10;
  }
  return -1;
}

static int read_hex4(const char* text, size_t offset, unsigned* out) {
  unsigned value = 0;
  for (size_t k = 0; k < 4; k++) {
    int digit = hex_value((unsigned char)text[offset + k]);
    if (digit < 0) {
      return -1;
    }
    value = (value << 4) | (unsigned)digit;
  }
  *out = value;
  return 0;
}

static int append_codepoint(char* out, size_t capacity, size_t* length, unsigned cp) {
  unsigned char bytes[4];
  size_t count;
  if (cp <= 0x7F) {
    bytes[0] = (unsigned char)cp;
    count = 1;
  } else if (cp <= 0x7FF) {
    bytes[0] = (unsigned char)(0xC0 | (cp >> 6));
    bytes[1] = (unsigned char)(0x80 | (cp & 0x3F));
    count = 2;
  } else if (cp <= 0xFFFF) {
    bytes[0] = (unsigned char)(0xE0 | (cp >> 12));
    bytes[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    bytes[2] = (unsigned char)(0x80 | (cp & 0x3F));
    count = 3;
  } else if (cp <= 0x10FFFF) {
    bytes[0] = (unsigned char)(0xF0 | (cp >> 18));
    bytes[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    bytes[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    bytes[3] = (unsigned char)(0x80 | (cp & 0x3F));
    count = 4;
  } else {
    return -1;
  }
  if (*length + count >= capacity) {
    return -2;
  }
  memcpy(out + *length, bytes, count);
  *length += count;
  return 0;
}

static int unescape_string(const char* text, size_t len,
                           char* out, size_t capacity, size_t* out_len) {
  if (out == NULL || capacity == 0 || out_len == NULL) {
    return -2;
  }
  size_t length = 0;
  size_t i = 0;
  while (i < len) {
    unsigned char c = (unsigned char)text[i];
    if (c != '\\') {
      if (length + 1 >= capacity) {
        out[capacity - 1] = '\0';
        return -2;
      }
      out[length++] = (char)c;
      i++;
      continue;
    }
    i++;
    if (i >= len) {
      return -1;
    }
    switch (text[i]) {
      case '"': c = '"'; i++; break;
      case '\\': c = '\\'; i++; break;
      case '/': c = '/'; i++; break;
      case 'b': c = '\b'; i++; break;
      case 'f': c = '\f'; i++; break;
      case 'n': c = '\n'; i++; break;
      case 'r': c = '\r'; i++; break;
      case 't': c = '\t'; i++; break;
      case 'u': {
        unsigned cp;
        if (i + 4 >= len || read_hex4(text, i + 1, &cp) != 0) {
          return -1;
        }
        i += 5;
        if (cp >= 0xD800 && cp <= 0xDBFF) {
          unsigned low;
          if (i + 5 >= len || text[i] != '\\' || text[i + 1] != 'u'
              || read_hex4(text, i + 2, &low) != 0
              || low < 0xDC00 || low > 0xDFFF) {
            return -1;
          }
          cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          i += 6;
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
          return -1;
        }
        if (append_codepoint(out, capacity, &length, cp) != 0) {
          out[capacity - 1] = '\0';
          return -2;
        }
        continue;
      }
      default:
        return -1;
    }
    if (length + 1 >= capacity) {
      out[capacity - 1] = '\0';
      return -2;
    }
    out[length++] = (char)c;
  }
  out[length] = '\0';
  *out_len = length;
  return 0;
}

int bole_json_string(const bole_json_member* member,
                     char* out, size_t capacity, size_t* out_len) {
  if (member == NULL || member->type != BOLE_JSON_STRING || member->value_len < 2) {
    return -1;
  }
  return unescape_string(member->value + 1, member->value_len - 2, out, capacity, out_len);
}

int bole_json_raw(const bole_json_member* member,
                  char* out, size_t capacity, size_t* out_len) {
  if (member == NULL || out == NULL || capacity == 0) {
    return -1;
  }
  if (member->value_len + 1 > capacity) {
    return -2;
  }
  memcpy(out, member->value, member->value_len);
  out[member->value_len] = '\0';
  if (out_len != NULL) {
    *out_len = member->value_len;
  }
  return 0;
}

int bole_json_u64(const bole_json_member* member, uint64_t* out) {
  if (member == NULL || member->type != BOLE_JSON_NUMBER || out == NULL
      || member->value_len == 0 || member->value_len >= 32) {
    return -1;
  }
  char temp[32];
  memcpy(temp, member->value, member->value_len);
  temp[member->value_len] = '\0';
  char* end = NULL;
  errno = 0;
  unsigned long long value = strtoull(temp, &end, 10);
  if (end == temp || *end != '\0' || temp[0] == '-' || errno == ERANGE) {
    return -1;
  }
  *out = (uint64_t)value;
  return 0;
}

int bole_json_i64(const bole_json_member* member, int64_t* out) {
  if (member == NULL || member->type != BOLE_JSON_NUMBER || out == NULL
      || member->value_len == 0 || member->value_len >= 32) {
    return -1;
  }
  char temp[32];
  memcpy(temp, member->value, member->value_len);
  temp[member->value_len] = '\0';
  char* end = NULL;
  errno = 0;
  long long value = strtoll(temp, &end, 10);
  if (end == temp || *end != '\0' || errno == ERANGE) {
    return -1;
  }
  *out = (int64_t)value;
  return 0;
}

int bole_json_bool(const bole_json_member* member, int* out) {
  if (member == NULL || member->type != BOLE_JSON_BOOL || out == NULL) {
    return -1;
  }
  if (member->value_len == 4 && memcmp(member->value, "true", 4) == 0) {
    *out = 1;
    return 0;
  }
  if (member->value_len == 5 && memcmp(member->value, "false", 5) == 0) {
    *out = 0;
    return 0;
  }
  return -1;
}

int bole_buf_init(bole_buf* buf, size_t initial) {
  if (buf == NULL) {
    return -1;
  }
  if (initial < 64) {
    initial = 64;
  }
  buf->data = (char*)malloc(initial);
  if (buf->data == NULL) {
    return -1;
  }
  buf->cap = initial;
  buf->len = 0;
  buf->data[0] = '\0';
  return 0;
}

void bole_buf_free(bole_buf* buf) {
  if (buf != NULL) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
  }
}

void bole_buf_reset(bole_buf* buf) {
  if (buf != NULL && buf->data != NULL) {
    buf->len = 0;
    buf->data[0] = '\0';
  }
}

static int buf_reserve(bole_buf* buf, size_t need) {
  if (buf == NULL || buf->data == NULL) {
    return -1;
  }
  if (need + 1 <= buf->cap) {
    return 0;
  }
  size_t cap = buf->cap;
  while (cap < need + 1) {
    if (cap > (size_t)-1 / 2) {
      return -1;
    }
    cap *= 2;
  }
  char* next = (char*)realloc(buf->data, cap);
  if (next == NULL) {
    return -1;
  }
  buf->data = next;
  buf->cap = cap;
  return 0;
}

int bole_buf_append(bole_buf* buf, const char* data, size_t len) {
  if (buf == NULL || (data == NULL && len > 0)) {
    return -1;
  }
  if (buf_reserve(buf, buf->len + len) != 0) {
    return -1;
  }
  if (len > 0) {
    memcpy(buf->data + buf->len, data, len);
  }
  buf->len += len;
  buf->data[buf->len] = '\0';
  return 0;
}

int bole_buf_append_cstr(bole_buf* buf, const char* text) {
  return text == NULL ? -1 : bole_buf_append(buf, text, strlen(text));
}

int bole_buf_append_char(bole_buf* buf, char c) {
  return bole_buf_append(buf, &c, 1);
}

int bole_buf_appendf(bole_buf* buf, const char* format, ...) {
  if (buf == NULL || format == NULL) {
    return -1;
  }
  size_t cap = 256;
  for (;;) {
    char* temp = (char*)malloc(cap);
    if (temp == NULL) {
      return -1;
    }
    va_list args;
    va_start(args, format);
    int written = vsnprintf(temp, cap, format, args);
    va_end(args);
    if (written >= 0 && (size_t)written < cap) {
      int result = bole_buf_append(buf, temp, (size_t)written);
      free(temp);
      return result;
    }
    free(temp);
    if (cap >= (1u << 20)) {
      return -1;
    }
    cap *= 2;
  }
}

int bole_buf_append_json_string(bole_buf* buf, const char* text) {
  if (buf == NULL || text == NULL) {
    return -1;
  }
  if (bole_buf_append_char(buf, '"') != 0) {
    return -1;
  }
  for (const unsigned char* p = (const unsigned char*)text; *p != 0; p++) {
    unsigned char c = *p;
    switch (c) {
      case '"': if (bole_buf_append_cstr(buf, "\\\"") != 0) return -1; break;
      case '\\': if (bole_buf_append_cstr(buf, "\\\\") != 0) return -1; break;
      case '\b': if (bole_buf_append_cstr(buf, "\\b") != 0) return -1; break;
      case '\f': if (bole_buf_append_cstr(buf, "\\f") != 0) return -1; break;
      case '\n': if (bole_buf_append_cstr(buf, "\\n") != 0) return -1; break;
      case '\r': if (bole_buf_append_cstr(buf, "\\r") != 0) return -1; break;
      case '\t': if (bole_buf_append_cstr(buf, "\\t") != 0) return -1; break;
      default:
        if (c < 0x20) {
          if (bole_buf_appendf(buf, "\\u%04x", (unsigned)c) != 0) return -1;
        } else {
          if (bole_buf_append_char(buf, (char)c) != 0) return -1;
        }
        break;
    }
  }
  return bole_buf_append_char(buf, '"');
}
