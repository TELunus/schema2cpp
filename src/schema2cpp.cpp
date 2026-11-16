// schema2cpp - read a JSON Schema and emit a header with matching C++ classes.
//
//   schema2cpp schema.json [-o out.hpp] [--name RootClass] [--namespace ns]
//
// With --data, the data file is checked against the schema while generating, and the
// header also gets an accessor returning that data as a ready-built object (no runtime
// parsing); generation fails if the data does not match.
//
// Each generated class also has update(json_text), which writes the object's members back into
// a JSON document and keeps whatever the schema does not describe.
//
// Each generated class has constructors that take JSON text (or an already
// parsed s2c::Value) and throw s2c::ParseError (malformed JSON) or
// s2c::ValidationError (JSON that doesn't match the schema).

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <utility>

#include "s2c_runtime.hpp"

using s2c::Value;

namespace {

[[noreturn]] void die(const std::string& msg) { throw std::runtime_error(msg); }

// ---------- naming helpers ----------

std::string camel(const std::string& s) {
  std::string out;
  bool up = true;
  for (unsigned char c : s) {
    if (std::isalnum(c)) {
      out += up ? static_cast<char>(std::toupper(c)) : static_cast<char>(c);
      up = false;
    } else {
      up = true;
    }
  }
  if (out.empty()) out = "Type";
  if (std::isdigit(static_cast<unsigned char>(out[0]))) out = "T" + out;
  return out;
}

const std::set<std::string> kKeywords = {
    "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case", "catch", "char",
    "class", "const", "constexpr", "continue", "decltype", "default", "delete", "do", "double",
    "else", "enum", "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
    "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "not", "nullptr",
    "operator", "or", "private", "protected", "public", "register", "return", "short", "signed",
    "sizeof", "static", "struct", "switch", "template", "this", "throw", "true", "try",
    "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void",
    "volatile", "while", "xor", "NULL", "jobj", "jpath", "Tag",
    // names of generated methods and of locals inside them
    "update", "to_value", "jexisting", "jw"};

std::string ident(const std::string& s, const std::string& class_name) {
  std::string out;
  for (unsigned char c : s) out += std::isalnum(c) ? static_cast<char>(c) : '_';
  if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) out = "_" + out;
  if (kKeywords.count(out) || out == class_name) out += "_";
  return out;
}

std::string cpp_str(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20 || c == 0x7f) {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\%03o", c);  // 3-digit octal: never swallows next char
      out += buf;
    } else {
      out += static_cast<char>(c);
    }
  }
  return out + "\"";
}

std::string one_line(const std::string& s) {
  std::string out;
  for (char c : s) out += (c == '\n' || c == '\r') ? ' ' : c;
  return out;
}

std::string dbl(double d) {
  char buf[40];
  std::snprintf(buf, sizeof buf, "%.17g", d);
  std::string s = buf;
  if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
  return s;
}

// ---------- schema access helpers ----------

const Value* get(const Value& obj, const char* key) {
  return obj.type() == Value::Type::Object ? obj.as_object().find(key) : nullptr;
}

std::optional<double> get_num(const Value& obj, const char* key) {
  const Value* v = get(obj, key);
  if (!v) return std::nullopt;
  if (v->type() != Value::Type::Number) die(std::string("\"") + key + "\" must be a number");
  return v->as_number().d;
}

std::string opt_size(const Value& obj, const char* key) {
  auto n = get_num(obj, key);
  if (!n) return "std::nullopt";
  if (*n < 0 || *n != std::floor(*n)) die(std::string("\"") + key + "\" must be a non-negative integer");
  return std::to_string(static_cast<unsigned long long>(*n));
}

// ---------- the generator ----------

struct Resolved {
  std::string type;    // C++ type of the member
  std::string reader;  // C++ expression producing a reader for that type
  std::string writer;  // C++ expression producing the matching writer (see s2c_runtime.hpp)
  std::string cls;     // generated class name, if this is an object class
  Resolved(std::string t = "", std::string r = "", std::string w = "", std::string c = "")
      : type(std::move(t)), reader(std::move(r)), writer(std::move(w)), cls(std::move(c)) {}
};

