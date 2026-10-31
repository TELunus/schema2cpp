// s2c_runtime.cpp - s2c::parse() implemented with Wuffs' JSON token decoder.
//
// Put wuffs-v0.3.c (from https://github.com/google/wuffs/tree/main/release/c)
// next to this file, or point -I at its directory.

#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__JSON
#include "wuffs-v0.3.c"

#include <cmath>
#include <cstdlib>
#include <memory>
#include <new>

#include "s2c_runtime.hpp"

namespace s2c {
namespace {

void append_utf8(std::string& s, uint32_t cp) {
  if (cp < 0x80) {
    s += static_cast<char>(cp);
  } else if (cp < 0x800) {
    s += static_cast<char>(0xC0 | (cp >> 6));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    s += static_cast<char>(0xE0 | (cp >> 12));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    s += static_cast<char>(0xF0 | (cp >> 18));
    s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// Turns Wuffs' flat token stream into a Value tree.
class Builder {
 public:
  void token(const wuffs_base__token& t, const uint8_t* src, uint64_t pos, uint64_t len) {
    const int64_t vbc = wuffs_base__token__value_base_category(&t);
    const uint64_t vbd = wuffs_base__token__value_base_detail(&t);
    const bool continued = wuffs_base__token__continued(&t);

    switch (vbc) {
      case WUFFS_BASE__TOKEN__VBC__FILLER:
        return;  // whitespace, commas, colons

      case WUFFS_BASE__TOKEN__VBC__STRUCTURE:
        if (vbd & WUFFS_BASE__TOKEN__VBD__STRUCTURE__PUSH) {
          stack_.emplace_back();
          stack_.back().is_dict = (vbd & WUFFS_BASE__TOKEN__VBD__STRUCTURE__TO_DICT) != 0;
        } else {
          Frame f = std::move(stack_.back());
          stack_.pop_back();
          emit(f.is_dict ? Value::make_object(std::move(f.obj)) : Value::make_array(std::move(f.arr)));
        }
        return;

      case WUFFS_BASE__TOKEN__VBC__STRING:
        if (vbd & WUFFS_BASE__TOKEN__VBD__STRING__CONVERT_1_DST_1_SRC_COPY) {
          str_.append(reinterpret_cast<const char*>(src + pos), len);
        } else if (!(vbd & WUFFS_BASE__TOKEN__VBD__STRING__CONVERT_0_DST_1_SRC_DROP)) {
          throw ParseError("unsupported string token");
        }
        break;

      case WUFFS_BASE__TOKEN__VBC__UNICODE_CODE_POINT:
        append_utf8(str_, static_cast<uint32_t>(vbd));
        break;

      case WUFFS_BASE__TOKEN__VBC__LITERAL:
        if (vbd & WUFFS_BASE__TOKEN__VBD__LITERAL__NULL) {
          emit(Value());
        } else if (vbd & WUFFS_BASE__TOKEN__VBD__LITERAL__TRUE) {
          emit(Value::make_bool(true));
        } else if (vbd & WUFFS_BASE__TOKEN__VBD__LITERAL__FALSE) {
          emit(Value::make_bool(false));
        } else {
          throw ParseError("unsupported literal");
        }
        return;

      case WUFFS_BASE__TOKEN__VBC__NUMBER:
        num_.append(reinterpret_cast<const char*>(src + pos), len);
        if (!continued) {
          emit(make_number(num_));
          num_.clear();
        }
        return;

      default:
        throw ParseError("unsupported token category");
    }

    // Only string-ish tokens reach here.
    if (!continued) finish_string();
  }

  bool complete() const { return have_root_ && stack_.empty(); }
  Value take() { return std::move(root_); }

 private:
  struct Frame {
    bool is_dict = false;
    bool want_key = true;
    std::string key;
    Value::Array arr;
    Value::Object obj;
  };

  static Value make_number(const std::string& text) {
    Value::Number n;
    auto s = wuffs_base__make_slice_u8(
        reinterpret_cast<uint8_t*>(const_cast<char*>(text.data())), text.size());
    auto i = wuffs_base__parse_number_i64(s, WUFFS_BASE__PARSE_NUMBER_XXX__DEFAULT_OPTIONS);
    if (wuffs_base__status__is_ok(&i.status)) {
      n.i = i.value;
      n.d = static_cast<double>(i.value);
      n.is_int = true;
      return Value::make_number(n);
    }
    auto d = wuffs_base__parse_number_f64(s, WUFFS_BASE__PARSE_NUMBER_XXX__DEFAULT_OPTIONS);
    if (!wuffs_base__status__is_ok(&d.status)) throw ParseError("number out of range: " + text);
    n.d = d.value;
    if (std::isfinite(n.d) && n.d == std::floor(n.d) && std::fabs(n.d) < 9.2e18) {
      n.i = static_cast<int64_t>(n.d);
      n.is_int = true;  // e.g. "1.0" or "1e3" count as integers, like JSON Schema says
    }
    return Value::make_number(n);
  }

  void finish_string() {
    if (wuffs_base__utf_8__longest_valid_prefix(reinterpret_cast<const uint8_t*>(str_.data()),
                                                str_.size()) != str_.size())
      throw ParseError("string is not valid UTF-8");
    std::string s = std::move(str_);
    str_.clear();
    if (!stack_.empty() && stack_.back().is_dict && stack_.back().want_key) {
      stack_.back().key = std::move(s);
      stack_.back().want_key = false;
      return;
    }
    emit(Value::make_string(std::move(s)));
  }

  void emit(Value v) {
    if (stack_.empty()) {
      root_ = std::move(v);
      have_root_ = true;
      return;
    }
    Frame& f = stack_.back();
    if (!f.is_dict) {
      f.arr.push_back(std::move(v));
    } else {
      f.obj.add(std::move(f.key), std::move(v));
      f.key.clear();
      f.want_key = true;
    }
  }

  std::vector<Frame> stack_;
  std::string str_, num_;
  Value root_;
  bool have_root_ = false;
};

struct FreeDeleter {
  void operator()(void* p) const { std::free(p); }
};

}  // namespace

Value parse(std::string_view text) {
  std::unique_ptr<wuffs_json__decoder, FreeDeleter> dec(wuffs_json__decoder__alloc());
  if (!dec) throw std::bad_alloc();

  constexpr size_t kTokens = 512;
  wuffs_base__token token_array[kTokens];
  wuffs_base__token_buffer tokens =
      wuffs_base__slice_token__writer(wuffs_base__make_slice_token(token_array, kTokens));

  const uint8_t* data = reinterpret_cast<const uint8_t*>(text.data());
  wuffs_base__io_buffer src =
      wuffs_base__ptr_u8__reader(const_cast<uint8_t*>(data), text.size(), /*closed=*/true);

  Builder builder;
  uint64_t pos = 0;  // tokens tile the input, so summing lengths gives the offset
  for (;;) {
    wuffs_base__status st = wuffs_json__decoder__decode_tokens(dec.get(), &tokens, &src,
                                                               wuffs_base__empty_slice_u8());
    while (tokens.meta.ri < tokens.meta.wi) {
      const wuffs_base__token t = tokens.data.ptr[tokens.meta.ri++];
      const uint64_t len = wuffs_base__token__length(&t);
      builder.token(t, data, pos, len);
      pos += len;
    }
    wuffs_base__token_buffer__compact(&tokens);

    if (st.repr == nullptr) break;  // OK: one complete top-level value
    if (st.repr == wuffs_base__suspension__short_write) continue;  // token buffer was full
    throw ParseError(std::string("invalid JSON near byte ") + std::to_string(pos) + ": " +
                     wuffs_base__status__message(&st));
  }

  if (!builder.complete()) throw ParseError("incomplete JSON document");

  // The decoder stops after the first top-level value; only whitespace may follow.
  for (size_t i = static_cast<size_t>(pos); i < text.size(); ++i) {
    char c = text[i];
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
      throw ParseError("unexpected trailing characters after JSON value at byte " + std::to_string(i));
  }
  return builder.take();
}

}  // namespace s2c
