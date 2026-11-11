#include <iostream>
#include <string>
#include "person.hpp"
#include "settings.hpp"

static int failures = 0;
#define CHECK_EQ(got, want) do { std::string g_ = (got); std::string w_ = (want); \
  if (g_ != w_) { std::cerr << "FAIL line " << __LINE__ << ":\n  got  " << g_ << "\n  want " << w_ << "\n"; ++failures; } } while (0)

template <class F>
static bool throws_validation(F f) {
  try { f(); } catch (const s2c::ValidationError&) { return true; } catch (...) {}
  return false;
}
template <class F>
static bool throws_parse(F f) {
  try { f(); } catch (const s2c::ParseError&) { return true; } catch (...) {}
  return false;
}

int main() {
  using settings::Settings;

  // Changed members are written; extra keys survive; keys keep the document's order.
  {
    Settings s(R"({"name":"a","mode":"fast","count":3})");
    s.name = "b";
    s.count = 4;
    CHECK_EQ(s.update(R"({"zzz":{"k":1},"name":"a","count":3,"mode":"fast"})"),
             R"({"zzz":{"k":1},"name":"b","count":4,"mode":"fast"})");
    // New keys are appended in schema order.
    CHECK_EQ(s.update(R"({"zzz":1})"), R"({"zzz":1,"name":"b","mode":"fast","count":4})");
  }

  // Optional member set to nullopt removes the key; nullable ones become null.
  {
    Settings s(R"({"name":"a","mode":null})");
    CHECK_EQ(s.update(R"({"name":"x","mode":"m","count":9,"nick":"n","note":"t"})"),
             R"({"name":"a","mode":null,"nick":null})");
    CHECK_EQ(s.update(R"({"mode":"m"})"), R"({"mode":null,"name":"a"})");  // absent nick stays absent
  }

  // Nested objects merge (and drop extras when additionalProperties is false);
  // array elements merge by index; free-form objects are replaced.
  {
    Settings s(R"({"name":"a","mode":null,"limits":{"lo":1},"items":[{"id":1},{"id":2}],"free":{"a":1}})");
    CHECK_EQ(s.update(R"({"name":"x","mode":null,"limits":{"hi":9,"lo":0,"other":1},
                          "items":[{"id":9,"x":"keep"},{"id":8},{"id":7,"y":1}],"free":{"b":2}})"),
             R"({"name":"a","mode":null,"limits":{"lo":1},"items":[{"id":1,"x":"keep"},{"id":2}],"free":{"a":1}})");
  }

  // A property called "update" does not collide with the method.
  {
    Settings s(R"({"name":"a","mode":null,"update":true})");
    CHECK_EQ(s.update_ ? "t" : "f", "t");
    CHECK_EQ(s.update(R"({})"), R"({"name":"a","mode":null,"update":true})");
  }

  // Numbers, strings, pretty printing.
  {
    Settings s(R"({"name":"q\"\\\n\u0001é","mode":null,"ratio":0.1,"count":0})");
    CHECK_EQ(s.update("{}"), R"({"name":"q\"\\\n\u0001é","mode":null,"count":0,"ratio":0.1})");
    s.ratio = 3.0;
    CHECK_EQ(s.update(R"({"name":"a"})", 2), "{\n  \"name\": \"q\\\"\\\\\\n\\u0001é\",\n  \"mode\": null,\n  \"count\": 0,\n  \"ratio\": 3\n}");
  }

  // Errors.
  {
    Settings s(R"({"name":"a","mode":null})");
    CHECK_EQ(std::to_string(throws_parse([&] { s.update("{"); })), "1");
    CHECK_EQ(std::to_string(throws_validation([&] { s.update("[]"); })), "1");
    s.count = -1;  // violates minimum
    CHECK_EQ(std::to_string(throws_validation([&] { s.update("{}"); })), "1");
    s.count = std::nullopt;
    s.name = "bad\xff";
    CHECK_EQ(std::to_string(throws_validation([&] { s.update("{}"); })), "1");
  }

  // additionalProperties: false at the root drops unknown keys; nested open objects keep theirs.
  {
    Person p(R"({"name":"Ann","age":30,"address":{"zip":"V6B","lines":[{"t":2.5}]}})");
    std::string out = p.update(R"({"name":"Old","age":1,"address":{"zip":"x","note":"hi","lines":[{"t":1,"u":"keep"}]},"junk":1})");
    CHECK_EQ(out, R"({"name":"Ann","age":30,"address":{"zip":"V6B","note":"hi","lines":[{"t":2.5,"u":"keep"}]}})");
    Person again(out);  // the result is valid JSON for the schema
    CHECK_EQ(again.name, "Ann");
  }
  return failures ? 1 : 0;
}