struct TypeInfo {
  std::string type;  // "string", "integer", "object", ... or "" for "anything"
  bool nullable = false;
};

class Generator {
 public:
  Generator(const Value& root, std::string ns, std::string schema_dir)
      : root_(root), ns_(std::move(ns)), schema_dir_(std::move(schema_dir)) {}

  std::string run(const std::string& root_name, const Value* data = nullptr,
                  const std::string& data_name = "", const std::string& data_file = "") {
    Resolved r = resolve(root_, root_name);
    if (r.reader.rfind("s2c::object_reader<", 0) != 0)
      die("the root schema must describe an object");
    std::string embedded;
    if (data) {
      embedded = "/// Embedded from " + one_line(data_file) +
                 "; checked against the schema when this header was generated.\n"
                 "inline const " + r.cls + "& " + data_name + "() {\n  static const " + r.cls + " value = " +
                 data_expr(root_, *data, "$") + ";\n  return value;\n}\n\n";
    }
    std::ostringstream o;
    o << "// Generated by schema2cpp. DO NOT EDIT.\n#pragma once\n\n"
         "#include <cstdint>\n#include <optional>\n#include <string>\n#include <string_view>\n"
         "#include <vector>\n\n#include \"s2c_runtime.hpp\"\n\n";
    if (!ns_.empty()) o << "namespace " << ns_ << " {\n\n";
    o << classes_ << embedded;
    if (!ns_.empty()) o << "}  // namespace " << ns_ << "\n";
    return o.str();
  }

 private:
  const Value& root_;
  std::string ns_;
  std::string schema_dir_;
  std::string classes_;
  std::set<std::string> used_names_;
  std::map<std::string, Resolved> ref_cache_;
  std::set<std::string> ref_in_progress_;
  std::map<std::string, Value> loaded_schemas_;
  std::vector<std::unique_ptr<Value>> synthesized_;

  void warn(const std::string& msg) { std::cerr << "warning: " << msg << "\n"; }

  std::string unique_name(std::string base) {
    std::string name = base;
    for (int n = 2; used_names_.count(name); ++n) name = base + std::to_string(n);
    used_names_.insert(name);
    return name;
  }

  const Value& load_schema(const std::string& path) {
    auto it = loaded_schemas_.find(path);
    if (it != loaded_schemas_.end()) return it->second;
    std::ifstream in(path, std::ios::binary);
    if (!in) die("cannot open referenced schema " + path);
    std::stringstream buf;
    buf << in.rdbuf();
    Value v = s2c::parse(buf.str());
    return loaded_schemas_.emplace(path, std::move(v)).first->second;
  }

  const Value& follow_ref(const std::string& ref) {
    if (ref.rfind("#", 0) != 0) {
      if (schema_dir_.empty())
        die("only local \"$ref\"s are supported unless --schema-dir is given, got " + ref);
      std::string path = schema_dir_;
      if (!path.empty() && path.back() != '/' && path.back() != '\\') path += '/';
      path += ref;
      return load_schema(path);
    }
    const Value* cur = &root_;
    std::stringstream ss(ref.substr(1));
    std::string seg;
    std::getline(ss, seg, '/');  // leading empty segment
    while (std::getline(ss, seg, '/')) {
      std::string key;
      for (size_t i = 0; i < seg.size(); ++i) {
        if (seg[i] == '~' && i + 1 < seg.size() && (seg[i + 1] == '0' || seg[i + 1] == '1'))
          key += (seg[++i] == '0') ? '~' : '/';
        else
          key += seg[i];
      }
      cur = get(*cur, key.c_str());
      if (!cur) die("cannot resolve $ref " + ref);
    }
    return *cur;
  }

