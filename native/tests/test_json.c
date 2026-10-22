#include "../src/json.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char* NOTIFICATION =
    "{\"jsonrpc\":\"2.0\",\"method\":\"bole.event\",\"params\":{"
    "\"sessionId\":\"s-001\","
    "\"event\":{\"event_id\":42,\"type\":10,\"session_id\":\"s-001\","
    "\"task\":1,\"step\":0,\"seq\":18,\"ts_ms\":1761100000000,"
    "\"payload\":{\"v\":1,\"turn\":1,\"text\":\"a\\\"b\\u4f60\"}}}}";

static void test_notification(void) {
  bole_json_member top[16];
  size_t top_count = 0;
  assert(bole_json_parse_object(NOTIFICATION, strlen(NOTIFICATION), top, 16, &top_count) == 0);
  assert(top_count == 3);

  const bole_json_member* params = bole_json_member_get(top, top_count, "params");
  assert(params != NULL && params->type == BOLE_JSON_OBJECT);

  bole_json_member params_members[8];
  size_t params_count = 0;
  assert(bole_json_parse_object(params->value, params->value_len, params_members, 8, &params_count) == 0);

  char session[64];
  size_t session_len = 0;
  const bole_json_member* session_member = bole_json_member_get(params_members, params_count, "sessionId");
  assert(session_member != NULL);
  assert(bole_json_string(session_member, session, sizeof(session), &session_len) == 0);
  assert(strcmp(session, "s-001") == 0 && session_len == 5);

  const bole_json_member* event = bole_json_member_get(params_members, params_count, "event");
  assert(event != NULL && event->type == BOLE_JSON_OBJECT);

  bole_json_member event_members[16];
  size_t event_count = 0;
  assert(bole_json_parse_object(event->value, event->value_len, event_members, 16, &event_count) == 0);

  uint64_t event_id = 0;
  assert(bole_json_u64(bole_json_member_get(event_members, event_count, "event_id"), &event_id) == 0);
  assert(event_id == 42);

  int64_t ts = 0;
  assert(bole_json_i64(bole_json_member_get(event_members, event_count, "ts_ms"), &ts) == 0);
  assert(ts == 1761100000000LL);

  const bole_json_member* payload = bole_json_member_get(event_members, event_count, "payload");
  assert(payload != NULL && payload->type == BOLE_JSON_OBJECT);

  char raw[256];
  size_t raw_len = 0;
  assert(bole_json_raw(payload, raw, sizeof(raw), &raw_len) == 0);
  assert(strstr(raw, "\"turn\":1") != NULL);

  bole_json_member payload_members[8];
  size_t payload_count = 0;
  assert(bole_json_parse_object(payload->value, payload->value_len, payload_members, 8, &payload_count) == 0);
  const bole_json_member* text = bole_json_member_get(payload_members, payload_count, "text");
  char text_value[64];
  size_t text_len = 0;
  assert(bole_json_string(text, text_value, sizeof(text_value), &text_len) == 0);
  assert(strcmp(text_value, "a\"b你") == 0);
}

static void test_invalid(void) {
  bole_json_member members[4];
  size_t count = 0;
  assert(bole_json_parse_object("{\"a\":1,}", 8, members, 4, &count) != 0);
  assert(bole_json_parse_object("{\"a\":01}", 8, members, 4, &count) != 0);
  assert(bole_json_parse_object("[1,2]", 5, members, 4, &count) == -3);
  assert(bole_json_parse_object("{\"a\":1} trailing", 16, members, 4, &count) != 0);

  const char* surrogate = "{\"a\":\"\\uD800\"}";
  assert(bole_json_parse_object(surrogate, strlen(surrogate), members, 4, &count) == 0);
  char value[8];
  size_t value_len = 0;
  assert(bole_json_string(bole_json_member_get(members, count, "a"), value, sizeof(value), &value_len) == -1);
}

static void test_builder(void) {
  bole_buf buf;
  assert(bole_buf_init(&buf, 64) == 0);
  assert(bole_buf_append_cstr(&buf, "{\"id\":") == 0);
  assert(bole_buf_appendf(&buf, "%llu", 7ULL) == 0);
  assert(bole_buf_append_cstr(&buf, ",\"text\":") == 0);
  assert(bole_buf_append_json_string(&buf, "line\n\"quote\"") == 0);
  assert(bole_buf_append_char(&buf, '}') == 0);

  bole_json_member members[8];
  size_t count = 0;
  assert(bole_json_parse_object(buf.data, buf.len, members, 8, &count) == 0);
  assert(count == 2);
  const bole_json_member* text = bole_json_member_get(members, count, "text");
  char value[64];
  size_t value_len = 0;
  assert(bole_json_string(text, value, sizeof(value), &value_len) == 0);
  assert(strcmp(value, "line\n\"quote\"") == 0);
  bole_buf_free(&buf);
}

