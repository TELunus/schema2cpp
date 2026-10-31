#include <iostream>
#include "config.hpp"

int main() {
  const Person& p = config();   // built at compile time from data/person.json
  std::cout << p.name << " is " << p.age << "\n";
}