  // Flatten a schema that has $ref + local keywords into a merged schema.
  // If the schema has no $ref, or only $ref (+metadata), returns the original.
  const Value& flatten_ref(const Value& schema) {
    const Value* ref = get(schema, "$ref");
    if (!ref || ref->type() != Value::Type::String) return schema;

    // Check if schema only contains $ref and metadata (no additional keywords to merge).
    bool has_extra = false;
    for (const auto& kv : schema.as_object().items()) {
      const std::string& k = kv.first;
      if (k != "$ref" && k != "$id" && k != "$schema" && k != "title" &&
          k != "description" && k != "gltf_detailedDescription" &&
          k != "gltf_sectionDescription") {
        has_extra = true;
        break;
      }
    }
    if (!has_extra) return schema;

    // Merge: follow ref target, then overlay local keywords.
    // Recursively flatten the base first.
    const Value& base = flatten_ref(follow_ref(ref->as_string()));
    if (base.type() != Value::Type::Object) return base;

    synthesized_.push_back(std::make_unique<Value>());
    Value& merged = *synthesized_.back();
    s2c::Object obj;

    // Copy all keywords from base except properties/required/additionalProperties.
    for (const auto& kv : base.as_object().items()) {
      const std::string& k = kv.first;
      if (k == "properties" || k == "required" || k == "additionalProperties")
        continue;
      obj.add(k, kv.second);
    }

    // Merge properties: base then local (local wins on conflict).
    std::vector<std::pair<std::string, Value>> props;
    std::set<std::string> seen;
    auto add_props = [&](const Value& src) {
      if (const Value* p = get(src, "properties"); p && p->type() == Value::Type::Object) {
        for (const auto& kv : p->as_object().items()) {
          if (!seen.count(kv.first)) {
            props.emplace_back(kv.first, kv.second);
            seen.insert(kv.first);
          }
        }
      }
    };
    add_props(base);
    add_props(schema);
    if (!props.empty()) {
      s2c::Object pobj;
      for (auto& pr : props) pobj.add(pr.first, pr.second);
      obj.add("properties", Value::make_object(std::move(pobj)));
    }

    // Merge required: union of base and local.
    std::set<std::string> req;
    auto add_req = [&](const Value& src) {
      if (const Value* r = get(src, "required"); r && r->type() == Value::Type::Array) {
        for (const Value& v : r->as_array())
          if (v.type() == Value::Type::String) req.insert(v.as_string());
      }
    };
    add_req(base);
    add_req(schema);
    if (!req.empty()) {
      Value::Array arr;
      for (const auto& r : req) arr.push_back(Value::make_string(r));
      obj.add("required", Value::make_array(std::move(arr)));
    }

    // additionalProperties: local overrides base.
    const Value* lap = get(schema, "additionalProperties");
    const Value* bap = get(base, "additionalProperties");
    if (lap) obj.add("additionalProperties", *lap);
    else if (bap) obj.add("additionalProperties", *bap);

    merged = Value::make_object(std::move(obj));
    return merged;
  }

  // Infers the common type of an anyOf/oneOf union. The glTF schema uses the
  // idiom [{const: A, type: T}, ..., {type: T}] where the trailing branch accepts
  // any T, so the union's effective type is simply T. Returns "" when the
  // branches disagree or impose no type.
  static std::string union_type(const Value::Array& branches) {
    std::string found;
    for (const Value& b : branches) {
      std::string bt;
      if (const Value* t = get(b, "type"); t && t->type() == Value::Type::String) {
        bt = t->as_string();
      } else if (const Value* c = get(b, "const"); c) {
        switch (c->type()) {
          case Value::Type::Number: bt = c->as_number().is_int ? "integer" : "number"; break;
          case Value::Type::String: bt = "string"; break;
          case Value::Type::Bool: bt = "boolean"; break;
          case Value::Type::Null: bt = "null"; break;
          default: break;
        }
      }
      if (bt.empty()) continue;  // branch imposes no type
      if (found.empty()) found = bt;
      else if (found != bt) {
        // "integer" is a subset of "number"; a union of both is "number".
        if ((found == "integer" && bt == "number") || (found == "number" && bt == "integer"))
          found = "number";
        else
          return "";
      }
    }
    return found;
  }

