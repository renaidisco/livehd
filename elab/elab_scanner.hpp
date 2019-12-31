//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/substitute.h"

#include "explicit_type.hpp"
#include "fmt/format.h"
#include "iassert.hpp"

using Token_id = uint8_t;

using Token_entry = Explicit_type<uint32_t, struct Token_entry_struct, 0>;

// WARNING:
//
// We can not use enum or enum class cleanly because they do not support
// inheritance in C++. It is just cleaner (less static_cast conversions) using
// constexpr
//
// Suggested way to extend tokens for other languages: (replace pyrope for your language)
//
// constexpr Token_id Pyrope_id_if   = 128; // Bigger than Token_id_keyword_first
// constexpr Token_id Pyrope_id_else = 129;
// constexpr Token_id Pyrope_id_for  = 130;
//
constexpr Token_id Token_id_nop           = 0;   // invalid token
constexpr Token_id Token_id_comment       = 1;   // c-like comments
constexpr Token_id Token_id_register      = 2;   // #asd #_asd
constexpr Token_id Token_id_pipe          = 3;   // |>
constexpr Token_id Token_id_alnum         = 4;   // a..z..0..9.._
constexpr Token_id Token_id_ob            = 5;   // {
constexpr Token_id Token_id_cb            = 6;   // }
constexpr Token_id Token_id_colon         = 7;   // :
constexpr Token_id Token_id_gt            = 8;   // >
constexpr Token_id Token_id_or            = 9;   // |
constexpr Token_id Token_id_at            = 10;  // @
constexpr Token_id Token_id_label         = 11;  // foo:
constexpr Token_id Token_id_output        = 12;  // %outasd
constexpr Token_id Token_id_input         = 13;  // $asd
constexpr Token_id Token_id_dollar        = 14;  // $
constexpr Token_id Token_id_percent       = 15;  // %
constexpr Token_id Token_id_dot           = 16;  // .
constexpr Token_id Token_id_div           = 17;  // /
constexpr Token_id Token_id_string        = 18;  // "asd23*.v" string (double quote)
constexpr Token_id Token_id_semicolon     = 19;  // ;
constexpr Token_id Token_id_comma         = 20;  // ,
constexpr Token_id Token_id_op            = 21;  // (
constexpr Token_id Token_id_cp            = 22;  // )
constexpr Token_id Token_id_pound         = 23;  // #
constexpr Token_id Token_id_mult          = 24;  // *
constexpr Token_id Token_id_num           = 25;  // 0123123 or 123123 or 0123ubits
constexpr Token_id Token_id_backtick      = 26;  // `ifdef
constexpr Token_id Token_id_synopsys      = 27;  // synopsys... directive in comment
constexpr Token_id Token_id_plus          = 28;  // +
constexpr Token_id Token_id_minus         = 29;  // -
constexpr Token_id Token_id_bang          = 30;  // !
constexpr Token_id Token_id_lt            = 31;  // <
constexpr Token_id Token_id_eq            = 32;  // =
constexpr Token_id Token_id_same          = 33;  // ==
constexpr Token_id Token_id_diff          = 34;  // !=
constexpr Token_id Token_id_coloneq       = 35;  // :=
constexpr Token_id Token_id_ge            = 37;  // >=
constexpr Token_id Token_id_le            = 38;  // <=
constexpr Token_id Token_id_and           = 39;  // &
constexpr Token_id Token_id_xor           = 40;  // ^
constexpr Token_id Token_id_qmark         = 41;  // ?
constexpr Token_id Token_id_tick          = 42;  // '
constexpr Token_id Token_id_obr           = 43;  // [
constexpr Token_id Token_id_cbr           = 44;  // ]
constexpr Token_id Token_id_backslash     = 45;  // \ back slash
constexpr Token_id Token_id_reference     = 46;  // \foo
constexpr Token_id Token_id_keyword_first = 64;
constexpr Token_id Token_id_keyword_last  = 254;

