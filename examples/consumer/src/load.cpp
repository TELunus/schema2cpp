#include <fstream>
#include <iostream>
#include <sstream>
#include "person.hpp"

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1], std::ios::binary);
  std::stringstream buf;
  buf << in.rdbuf();
  try {
    Person p(buf.str());
    std::cout << p.name << " is " << p.age << "\n";
  } catch (const s2c::ParseError& e) {
    std::cerr << "malformed JSON: " << e.what() << "\n";  return 1;
  } catch (const s2c::ValidationError& e) {
    std::cerr << "schema mismatch: " << e.what() << "\n"; return 1;
  }
}