  TypeInfo type_info(const Value& schema, const std::string& name_hint) {
    std::string type;
    bool nullable = false;
    if (const Value* t = get(schema, "type")) {
      std::vector<std::string> names;
      if (t->type() == Value::Type::String) {
        names.push_back(t->as_string());
      } else if (t->type() == Value::Type::Array) {
        for (const Value& e : t->as_array()) {
          if (e.type() != Value::Type::String) die("\"type\" entries must be strings");
          names.push_back(e.as_string());
        }
      } else {
        die("\"type\" must be a string or array");
      }
      for (const auto& n : names) {
        if (n == "null" && names.size() > 1) nullable = true;
        else if (type.empty()) type = n;
        else die("union types are not supported at " + name_hint);
      }
      if (type.empty()) type = "null";
    } else if (const Value* u = get(schema, "anyOf"); u && u->type() == Value::Type::Array) {
      type = union_type(u->as_array());
    } else if (const Value* u = get(schema, "oneOf"); u && u->type() == Value::Type::Array) {
      type = union_type(u->as_array());
    } else if (get(schema, "properties")) {
      type = "object";
    } else if (get(schema, "items")) {
      type = "array";
    } else if (const Value* e = get(schema, "enum")) {
      type = (e->type() == Value::Type::Array && !e->as_array().empty() &&
              e->as_array()[0].type() == Value::Type::String)
                 ? "string"
                 : "";
    }
    return {type, nullable};
  }

  std::map<const Value*, Resolved> node_cache_;

  // Memoized per schema node, so a class is emitted once however often it is visited.
  Resolved resolve(const Value& schema, const std::string& name_hint) {
    auto it = node_cache_.find(&schema);
    if (it != node_cache_.end()) return it->second;
    Resolved r = resolve_uncached(schema, name_hint);
    node_cache_[&schema] = r;
    return r;
  }