class Token {
public:
  Token() {
    tok  = Token_id_nop;
    pos  = 0;
    line = 0;
    text = std::string_view{""};
  }
  Token(Token_id _tok, uint64_t _pos, uint32_t _line, std::string_view _text) {
    tok  = _tok;
    pos  = _pos;
    line = _line;
    text = _text;
  }
  void reset(Token_id _tok, uint64_t _pos, uint32_t _line, std::string_view _text) {
    tok  = _tok;
    pos  = _pos;
    line = _line;
    text = _text;
  }
  void clear(uint64_t _pos, uint32_t _line, std::string_view _text) {
    tok  = Token_id_nop;
    pos  = _pos;
    line = _line;
    text = _text;
  }

  void fuse_token(Token_id new_tok, const Token &t2) {
    I(text.data()+text.size() == t2.text.data()); // t2 must be continuous (otherwise, create new token)
    tok = new_tok;

    auto new_len = text.size() + t2.text.size();
    text = std::string_view{text.data(), new_len};
  }
  void append_token(const Token &t2) {
    I(text.data()+text.size() == t2.text.data()); // t2 must be continuous (otherwise, create new token)

    auto new_len = text.size() + t2.text.size();
    text = std::string_view{text.data(), new_len};
  }

  void adjust_token_size(uint64_t end_pos) {
    GI(tok != Token_id_nop, end_pos >= pos);
    auto new_len = end_pos - pos;
    text = std::string_view{text.data(), new_len};
  }

  Token_id tok;  // Token (identifier, if, while...)
  uint64_t pos;  // Position in original memblock for debugging
  uint32_t line; // line of code
  std::string_view text;

  std::string_view get_text() const { return text; }
};

class Elab_scanner {
public:
  typedef std::vector<Token> Token_list;
protected:

  Token_list       token_list;

private:
  std::string      buffer_name;

  std::string_view memblock;
  int              memblock_fd;
  void unregister_memblock();

  struct Translate_item {
    Translate_item() : tok(Token_id_nop), try_merge(false) {}
    Translate_item(Token_id t, bool tm = false) : tok(t), try_merge(tm) {}
    Translate_item &operator=(const Translate_item &other) {
      *const_cast<Token_id *>(&tok)   = other.tok;
      *const_cast<bool *>(&try_merge) = other.try_merge;
      return *this;
    }
    Translate_item &operator=(const Token_id &other_tok) {
      *const_cast<Token_id *>(&tok)   = other_tok;
      *const_cast<bool *>(&try_merge) = false;
      return *this;
    }
    const Token_id tok;
    const bool     try_merge;
  };
  std::vector<Translate_item> translate;
  bool                        token_list_spaced = true;
  bool                        trying_merge      = false;

  // Fields updated for each chunk processed
  Token_entry scanner_pos;

  int         max_errors;
  int         max_warnings;
  mutable int n_errors;  // NOTE: mutable to allow const methods for error/warning reporting
  mutable int n_warnings;

  void setup_translate();

  void add_token(Token &t);

  void scan_raw_msg(std::string_view cat, std::string_view text, bool third) const;

  void lex_error(std::string_view text);

  void parse_setup(std::string_view filename);
  void parse_setup();
  void parse_step();

protected:
  std::string_view get_memblock() const { return memblock; }
  std::string_view get_filename() const { I(memblock_fd != -1); return buffer_name; }

public:
  Elab_scanner();
  virtual ~Elab_scanner();

  void scan_error(std::string_view text) const;  // Not really const, but to allow to be used by const methods
  void scan_warn(std::string_view text) const;

  void parser_error(std::string_view text) const;
  void parser_warn(std::string_view text) const;
  void parser_info(std::string_view text) const;

