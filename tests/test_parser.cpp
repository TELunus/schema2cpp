#include <cmath>
#include <iostream>
#include <string>
#include "s2c_runtime.hpp"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; ++failures; } } while (0)

static bool rejects(const std::string& text) {
  try { s2c::parse(text); } catch (const s2c::ParseError&) { return true; }
  return false;
}

int main() {
  using T = s2c::Value::Type;
  CHECK(s2c::parse("\"\\u00e9\"").as_string() == "\xC3\xA9");
  CHECK(s2c::parse("\"\\ud83d\\ude00\"").as_string() == "\xF0\x9F\x98\x80");
  CHECK(s2c::parse("\"a\\nb\\\"c\\\\\"").as_string() == "a\nb\"c\\");
  CHECK(s2c::parse("\"\"").as_string().empty());
  CHECK(s2c::parse("  [1, 2 ,3]\n").as_array().size() == 3);
  CHECK(s2c::parse("1.0").as_number().is_int);
  CHECK(!s2c::parse("1.5").as_number().is_int);
  CHECK(s2c::parse("-7").as_number().i == -7);
  CHECK(!s2c::parse("12345678901234567890").as_number().is_int);
  CHECK(s2c::parse("null").type() == T::Null);
  CHECK(s2c::parse("true").as_bool());

  s2c::Value o = s2c::parse(R"({"b":1,"a":[true,null]})");
  CHECK(o.as_object().items()[0].first == "b");  // insertion order kept
  CHECK(o.as_object().find("a")->as_array().size() == 2);

  std::string deep(100, '['); deep += std::string(100, ']');
  CHECK(s2c::parse(deep).type() == T::Array);

  CHECK(rejects(""));
  CHECK(rejects("[1,2"));
  CHECK(rejects("{\"a\":1} x"));
  CHECK(rejects("{\"a\":1,\"a\":2}"));
  CHECK(rejects("{\"a\":}"));
  CHECK(rejects("\"\xff\""));  // invalid UTF-8
  CHECK(rejects("[1,]"));
  return failures ? 1 : 0;
}