  Resolved resolve_uncached(const Value& schema, const std::string& name_hint) {
    if (schema.type() == Value::Type::Bool) return {"s2c::Value", "s2c::any_reader()", "s2c::any_writer()"};
    if (schema.type() != Value::Type::Object) die("a schema must be an object or boolean");

    if (const Value* ref = get(schema, "$ref")) {
      if (ref->type() != Value::Type::String) die("\"$ref\" must be a string");
      bool has_extra = get(schema, "properties") != nullptr || get(schema, "required") != nullptr ||
                       get(schema, "additionalProperties") != nullptr;
      if (has_extra) {
        const Value& merged = flatten_ref(schema);
        return resolve(merged, name_hint);
      }
      const std::string& key = ref->as_string();
      auto hit = ref_cache_.find(key);
      if (hit != ref_cache_.end()) return hit->second;
      if (!ref_in_progress_.insert(key).second)
        die("recursive schemas are not supported ($ref " + key + ")");
      std::string tail = key.substr(key.find_last_of('/') + 1);
      Resolved r = resolve(follow_ref(key), camel(tail));
      ref_in_progress_.erase(key);
      ref_cache_[key] = r;
      return r;
    }

    for (const char* kw : {"anyOf", "oneOf"}) {
      const Value* u = get(schema, kw);
      if (u && u->type() == Value::Type::Array && union_type(u->as_array()).empty())
        warn(std::string("'") + kw + "' has branches with no common type and is ignored at " + name_hint);
    }
    for (const char* kw : {"allOf", "not", "if", "patternProperties"})
      if (get(schema, kw)) warn(std::string("'") + kw + "' is not supported and is ignored at " + name_hint);
    for (const char* kw : {"pattern", "format", "uniqueItems", "multipleOf", "const", "exclusiveMinimum",
                           "exclusiveMaximum"})
      if (get(schema, kw)) warn(std::string("'") + kw + "' is not enforced at " + name_hint);

    TypeInfo ti = type_info(schema, name_hint);
    const std::string& type = ti.type;
    const bool nullable = ti.nullable;

    Resolved r;
    if (type == "string") {
      std::vector<std::string> allowed;
      if (const Value* e = get(schema, "enum")) {
        if (e->type() != Value::Type::Array) die("\"enum\" must be an array");
        for (const Value& v : e->as_array()) {
          if (v.type() != Value::Type::String) die("only string enums are supported at " + name_hint);
          allowed.push_back(v.as_string());
        }
      }
      std::string list;
      for (size_t i = 0; i < allowed.size(); ++i) list += (i ? ", " : "") + cpp_str(allowed[i]);
      r = {"std::string", "s2c::string_reader(" + opt_size(schema, "minLength") + ", " +
                              opt_size(schema, "maxLength") + ", {" + list + "})",
           "s2c::string_writer()"};
    } else if (type == "integer") {
      auto lo = get_num(schema, "minimum"), hi = get_num(schema, "maximum");
      r = {"std::int64_t",
           "s2c::int_reader(" +
               (lo ? std::to_string(static_cast<long long>(std::ceil(*lo))) : std::string("std::nullopt")) +
               ", " +
               (hi ? std::to_string(static_cast<long long>(std::floor(*hi))) : std::string("std::nullopt")) +
               ")",
           "s2c::int_writer()"};
    } else if (type == "number") {
      auto lo = get_num(schema, "minimum"), hi = get_num(schema, "maximum");
      r = {"double", "s2c::number_reader(" + (lo ? dbl(*lo) : std::string("std::nullopt")) + ", " +
                         (hi ? dbl(*hi) : std::string("std::nullopt")) + ")",
         "s2c::number_writer()"};
    } else if (type == "boolean") {
      r = {"bool", "s2c::bool_reader()", "s2c::bool_writer()"};
    } else if (type == "null") {
      r = {"std::nullptr_t", "s2c::null_reader()", "s2c::null_writer()"};
    } else if (type == "array") {
      Resolved item{"s2c::Value", "s2c::any_reader()", "s2c::any_writer()"};
      if (const Value* items = get(schema, "items")) item = resolve(*items, name_hint + "Item");
      r = {"std::vector<" + item.type + ">", "s2c::array_reader(" + item.reader + ", " +
                                                 opt_size(schema, "minItems") + ", " +
                                                 opt_size(schema, "maxItems") + ")",
         "s2c::array_writer(" + item.writer + ")"};
    } else if (type == "object") {
      r = get(schema, "properties") ? emit_class(schema, name_hint)
                                    : Resolved{"s2c::Value", "s2c::any_object_reader()", "s2c::any_writer()"};
    } else if (type.empty()) {
      r = {"s2c::Value", "s2c::any_reader()", "s2c::any_writer()"};
    } else {
      die("unknown type '" + type + "' at " + name_hint);
    }

    if (nullable) r = {"std::optional<" + r.type + ">", "s2c::nullable(" + r.reader + ")",
           "s2c::nullable_writer(" + r.writer + ")", r.cls};
    return r;
  }


  // ---------- embedded data: validate JSON against the schema, emit a C++ expression ----------

  static std::string value_expr(const Value& v) {
    switch (v.type()) {
      case Value::Type::Null: return "s2c::Value()";
      case Value::Type::Bool: return std::string("s2c::Value::make_bool(") + (v.as_bool() ? "true" : "false") + ")";
      case Value::Type::Number: {
        const auto& n = v.as_number();
        return "s2c::Value::make_number(s2c::Value::Number{" + dbl(n.d) + ", " +
               std::to_string(static_cast<long long>(n.i)) + "LL, " + (n.is_int ? "true" : "false") + "})";
      }
      case Value::Type::String: return "s2c::Value::make_string(" + cpp_str(v.as_string()) + ")";
      case Value::Type::Array: {
        std::string out = "s2c::Value::make_array(s2c::Value::Array{";
        bool first = true;
        for (const Value& e : v.as_array()) out += (first ? "" : ", ") + value_expr(e), first = false;
        return out + "})";
      }
      case Value::Type::Object: {
        std::string out = "s2c::object_of({";
        bool first = true;
        for (const auto& kv : v.as_object().items())
          out += (first ? "{" : ", {") + cpp_str(kv.first) + ", " + value_expr(kv.second) + "}", first = false;
        return out + "})";
      }
    }
    return "";
  }

