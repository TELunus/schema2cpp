#include <iostream>
#include "person_embedded.hpp"

int main() {
  const embedded::Person& p = embedded::sample();
  int failures = 0;
  auto check = [&](bool ok, const char* what) { if (!ok) { std::cerr << "FAIL: " << what << "\n"; ++failures; } };
  check(p.name == "Ann \"The\" Dev", "name");
  check(p.age == 30, "age");
  check(!p.email.has_value(), "null email");
  check(p.role && *p.role == "admin", "enum");
  check(p.tags && p.tags->size() == 2, "tags");
  check(p.address.zip == "V6B", "nested");
  check(p.address.lines && p.address.lines->size() == 2 && *(*p.address.lines)[0].t == 1.5, "array of objects");
  check(&embedded::sample() == &p, "same object each call");
  return failures ? 1 : 0;
}
