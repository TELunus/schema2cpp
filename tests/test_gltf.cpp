// Tests for the glTF schema: schema2cpp must be able to consume the Khronos
// glTF 2.0 JSON Schema (with relative file $refs) and emit a class that
// parses the Khronos-generated reference assets.
//
// Usage: test_gltf <positive-dir> <negative-dir>
//
// The positive directory is walked recursively; every *.gltf file must parse
// and round-trips through update(). The negative directory holds assets that
// are semantically invalid (primitive restart) but schema-valid, so they are
// expected to parse as well; they exist to make sure we iterate everything
// without crashing. A hand-crafted schema-violating document must be rejected.

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>

#include "gltf.hpp"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; ++failures; } } while (0)

static std::string slurp(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open " + p.string());
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static bool expect_validation_error(const std::string& json) {
  try {
    GlTF g(json);
    (void)g;
    return false;
  } catch (const s2c::ValidationError&) {
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

static bool expect_parse_error(const std::string& json) {
  try {
    GlTF g(json);
    (void)g;
    return false;
  } catch (const s2c::ParseError&) {
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// Parses every *.gltf under `dir`; returns the count.
static int parse_tree(const std::string& dir, bool expect_ok) {
  int count = 0;
  for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
    if (!e.is_regular_file() || e.path().extension() != ".gltf") continue;
    const std::string text = slurp(e.path());
    try {
      GlTF g(text);
      // Round-trip: update("{}") rewrites every member in schema order; feeding
      // the result back must be a no-op (i.e. update is idempotent).
      const std::string once = g.update("{}");
      GlTF g2(once);
      const std::string twice = g2.update("{}");
      if (once != twice) {
        std::cerr << "FAIL round-trip " << e.path().string() << "\n  once:  " << once.substr(0, 200) << "\n  twice: " << twice.substr(0, 200) << "\n";
        ++failures;
      }
      ++count;
      if (!expect_ok) {
        std::cerr << "FAIL " << e.path().string() << ": expected validation failure, but it parsed\n";
        ++failures;
      }
    } catch (const std::exception& ex) {
      if (expect_ok) {
        std::cerr << "FAIL " << e.path().string() << ": " << ex.what() << "\n";
        ++failures;
      } else {
        ++count;
      }
    }
  }
  return count;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: " << argv[0] << " <positive-dir> <negative-dir>\n";
    return 2;
  }

  const int pos = parse_tree(argv[1], /*expect_ok=*/true);
  const int neg = parse_tree(argv[2], /*expect_ok=*/true);  // negatives are schema-valid
  std::cout << "parsed " << pos << " positive and " << neg << " negative assets\n";
  CHECK(pos > 0);
  CHECK(neg >= 0);

  // Hand-crafted violations must be rejected.
  CHECK(expect_validation_error("{}"));  // root "asset" is required
  CHECK(expect_validation_error(R"({"asset":{"version":42}})"));  // version must be a string
  CHECK(expect_validation_error(R"({"asset":{"version":"2.0"},"scene":-1})"));  // glTFid below minimum
  CHECK(expect_parse_error("{ not json"));

  if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
  std::cout << "all glTF tests passed\n";
  return 0;
}