  std::string data_expr(const Value& schema, const Value& data, const std::string& path) {
    if (schema.type() == Value::Type::Bool) return value_expr(data);
    if (const Value* ref = get(schema, "$ref")) {
      bool has_extra = get(schema, "properties") != nullptr || get(schema, "required") != nullptr ||
                       get(schema, "additionalProperties") != nullptr;
      if (has_extra) {
        const Value& merged = flatten_ref(schema);
        return data_expr(merged, data, path);
      }
      return data_expr(follow_ref(ref->as_string()), data, path);
    }

    TypeInfo ti = type_info(schema, path);
    if (data.type() == Value::Type::Null && ti.nullable) return "std::nullopt";
    const std::string& t = ti.type;
    auto expect_type = [&](Value::Type ty) {
      if (data.type() != ty)
        die(path + ": expected " + Value::type_name(ty) + ", got " + data.type_name());
    };
    auto lo = get_num(schema, "minimum"), hi = get_num(schema, "maximum");

    if (t == "string") {
      expect_type(Value::Type::String);
      const std::string& s = data.as_string();
      auto n = s2c::utf8_length(s);
      if (auto m = get_num(schema, "minLength"); m && n < *m) die(path + ": string shorter than minLength");
      if (auto m = get_num(schema, "maxLength"); m && n > *m) die(path + ": string longer than maxLength");
      if (const Value* e = get(schema, "enum")) {
        bool ok = false;
        for (const Value& v : e->as_array()) ok = ok || (v.type() == Value::Type::String && v.as_string() == s);
        if (!ok) die(path + ": '" + s + "' is not one of the allowed values");
      }
      return "std::string(" + cpp_str(s) + ")";
    }
    if (t == "integer") {
      expect_type(Value::Type::Number);
      const auto& n = data.as_number();
      if (!n.is_int) die(path + ": expected integer");
      if (lo && n.d < std::ceil(*lo)) die(path + ": value below minimum");
      if (hi && n.d > std::floor(*hi)) die(path + ": value above maximum");
      return "std::int64_t(" + std::to_string(static_cast<long long>(n.i)) + "LL)";
    }
    if (t == "number") {
      expect_type(Value::Type::Number);
      double d = data.as_number().d;
      if (lo && d < *lo) die(path + ": value below minimum");
      if (hi && d > *hi) die(path + ": value above maximum");
      return "double(" + dbl(d) + ")";
    }
    if (t == "boolean") {
      expect_type(Value::Type::Bool);
      return data.as_bool() ? "true" : "false";
    }
    if (t == "null") {
      expect_type(Value::Type::Null);
      return "nullptr";
    }
    if (t == "array") {
      expect_type(Value::Type::Array);
      const auto& a = data.as_array();
      if (auto m = get_num(schema, "minItems"); m && a.size() < *m) die(path + ": fewer than minItems");
      if (auto m = get_num(schema, "maxItems"); m && a.size() > *m) die(path + ": more than maxItems");
      const Value* items = get(schema, "items");
      Resolved item("s2c::Value");
      if (items) item = resolve(*items, "");
      std::string out = "std::vector<" + item.type + ">{";
      for (size_t i = 0; i < a.size(); ++i)
        out += std::string(i ? ", " : "") +
               (items ? data_expr(*items, a[i], path + "[" + std::to_string(i) + "]") : value_expr(a[i]));
      return out + "}";
    }
    if (t == "object") {
      expect_type(Value::Type::Object);
      const Value* props = get(schema, "properties");
      if (!props) return value_expr(data);
      const auto& obj = data.as_object();
      std::set<std::string> required;
      if (const Value* req = get(schema, "required"))
        for (const Value& v : req->as_array()) required.insert(v.as_string());
      const Value* ap = get(schema, "additionalProperties");
      if (ap && ap->type() == Value::Type::Bool && !ap->as_bool())
        for (const auto& kv : obj.items())
          if (!props->as_object().find(kv.first)) die(path + "." + kv.first + ": unexpected property");
      std::string out = resolve(schema, "").cls + "(s2c::from_members";
      for (const auto& [key, sub] : props->as_object().items()) {
        const Value* d = obj.find(key);
        if (d) out += ", " + data_expr(sub, *d, path + "." + key);
        else if (required.count(key)) die(path + ": missing required property '" + key + "'");
        else out += ", std::nullopt";
      }
      return out + ")";
    }
    return value_expr(data);  // no type constraint
  }