  template <typename... Args>
  void scan_error(const char *format, const Args &... args) const {
    fmt::format_args fargs = fmt::make_format_args(args...);
    fmt::memory_buffer tmp;
    fmt::vformat_to(tmp, format, fargs);
    scan_error(std::string_view(tmp.data(), tmp.size()));
  }
  template <typename... Args>
  void scan_warn(std::string_view format, const Args &... args) const {
    fmt::format_args fargs = fmt::make_format_args(args...);
    fmt::memory_buffer tmp;
    fmt::vformat_to(tmp, format, fargs);
    scan_warn(std::string_view(tmp.data(), tmp.size()));
  }
  template <typename... Args>
  void parser_error(std::string_view format, const Args &... args) const {
    fmt::format_args fargs = fmt::make_format_args(args...);
    fmt::memory_buffer tmp;
    fmt::vformat_to(tmp, format, fargs);
    parser_error(std::string_view(tmp.data(), tmp.size()));
  }
  template <typename... Args>
  void parser_warn(std::string_view format, const Args &... args) const {
    fmt::format_args fargs = fmt::make_format_args(args...);
    fmt::memory_buffer tmp;
    fmt::vformat_to(tmp, format, fargs);
    parser_warn(std::string_view(tmp.data(), tmp.size()));
  }
  template <typename... Args>
  void parser_info(std::string_view format, const Args &... args) const {
    fmt::format_args fargs = fmt::make_format_args(args...);
    fmt::memory_buffer tmp;
    fmt::vformat_to(tmp, format, fargs);
    parser_info(std::string_view(tmp.data(), tmp.size()));
  }

  bool scan_next();
  bool scan_prev();

  void set_max_errors(int n) { max_errors = n; }
  void set_max_warning(int n) { max_warnings = n; }

  bool scan_is_end() const { return scanner_pos >= token_list.size(); }

  bool scan_is_token(Token_id tok) const {
    if (scanner_pos < token_list.size()) return token_list[scanner_pos].tok == tok;
    return false;
  }

  Token_entry scan_token() const {
    I(scanner_pos != 0);
    I(scanner_pos < token_list.size());
    return scanner_pos;
  }

  std::string_view scan_prev_text() const {
    size_t p = scanner_pos - 1;
    if (scanner_pos <= 0) p = 0;
    return token_list[p].get_text();
  }

  std::string_view scan_next_text() const {
    size_t p = scanner_pos + 1;
    if (p >= token_list.size())
      p = token_list.size()-1;
    return token_list[p].get_text();
  }

  bool scan_next_is_token(Token_id tok) const {
    size_t p = scanner_pos + 1;
    if (p >= token_list.size())
      return false;
    return token_list[p].tok == tok;
  }

  std::string_view scan_peep_text(int offset) const {
    I(offset != 0);
    size_t p = scanner_pos + offset;
    if (p >= token_list.size())
      p = token_list.size()-1;
    else if (offset > static_cast<int>(scanner_pos))
      p = 0 ;
    return token_list[p].get_text();
  }

  void scan_format_append(std::string &text) const;

  inline std::string_view scan_text(const Token_entry te) const {
    I(token_list.size() > te);
    return token_list[te].get_text();
  }

  std::string_view scan_text() const {
    return token_list[scanner_pos].get_text();
  }
  uint32_t    scan_line() const;

  size_t get_token_pos() const { return token_list[scan_token()].pos; }

  bool scan_is_prev_token(Token_id tok) const {
    if (scanner_pos == 0) return false;
    I(scanner_pos < token_list.size());
    return token_list[scanner_pos - 1].tok == tok;
  }
  bool scan_is_next_token(int pos, Token_id tok) const {
    if ((scanner_pos + pos) >= token_list.size()) return false;
    return token_list[scanner_pos + pos].tok == tok;
  }

  bool scan_peep_is_token(Token_id tok, int offset) const {
    I(offset != 0);
    size_t p = scanner_pos + offset;
    if (p >= token_list.size())
      p = token_list.size() - 1;
    else if (offset > static_cast<int>(scanner_pos))
      p = 0 ;
    return token_list[p].tok == tok;;
  }

  void patch_pass(const absl::flat_hash_map<std::string, Token_id> &keywords);

  void patch_pass() {
      absl::flat_hash_map<std::string, Token_id> no_keywords;
      patch_pass(no_keywords);
  }

  void parse_file(std::string_view filename) {
    parse_setup(filename);
    parse_step();
  }

  void parse_inline(std::string_view txt) {
    parse_setup();
    memblock = txt;
    parse_step();
  }

  void dump_token() const;

  virtual void elaborate() = 0;

  bool has_errors() const { return n_errors > 0; }

  Token scan_get_token(int offset = 0) const {
    size_t p = scanner_pos + offset;
    if (p >= token_list.size())
      p = token_list.size() - 1;

    //comment out by Sheng
    //else if (offset > static_cast<int>(scanner_pos))
    //  p = 0 ;
    return token_list[p];
  }
};
