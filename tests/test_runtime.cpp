#include <iostream>
#include "person.hpp"

enum class Expect { Ok, Parse, Invalid };

int main() {
  struct Case { const char* json; Expect expect; const char* label; };
  const Case cases[] = {
    {R"({"name":"Ann","age":30,"address":{"zip":"V6B"},"tags":["a"],"email":null,"role":"admin"})", Expect::Ok, "valid"},
    {R"({"name":"Ann","age":30,"address":{"zip":"V6B"},"bogus":1})", Expect::Invalid, "unknown property"},
    {R"({"name":"","age":30,"address":{"zip":"x"}})", Expect::Invalid, "minLength"},
    {R"({"name":"Ann","age":30.5,"address":{"zip":"x"}})", Expect::Invalid, "non-integer"},
    {R"({"name":"Ann","age":200,"address":{"zip":"x"}})", Expect::Invalid, "maximum"},
    {R"({"name":"Ann","age":30,"address":{}})", Expect::Invalid, "missing nested required"},
    {R"({"name":"Ann","age":30})", Expect::Invalid, "missing required"},
    {R"({"name":"Ann","age":30,"address":{"zip":"x","lines":[{"t":"s"}]}})", Expect::Invalid, "wrong type in array item"},
    {R"({"name":"Ann","age":30,"address":{"zip":"x"},"role":"root"})", Expect::Invalid, "enum"},
    {R"({"name":"Ann","age":30,"address":{"zip":"x"},"tags":["1","2","3","4"]})", Expect::Invalid, "maxItems"},
    {R"([])", Expect::Invalid, "root is not an object"},
    {R"({"name":"Ann","age":30,"address":{"zip":"x"},)", Expect::Parse, "truncated JSON"},
    {R"(not json)", Expect::Parse, "garbage"},
  };

  int failures = 0;
  for (const Case& c : cases) {
    Expect got = Expect::Ok;
    std::string msg;
    try { Person p(c.json); }
    catch (const s2c::ParseError& e) { got = Expect::Parse; msg = e.what(); }
    catch (const s2c::ValidationError& e) { got = Expect::Invalid; msg = e.what(); }
    if (got != c.expect) {
      std::cerr << "FAIL: " << c.label << " (" << msg << ")\n";
      ++failures;
    }
  }

  Person p(cases[0].json);
  if (p.name != "Ann" || p.age != 30 || p.email.has_value() || p.tags->size() != 1 || p.address.zip != "V6B") {
    std::cerr << "FAIL: field values\n";
    ++failures;
  }
  return failures ? 1 : 0;
}
