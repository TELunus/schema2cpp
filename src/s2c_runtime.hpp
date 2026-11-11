// s2c_runtime.hpp - tiny JSON DOM + validation helpers shared by the generator and
// by the C++ classes it generates. Requires C++17.
//
// s2c::parse() is implemented in s2c_runtime.cpp on top of Wuffs' JSON decoder.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace s2c {

// Thrown when the text is not well-formed JSON.
struct ParseError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Thrown when well-formed JSON does not match the schema. The message starts
// with a path such as "$.address.zip".
struct ValidationError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

class Value;

// Insertion-ordered object (keeps the order keys appeared in the document).
class Object {
 public:
  using Item = std::pair<std::string, Value>;
  const Value* find(std::string_view key) const;
  void add(std::string key, Value v);  // throws ParseError on duplicate key
  const std::vector<Item>& items() const { return items_; }
  std::size_t size() const { return items_.size(); }

 private:
  std::vector<Item> items_;
};

class Value {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };
  struct Number {
    double d = 0;
    std::int64_t i = 0;
    bool is_int = false;  // true if the value is integral and fits in int64
  };
  using Array = std::vector<Value>;
  using Object = s2c::Object;

  Value() = default;  // null
  static Value make_bool(bool b) { return Value(Storage(std::in_place_type<bool>, b)); }
  static Value make_number(Number n) { return Value(Storage(std::in_place_type<Number>, n)); }
  static Value make_string(std::string s) {
    return Value(Storage(std::in_place_type<std::string>, std::move(s)));
  }
  static Value make_array(Array a) { return Value(Storage(std::in_place_type<Array>, std::move(a))); }
  static Value make_object(Object o) {
    return Value(Storage(std::in_place_type<Object>, std::move(o)));
  }

  Type type() const { return static_cast<Type>(v_.index()); }
  static const char* type_name(Type t) {
    static const char* const names[] = {"null", "boolean", "number", "string", "array", "object"};
    return names[static_cast<int>(t)];
  }
  const char* type_name() const { return type_name(type()); }

  // Unchecked accessors: call only after checking type().
  bool as_bool() const { return std::get<bool>(v_); }
  const Number& as_number() const { return std::get<Number>(v_); }
  const std::string& as_string() const { return std::get<std::string>(v_); }
  const Array& as_array() const { return std::get<Array>(v_); }
  const Object& as_object() const { return std::get<Object>(v_); }

 private:
  using Storage = std::variant<std::nullptr_t, bool, Number, std::string, Array, Object>;
  explicit Value(Storage s) : v_(std::move(s)) {}
  Storage v_;
};

inline const Value* Object::find(std::string_view key) const {
  for (const Item& it : items_)
    if (it.first == key) return &it.second;
  return nullptr;
}
inline void Object::add(std::string key, Value v) {
  if (find(key)) throw ParseError("duplicate object key '" + key + "'");
  items_.emplace_back(std::move(key), std::move(v));
}

// Tag for the generated "construct from member values" constructors.
struct from_members_t {};
inline constexpr from_members_t from_members{};

// Builds an object Value from literal key/value pairs (used by embedded data).
inline Value object_of(std::initializer_list<std::pair<std::string, Value>> items) {
  Object o;
  for (const auto& kv : items) o.add(kv.first, kv.second);
  return Value::make_object(std::move(o));
}

// Parses a complete JSON document (implemented with Wuffs in s2c_runtime.cpp).
// Throws ParseError on malformed input, including trailing garbage.
Value parse(std::string_view text);

// ---------------------------------------------------------------------------
// Validation helpers used by generated code.
// ---------------------------------------------------------------------------