  Resolved emit_class(const Value& schema, const std::string& hint) {
    const std::string cls = unique_name(camel(hint));
    const Value* props = get(schema, "properties");
    if (props->type() != Value::Type::Object) die("\"properties\" must be an object");

    std::set<std::string> required;
    if (const Value* req = get(schema, "required")) {
      if (req->type() != Value::Type::Array) die("\"required\" must be an array");
      for (const Value& v : req->as_array()) {
        if (v.type() != Value::Type::String) die("\"required\" entries must be strings");
        required.insert(v.as_string());
      }
    }
    bool allow_extra = true;
    if (const Value* ap = get(schema, "additionalProperties"))
      if (ap->type() == Value::Type::Bool && !ap->as_bool()) allow_extra = false;

    std::ostringstream decls, inits, keys, params, copies, writes;
    std::set<std::string> member_names;
    bool first = true;
    for (const auto& [key, sub] : props->as_object().items()) {
      Resolved r = resolve(sub, cls + camel(key));
      const bool is_req = required.count(key) > 0;
      std::string type = r.type;
      if (!is_req && r.type.rfind("std::optional<", 0) != 0) type = "std::optional<" + r.type + ">";
      std::string id = ident(key, cls);
      while (!member_names.insert(id).second) id += "_";

      if (const Value* d = get(sub, "description"))
        if (d->type() == Value::Type::String) decls << "  /// " << one_line(d->as_string()) << "\n";
      decls << "  " << type << " " << id << ";\n";
      inits << (first ? "      : " : ",\n        ") << id << "(s2c::" << (is_req ? "required" : "optional_prop")
            << "(jobj, " << cpp_str(key) << ", jpath, " << r.reader << "))";
      const char* put = is_req ? "put_required"
                        : r.type.rfind("std::optional<", 0) == 0 ? "put_nullable" : "put_optional";
      writes << "    jw." << put << "(" << cpp_str(key) << ", " << id << ", " << r.writer << ", jpath);\n";
      keys << (first ? "" : ", ") << cpp_str(key);
      params << ", " << type << " " << id;
      copies << (copies.tellp() > 0 ? ", " : "") << id << "(std::move(" << id << "))";
      first = false;
    }
    for (const std::string& k : required)
      if (!props->as_object().find(k)) warn("required property '" + k + "' of " + cls + " has no schema; ignored");

    std::ostringstream o;
    if (const Value* d = get(schema, "description"))
      if (d->type() == Value::Type::String) o << "/// " << one_line(d->as_string()) << "\n";
    o << "class " << cls << " {\n public:\n" << decls.str() << "\n";
    o << "  // Throws s2c::ParseError / s2c::ValidationError.\n";
    o << "  explicit " << cls << "(std::string_view json_text) : " << cls << "(s2c::parse(json_text)) {}\n";
    o << "  explicit " << cls << "(const s2c::Value& value, const std::string& path = \"$\")\n"
      << "      : " << cls << "(s2c::require_object(value, path, {" << keys.str() << "}, "
      << (allow_extra ? "true" : "false") << "), path, Tag{}) {}\n\n";
    o << "  // Builds directly from values, with no parsing or validation (used for embedded data).\n"
      << "  " << cls << "(s2c::from_members_t" << params.str() << ")"
      << (first ? "" : " : " + copies.str()) << " {}\n\n";
    o << "  /// The JSON value of this object. If `jexisting` is an object it is updated rather than\n"
      << "  /// replaced: keys keep their order and new ones are appended"
      << (allow_extra ? ", and keys not in the schema are kept.\n"
                      : "; the schema forbids additional properties, so unknown keys are dropped.\n")
      << "  s2c::Value to_value(const s2c::Value* jexisting = nullptr, const std::string& jpath = \"$\") const {\n"
      << "    s2c::ObjectWriter jw(jexisting, {" << keys.str() << "}, " << (allow_extra ? "true" : "false")
      << ");\n"
      << writes.str() << "    return jw.finish();\n  }\n\n";
    o << "  /// Returns `json_text` (a JSON object) with the members of this object written into it.\n"
      << "  /// Keys the schema does not describe are preserved (not if additionalProperties is false);\n"
      << "  /// key order is kept, but whitespace is not. `indent` < 0 gives compact output.\n"
      << "  /// Throws s2c::ParseError for malformed text, s2c::ValidationError if the text is not an\n"
      << "  /// object or if the current member values do not satisfy the schema.\n"
      << "  std::string update(std::string_view json_text, int indent = -1) const {\n"
      << "    const s2c::Value jdoc = s2c::parse(json_text);\n"
      << "    s2c::expect(jdoc, s2c::Value::Type::Object, \"$\");\n"
      << "    const s2c::Value jmerged = to_value(&jdoc);\n"
      << "    (void)" << cls << "(jmerged);  // never emit JSON that violates the schema\n"
      << "    return s2c::to_json(jmerged, indent);\n  }\n\n";
    o << " private:\n  struct Tag {};\n  " << cls << "(const s2c::Object& jobj, const std::string& jpath, Tag)";
    if (first) o << " {\n    (void)jobj;\n    (void)jpath;\n  }\n";
    else o << "\n" << inits.str() << " {}\n";
    o << "};\n\n";
    classes_ += o.str();
    return {cls, "s2c::object_reader<" + cls + ">()", "s2c::object_writer<" + cls + ">()", cls};
  }
};

}  // namespace