static void test_limits(void) {
  bole_json_member members[2];
  size_t count = 0;
  const char* text = "{\"a\":1,\"b\":2,\"c\":3}";
  assert(bole_json_parse_object(text, strlen(text), members, 2, &count) == -2);
  assert(count == 3);
  assert(members[0].type == BOLE_JSON_NUMBER);
  assert(members[0].value_len == 1 && members[0].value[0] == '1');
  assert(members[1].type == BOLE_JSON_NUMBER);

  assert(bole_json_parse_object("{}", 2, NULL, 0, &count) == 0);
  assert(count == 0);
  assert(bole_json_parse_object("{", 1, members, 2, &count) != 0);
  assert(bole_json_parse_object("{\"a\":}", 6, members, 2, &count) != 0);
  assert(bole_json_parse_object("{\"a\":1,}", 8, members, 2, &count) != 0);
  assert(bole_json_parse_object("{\"a\":\"\\q\"}", 10, members, 2, &count) != 0);
  assert(bole_json_parse_object("{\"a\":\"\x01\"}", 9, members, 2, &count) != 0);
}

static void test_unicode(void) {
  const char* text = "{\"a\":\"\\u4f60\\u597d\\ud83d\\ude00\"}";
  bole_json_member members[4];
  size_t count = 0;
  assert(bole_json_parse_object(text, strlen(text), members, 4, &count) == 0);
  char value[32];
  size_t length = 0;
  assert(bole_json_string(bole_json_member_get(members, count, "a"),
                          value, sizeof(value), &length) == 0);
  assert(length == strlen("你好😀"));
  assert(strcmp(value, "你好😀") == 0);

  const char* lone_low = "{\"a\":\"\\udc00\"}";
  assert(bole_json_parse_object(lone_low, strlen(lone_low), members, 4, &count) == 0);
  assert(bole_json_string(bole_json_member_get(members, count, "a"),
                          value, sizeof(value), &length) == -1);

  const char* lone_high = "{\"a\":\"\\ud83d\"}";
  assert(bole_json_parse_object(lone_high, strlen(lone_high), members, 4, &count) == 0);
  assert(bole_json_string(bole_json_member_get(members, count, "a"),
                          value, sizeof(value), &length) == -1);

  char tiny[4];
  const char* ascii = "{\"a\":\"abcd\"}";
  assert(bole_json_parse_object(ascii, strlen(ascii), members, 4, &count) == 0);
  assert(bole_json_string(bole_json_member_get(members, count, "a"),
                          tiny, sizeof(tiny), &length) == -2);
  assert(tiny[sizeof(tiny) - 1] == '\0');
}

static void test_numbers(void) {
  const char* text = "{\"a\":0,\"b\":18446744073709551615,\"c\":-42,\"d\":1.5,"
                     "\"e\":true,\"f\":false,\"g\":null}";
  bole_json_member members[16];
  size_t count = 0;
  assert(bole_json_parse_object(text, strlen(text), members, 16, &count) == 0);
  uint64_t u = 1;
  int64_t i = 0;
  int b = 0;
  assert(bole_json_u64(bole_json_member_get(members, count, "a"), &u) == 0 && u == 0);
  assert(bole_json_u64(bole_json_member_get(members, count, "b"), &u) == 0);
  assert(u == 18446744073709551615ULL);
  assert(bole_json_u64(bole_json_member_get(members, count, "c"), &u) != 0);
  assert(bole_json_i64(bole_json_member_get(members, count, "c"), &i) == 0 && i == -42);
  assert(bole_json_u64(bole_json_member_get(members, count, "d"), &u) != 0);
  assert(bole_json_bool(bole_json_member_get(members, count, "e"), &b) == 0 && b == 1);
  assert(bole_json_bool(bole_json_member_get(members, count, "f"), &b) == 0 && b == 0);
  assert(bole_json_bool(bole_json_member_get(members, count, "g"), &b) != 0);
}

static void test_builder_growth(void) {
  bole_buf buf;
  assert(bole_buf_init(&buf, 1) == 0);
  for (int i = 0; i < 1000; i++) {
    assert(bole_buf_append_cstr(&buf, "0123456789") == 0);
  }
  assert(buf.len == 10000);
  assert(buf.data[buf.len] == '\0');
  assert(bole_buf_appendf(&buf, "%s:%d", "tail", 7) == 0);
  assert(strstr(buf.data, "tail:7") != NULL);
  bole_buf_reset(&buf);
  assert(buf.len == 0 && buf.data[0] == '\0');
  bole_buf_free(&buf);
}

int main(void) {
  test_notification();
  test_invalid();
  test_builder();
  test_limits();
  test_unicode();
  test_numbers();
  test_builder_growth();
  printf("json tests passed\n");
  return 0;
}