[[noreturn]] inline void fail(const std::string& path, const std::string& msg) {
  throw ValidationError(path + ": " + msg);
}
inline std::string join_key(const std::string& path, std::string_view key) {
  return path + "." + std::string(key);
}
inline std::string join_index(const std::string& path, std::size_t i) {
  return path + "[" + std::to_string(i) + "]";
}
inline void expect(const Value& v, Value::Type t, const std::string& path) {
  if (v.type() != t)
    fail(path, std::string("expected ") + Value::type_name(t) + ", got " + v.type_name());
}
inline std::size_t utf8_length(const std::string& s) {
  std::size_t n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

inline const Object& require_object(const Value& v, const std::string& path,
                                    std::initializer_list<const char*> known, bool allow_extra) {
  expect(v, Value::Type::Object, path);
  const Object& o = v.as_object();
  if (!allow_extra) {
    for (const auto& item : o.items()) {
      bool ok = false;
      for (const char* k : known) ok = ok || item.first == k;
      if (!ok) fail(join_key(path, item.first), "unexpected property");
    }
  }
  return o;
}

// A "reader" is any callable (const Value&, const std::string& path) -> T that
// throws ValidationError on mismatch.

inline auto string_reader(std::optional<std::size_t> min_len, std::optional<std::size_t> max_len,
                          std::vector<std::string> allowed) {
  return [=](const Value& v, const std::string& path) -> std::string {
    expect(v, Value::Type::String, path);
    const std::string& s = v.as_string();
    if (min_len || max_len) {
      std::size_t n = utf8_length(s);
      if (min_len && n < *min_len) fail(path, "string shorter than minLength " + std::to_string(*min_len));
      if (max_len && n > *max_len) fail(path, "string longer than maxLength " + std::to_string(*max_len));
    }
    if (!allowed.empty() && std::find(allowed.begin(), allowed.end(), s) == allowed.end())
      fail(path, "'" + s + "' is not one of the allowed values");
    return s;
  };
}

inline auto int_reader(std::optional<std::int64_t> lo, std::optional<std::int64_t> hi) {
  return [=](const Value& v, const std::string& path) -> std::int64_t {
    expect(v, Value::Type::Number, path);
    const Value::Number& n = v.as_number();
    if (!n.is_int) fail(path, "expected integer");
    if (lo && n.i < *lo) fail(path, "value below minimum " + std::to_string(*lo));
    if (hi && n.i > *hi) fail(path, "value above maximum " + std::to_string(*hi));
    return n.i;
  };
}

inline auto number_reader(std::optional<double> lo, std::optional<double> hi) {
  return [=](const Value& v, const std::string& path) -> double {
    expect(v, Value::Type::Number, path);
    double d = v.as_number().d;
    if (lo && d < *lo) fail(path, "value below minimum " + std::to_string(*lo));
    if (hi && d > *hi) fail(path, "value above maximum " + std::to_string(*hi));
    return d;
  };
}

inline auto bool_reader() {
  return [](const Value& v, const std::string& path) -> bool {
    expect(v, Value::Type::Bool, path);
    return v.as_bool();
  };
}

inline auto null_reader() {
  return [](const Value& v, const std::string& path) -> std::nullptr_t {
    expect(v, Value::Type::Null, path);
    return nullptr;
  };
}

// Unconstrained value (schema {} or anything we don't model).
inline auto any_reader() {
  return [](const Value& v, const std::string&) -> Value { return v; };
}

// Free-form object ("type": "object" with no "properties").
inline auto any_object_reader() {
  return [](const Value& v, const std::string& path) -> Value {
    expect(v, Value::Type::Object, path);
    return v;
  };
}

template <class T>
auto object_reader() {
  return [](const Value& v, const std::string& path) -> T { return T(v, path); };
}

template <class Item>
auto array_reader(Item item, std::optional<std::size_t> min_items, std::optional<std::size_t> max_items) {
  using R = std::decay_t<decltype(item(std::declval<const Value&>(), std::declval<const std::string&>()))>;
  return [=](const Value& v, const std::string& path) -> std::vector<R> {
    expect(v, Value::Type::Array, path);
    const Value::Array& a = v.as_array();
    if (min_items && a.size() < *min_items) fail(path, "fewer than minItems " + std::to_string(*min_items));
    if (max_items && a.size() > *max_items) fail(path, "more than maxItems " + std::to_string(*max_items));
    std::vector<R> out;
    out.reserve(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) out.push_back(item(a[i], join_index(path, i)));
    return out;
  };
}

template <class Inner>
auto nullable(Inner inner) {
  using R = std::decay_t<decltype(inner(std::declval<const Value&>(), std::declval<const std::string&>()))>;
  return [=](const Value& v, const std::string& path) -> std::optional<R> {
    if (v.type() == Value::Type::Null) return std::nullopt;
    return inner(v, path);
  };
}

template <class T>
struct is_optional : std::false_type {};
template <class T>
struct is_optional<std::optional<T>> : std::true_type {};

template <class Reader>
auto required(const Object& obj, std::string_view key, const std::string& path, const Reader& reader) {
  const Value* f = obj.find(key);
  if (!f) fail(path, "missing required property '" + std::string(key) + "'");
  return reader(*f, join_key(path, key));
}

template <class Reader>
auto optional_prop(const Object& obj, std::string_view key, const std::string& path, const Reader& reader) {
  using R = std::decay_t<decltype(reader(std::declval<const Value&>(), path))>;
  using Out = std::conditional_t<is_optional<R>::value, R, std::optional<R>>;
  const Value* f = obj.find(key);
  if (!f) return Out{};
  return Out(reader(*f, join_key(path, key)));
}

// ---------------------------------------------------------------------------
// Serialization, and the "writers" used by generated to_value() / update().
// ---------------------------------------------------------------------------

namespace detail {

inline bool valid_utf8(const std::string& s) {
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
  const unsigned char* end = p + s.size();
  while (p < end) {
    const unsigned char c = *p++;
    if (c < 0x80) continue;
    std::ptrdiff_t extra;
    std::uint32_t cp, min;
    if (c >= 0xC2 && c <= 0xDF) { extra = 1; cp = c & 0x1Fu; min = 0x80; }
    else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0Fu; min = 0x800; }
    else if (c >= 0xF0 && c <= 0xF4) { extra = 3; cp = c & 0x07u; min = 0x10000; }
    else return false;
    if (end - p < extra) return false;
    for (std::ptrdiff_t i = 0; i < extra; ++i) {
      if ((p[i] & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    p += extra;
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
  }
  return true;
}

inline void write_string(std::string& out, const std::string& s) {
  if (!valid_utf8(s)) throw ValidationError("string is not valid UTF-8");
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

inline void write_number(std::string& out, const Value::Number& n) {
  if (n.is_int) {
    out += std::to_string(n.i);
    return;
  }
  if (!std::isfinite(n.d)) throw ValidationError("cannot represent a non-finite number in JSON");
  char buf[40];
  // Shortest of 15..17 significant digits that reads back as the same double.
  for (int prec = 15; prec <= 17; ++prec) {
    std::snprintf(buf, sizeof buf, "%.*g", prec, n.d);
    if (prec == 17 || std::strtod(buf, nullptr) == n.d) break;
  }
  out += buf;
}

inline void write_value(std::string& out, const Value& v, int indent, int depth) {
  auto newline = [&](int d) {
    if (indent < 0) return;
    out += '\n';
    out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(d), ' ');
  };
  switch (v.type()) {
    case Value::Type::Null: out += "null"; break;
    case Value::Type::Bool: out += v.as_bool() ? "true" : "false"; break;
    case Value::Type::Number: write_number(out, v.as_number()); break;
    case Value::Type::String: write_string(out, v.as_string()); break;
    case Value::Type::Array: {
      const Value::Array& a = v.as_array();
      if (a.empty()) { out += "[]"; break; }
      out += '[';
      for (std::size_t i = 0; i < a.size(); ++i) {
        if (i) out += ',';
        newline(depth + 1);
        write_value(out, a[i], indent, depth + 1);
      }
      newline(depth);
      out += ']';
      break;
    }
    case Value::Type::Object: {
      const Object& o = v.as_object();
      if (o.size() == 0) { out += "{}"; break; }
      out += '{';
      bool first = true;
      for (const auto& kv : o.items()) {
        if (!first) out += ',';
        first = false;
        newline(depth + 1);
        write_string(out, kv.first);
        out += indent < 0 ? ":" : ": ";
        write_value(out, kv.second, indent, depth + 1);
      }
      newline(depth);
      out += '}';
      break;
    }
  }
}

}  // namespace detail

// Serializes a Value as JSON. indent < 0 gives compact output; indent >= 0 pretty-prints
// with that many spaces per level. Object keys keep their insertion order. Throws
// ValidationError for text that is not valid UTF-8 and for NaN/infinity.
inline std::string to_json(const Value& v, int indent = -1) {
  std::string out;
  detail::write_value(out, v, indent, 0);
  return out;
}

// A "writer" is any callable (const T&, const Value* existing, const std::string& path) -> Value,
// the inverse of a reader. `existing` is the matching part of the document being updated (or
// nullptr); only object and array writers use it, to keep what the class does not model.

inline auto string_writer() {
  return [](const std::string& s, const Value*, const std::string& path) -> Value {
    if (!detail::valid_utf8(s)) fail(path, "string is not valid UTF-8");
    return Value::make_string(s);
  };
}

inline auto int_writer() {
  return [](std::int64_t i, const Value*, const std::string&) -> Value {
    Value::Number n;
    n.i = i;
    n.d = static_cast<double>(i);
    n.is_int = true;
    return Value::make_number(n);
  };
}

inline auto number_writer() {
  return [](double d, const Value*, const std::string& path) -> Value {
    if (!std::isfinite(d)) fail(path, "cannot represent a non-finite number in JSON");
    Value::Number n;
    n.d = d;
    if (d == std::floor(d) && std::fabs(d) < 9.2e18) {  // same rule as the parser
      n.i = static_cast<std::int64_t>(d);
      n.is_int = true;
    }
    return Value::make_number(n);
  };
}

inline auto bool_writer() {
  return [](bool b, const Value*, const std::string&) -> Value { return Value::make_bool(b); };
}

inline auto null_writer() {
  return [](std::nullptr_t, const Value*, const std::string&) -> Value { return Value(); };
}

// Values the class holds verbatim (schema {} or free-form objects) are written back as they are.
inline auto any_writer() {
  return [](const Value& v, const Value*, const std::string&) -> Value { return v; };
}

template <class T>
auto object_writer() {
  return [](const T& o, const Value* existing, const std::string& path) -> Value {
    return o.to_value(existing, path);
  };
}

// Elements are merged with the element at the same index of the existing array (if any), so
// properties the class does not model survive edits made in place.
template <class Item>
auto array_writer(Item item) {
  return [=](const auto& vec, const Value* existing, const std::string& path) -> Value {
    const Value::Array* old =
        existing && existing->type() == Value::Type::Array ? &existing->as_array() : nullptr;
    Value::Array out;
    out.reserve(vec.size());
    for (std::size_t i = 0; i < vec.size(); ++i)
      out.push_back(item(vec[i], old && i < old->size() ? &(*old)[i] : nullptr, join_index(path, i)));
    return Value::make_array(std::move(out));
  };
}

template <class Inner>
auto nullable_writer(Inner inner) {
  return [=](const auto& opt, const Value* existing, const std::string& path) -> Value {
    if (!opt) return Value();
    return inner(*opt, existing, path);
  };
}

// Builds the object Value for a generated class, merging into the existing document's object:
//  - keys keep the order they had in the existing object; new keys are appended
//  - keys the class does not know are kept, unless the schema has additionalProperties: false
class ObjectWriter {
 public:
  ObjectWriter(const Value* existing, std::initializer_list<const char*> known, bool keep_extra) {
    if (!existing || existing->type() != Value::Type::Object) return;
    old_ = &existing->as_object();
    for (const auto& item : old_->items()) {
      bool is_known = false;
      for (const char* k : known) is_known = is_known || item.first == k;
      if (is_known) slots_.push_back(Slot{item.first, Value(), false});  // placeholder keeps position
      else if (keep_extra) slots_.push_back(Slot{item.first, item.second, true});
    }
  }

  // Required property (also used for a required nullable one: the writer maps nullopt to null).
  template <class T, class W>
  void put_required(std::string_view key, const T& v, const W& writer, const std::string& path) {
    set(key, writer(v, child(key), join_key(path, key)));
  }

  // Optional property: nullopt removes the key.
  template <class T, class W>
  void put_optional(std::string_view key, const std::optional<T>& v, const W& writer,
                    const std::string& path) {
    if (v) put_required(key, *v, writer, path);
  }

  // Optional *and* nullable property (std::optional cannot tell "absent" from "null"): nullopt
  // becomes null if the key is already in the document, and stays absent otherwise.
  template <class T, class W>
  void put_nullable(std::string_view key, const std::optional<T>& v, const W& writer,
                    const std::string& path) {
    if (v || child(key)) put_required(key, v, writer, path);
  }

  Value finish() {
    Object o;
    for (Slot& s : slots_)
      if (s.present) o.add(std::move(s.key), std::move(s.val));
    return Value::make_object(std::move(o));
  }

 private:
  struct Slot {
    std::string key;
    Value val;
    bool present;
  };

  const Value* child(std::string_view key) const { return old_ ? old_->find(key) : nullptr; }

  void set(std::string_view key, Value v) {
    for (Slot& s : slots_) {
      if (s.key == key) {
        s.val = std::move(v);
        s.present = true;
        return;
      }
    }
    slots_.push_back(Slot{std::string(key), std::move(v), true});
  }

  const Object* old_ = nullptr;
  std::vector<Slot> slots_;
};

}  // namespace s2c