int main(int argc, char** argv) {
  std::string schema_path, out_path, name, ns, data_path, data_name, schema_dir;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) die("missing value after " + a);
      return argv[++i];
    };
    try {
      if (a == "-o") out_path = next();
      else if (a == "--name") name = next();
      else if (a == "--namespace") ns = next();
      else if (a == "--data") data_path = next();
      else if (a == "--data-name") data_name = next();
      else if (schema_dir.empty() && a == "--schema-dir") schema_dir = next();
      else if (schema_path.empty()) schema_path = a;
      else die("unexpected argument " + a);
    } catch (const std::exception& e) {
      std::cerr << "error: " << e.what() << "\n";
      return 2;
    }
  }
  if (schema_path.empty()) {
    std::cerr << "usage: schema2cpp schema.json [-o out.hpp] [--name RootClass] [--namespace ns]\n"
                 "                [--data data.json [--data-name accessor]]\n";
    return 2;
  }

  try {
    std::ifstream in(schema_path, std::ios::binary);
    if (!in) die("cannot open " + schema_path);
    std::stringstream buf;
    buf << in.rdbuf();
    Value schema = s2c::parse(buf.str());

    if (name.empty()) {
      if (const Value* t = get(schema, "title"))
        if (t->type() == Value::Type::String) name = t->as_string();
    }
    if (name.empty()) {
      size_t slash = schema_path.find_last_of("/\\");
      name = schema_path.substr(slash == std::string::npos ? 0 : slash + 1);
      name = name.substr(0, name.find('.'));
    }

    std::optional<Value> data;
    if (!data_path.empty()) {
      std::ifstream din(data_path, std::ios::binary);
      if (!din) die("cannot open " + data_path);
      std::stringstream dbuf;
      dbuf << din.rdbuf();
      data = s2c::parse(dbuf.str());
      if (data_name.empty()) {
        size_t slash = data_path.find_last_of("/\\");
        std::string stem = data_path.substr(slash == std::string::npos ? 0 : slash + 1);
        data_name = ident(stem.substr(0, stem.find('.')), "") + "_data";
      }
    }
    std::string header = Generator(schema, ns, schema_dir).run(name, data ? &*data : nullptr, data_name, data_path);
    if (out_path.empty()) {
      std::cout << header;
    } else {
      std::ofstream out(out_path, std::ios::binary);
      if (!out) die("cannot write " + out_path);
      out << header;
    }
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
