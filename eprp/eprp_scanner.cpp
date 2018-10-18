
#include <ctype.h>

#include <limits>

#include "eprp_scanner.hpp"

#ifndef likely
#define likely(x)       __builtin_expect((x),1)
#endif
#ifndef unlikely
#define unlikely(x)     __builtin_expect((x),0)
#endif

void Eprp_scanner::setup_translate() {

  translate.resize(256);

  for(int i=0;i<256;i++) {
    if (isalnum(i) || i == '_') {
      translate[i] = TOK_ALNUM | TOK_TRYMERGE;
    }else{
      translate[i] = TOK_NOP;
    }
  }

  translate['{'] = TOK_OB;
  translate['}'] = TOK_CB;
  translate[':'] = TOK_COLON | TOK_TRYMERGE; // TOK_LABEL
  translate['|'] = TOK_OR;
  translate['.'] = TOK_DOT;
  translate[';'] = TOK_SEMICOLON;
  translate[','] = TOK_COMMA;
  translate['('] = TOK_OP;
  translate[')'] = TOK_CP;
  translate['#'] = TOK_POUND;
  translate['>'] = TOK_GT  | TOK_TRYMERGE;  // TOK_PIPE
  translate['*'] = TOK_MUL;
  translate['/'] = TOK_DIV;
  translate['"'] = TOK_STRING | TOK_TRYMERGE;  // Anything else until other TOK_STRING

  translate['@'] = TOK_AT;
  translate['$'] = TOK_DOLLAR;
  translate['%'] = TOK_PERCENT;

  translate['`'] = TOK_BACKTICK;

}

void Eprp_scanner::add_token(Token t) {

  // Handle strings, even before empty as spaces are legal
  if (!token_list.empty()) {
    Token last_tok = token_list.back();
    if (last_tok.tok == (TOK_STRING|TOK_TRYMERGE)) {
      if (t.tok == (TOK_STRING|TOK_TRYMERGE)) {
        token_list.back().tok = TOK_STRING; // Remove TOK_TRYMERGE (closing ")
        //token_list.back().len += t.len;  Do not include " in string
      }else{
        token_list.back().len += t.len;
      }
      return;
    }
  }

  if (!t.tok) {
    token_list_spaced = true;
    return;
  }

  if (t.tok & TOK_TRYMERGE) {
    if (t.tok == (TOK_TRYMERGE | TOK_STRING)) {
      t.pos++;   // Skip the first " in the string
      t.len = 0;
    }else{
      t.tok &= ~TOK_TRYMERGE;
      if (!token_list.empty() && !token_list_spaced) {
        Token last_tok = token_list.back();

        if (last_tok.tok == TOK_OR && t.tok == TOK_GT) {
          token_list.back().tok = TOK_PIPE;
          token_list.back().len += t.len;
          return;
        }else if (last_tok.tok == TOK_AT && t.tok == TOK_ALNUM) {
          token_list.back().tok = TOK_REGISTER;
          token_list.back().len += t.len;
          return;
        }else if (last_tok.tok == TOK_PERCENT && t.tok == TOK_ALNUM) {
          token_list.back().tok = TOK_OUTPUT;
          token_list.back().len += t.len;
          return;
        }else if (last_tok.tok == TOK_DOLLAR && t.tok == TOK_ALNUM) {
          token_list.back().tok = TOK_INPUT;
          token_list.back().len += t.len;
          return;
        }else if (last_tok.tok == TOK_ALNUM && t.tok == TOK_COLON) {
          token_list.back().tok = TOK_LABEL;
          // token_list.back().len += t.len;
          return;
        }
      }
    }
  }

  token_list_spaced = false;
  token_list.push_back(t);
}

void Eprp_scanner::patch_pass(const std::map<std::string, uint8_t> &keywords) {
  for(auto &t:token_list) {
    if (t.tok != TOK_ALNUM)
      continue;

    if (isdigit(buffer[t.pos])) {
      t.tok = TOK_NUM;
      continue;
    }
    std::string alnum(&buffer[t.pos], t.len);
    auto it = keywords.find(alnum);
    if (it == keywords.end())
      continue;

    assert(it->second >= TOK_KEYWORD_FIRST);
    assert(it->second <= TOK_KEYWORD_LAST);

    t.tok = it->second;
  }
}

void Eprp_scanner::parse(const std::string &name, const char *memblock, size_t sz, bool chunking) {

  buffer_name = name;
  buffer = 0;
  buffer_sz = 0;
  buffer_start_pos = 0;
  buffer_start_line = 0;
  scanner_pos = 0;

  int  nlines                = 0;
  int  nchunks               = 1;
  char last_c                = 0;
  bool in_comment            = false;
  bool in_singleline_comment = false;
  int  in_multiline_comment  = 0; // Nesting support

  const char *ptr_section = memblock;

  Token t;
  t.pos = 0;
  t.tok = TOK_NOP;

  buffer = memblock; // To allow error reporting before chunking

  bool starting_comment=false; // Only for comments to avoid /*/* nested back to back */*/
  bool finishing_comment=false; // Only for comments to avoid /*/* nested back to back */*/
  for(size_t i = 0; i < sz; i++) {
    char c = memblock[i];
    int pos = (&memblock[i] - ptr_section); // same as "i" unless chunking
    if(c == '\n' || c == '\r' || c == '\f') {
      nlines++;
      if (!in_comment && t.tok) {
        t.len = pos - t.pos;
        add_token(t);
        t.tok = TOK_NOP;
        t.pos = pos;

        // At least MIN_CHUNK_SIZE characters, and not close to the end of file
        if(unlikely(chunking && (ptr_section + MIN_CHUNK_SIZE) < &memblock[i] && (last_c == '\n' || last_c == '\r' || last_c == '\f') &&
            ((i + MIN_CHUNK_SIZE) < sz) && !token_list.empty())) {
          chunked(ptr_section, (&memblock[i] -ptr_section), ptr_section - memblock, nlines);
          ptr_section = &memblock[i + 1];
          nchunks++;
        }
      }else{
        starting_comment  = false;
        finishing_comment = false;
        in_singleline_comment = false;
        in_comment            = in_singleline_comment | in_multiline_comment;
      }
    }else if(unlikely(last_c == '/' && c == '/')) {
      t.len = pos - t.pos;
      t.tok = TOK_COMMENT;
      in_singleline_comment = true;
      in_comment = true;
      assert(!starting_comment);
      assert(!finishing_comment);
    }else if(unlikely(!finishing_comment && ((last_c == '/' && c == '*') || (last_c == '(' && c == '*')))) {
      t.len = pos - t.pos;
      t.tok = TOK_COMMENT;
      in_multiline_comment++;
      in_comment = true;
      starting_comment  = true;
      assert(!finishing_comment);
    }else if (unlikely(!starting_comment && ((last_c == '*' && c == '/') || (last_c == '*' && c == ')')))) {
      t.len = pos - t.pos;
      t.tok = TOK_COMMENT;
      in_multiline_comment--;
      if (in_multiline_comment < 0) {
        lex_error(fmt::format("{}:{} found end of comment without matching beginning of comment", name, nlines));
      }else if (in_multiline_comment==0) {
        in_singleline_comment = false;
        in_comment = false;
      }
      assert(!starting_comment);
      finishing_comment = true;
    }else if(in_comment) {
      starting_comment  = false;
      finishing_comment = false;
    }else{
      Token_id nt = translate[static_cast<uint8_t>(c)];
      if (t.tok != nt) {
        finishing_comment = false;
        t.len = pos - t.pos;
        add_token(t);
        t.tok = nt;
        t.pos = pos;
      }
    }

    last_c = c;
  }
  if (t.tok) {
    t.len = sz - t.pos;
    add_token(t);
  }

  if (&memblock[sz-1] != ptr_section)
    chunked(ptr_section, (&memblock[sz]-ptr_section), ptr_section - memblock, nlines);
}

Eprp_scanner::Eprp_scanner() {
  setup_translate();
  max_errors   = 1;
  max_warnings = 1024;
  n_errors = 0;
  n_warnings = 0;
}

void Eprp_scanner::chunked(const char *_buffer, size_t _buffer_sz, size_t _buffer_start_pos, size_t _buffer_start_line) {

  assert(_buffer_sz < std::numeric_limits<uint32_t>::max());

  // TODO?: we could multithread each chunk. buffer_*, scanner_pos, and token_list should be privatized per thread

  buffer = _buffer;
  buffer_sz = static_cast<uint32_t>(_buffer_sz);
  buffer_start_pos = _buffer_start_pos;
  buffer_start_line = _buffer_start_line;

  assert(token_list.size());

  elaborate();

  token_list.clear();
  scanner_pos = 0;
}

bool Eprp_scanner::scan_next() {
  if (scanner_pos >= token_list.size())
    return false;

  scanner_pos++;

  return true;
}

void Eprp_scanner::scan_append(std::string &text) const {

  assert(scanner_pos < token_list.size());

  text.append(&buffer[token_list[scanner_pos].pos], token_list[scanner_pos].len);
}

void Eprp_scanner::scan_format_append(std::string &text) const {
  assert(scanner_pos < token_list.size());

  int start_pos = token_list[scanner_pos].pos;
  int len = token_list[scanner_pos].len;
  if (scanner_pos>0) {
    int last_end_pos = token_list[scanner_pos-1].pos+token_list[scanner_pos-1].len;
    len += (start_pos - last_end_pos);
    start_pos = last_end_pos;
  }
  text.append(&buffer[start_pos], len);
}

void Eprp_scanner::scan_prev_append(std::string &text) const {

  assert(scanner_pos < token_list.size());
  int p = scanner_pos-1;
  if (p<0)
    p = 0;

  text.append(&buffer[token_list[p].pos], token_list[p].len);
}

void Eprp_scanner::scan_next_append(std::string &text) const {

  assert(scanner_pos < token_list.size());
  size_t p = scanner_pos+1;
  if (p>=token_list.size())
    p = token_list.size()-1;;

  text.append(&buffer[token_list[p].pos], token_list[p].len);
}

std::string Eprp_scanner::scan_text() const {
  std::string text;

  scan_append(text);

  return text;
}

int Eprp_scanner::scan_calc_lineno() const {

  size_t max_pos = scanner_pos;
  if (max_pos>=token_list.size())
    max_pos = token_list.size()-1;

  int nlines = 0;
  for(size_t i=0;i<token_list[max_pos].pos;i++) {
    char c = buffer[i];
    nlines += (c == '\n' || c == '\r' || c == '\f')?1:0;
  }

  return buffer_start_line + nlines;
}

void Eprp_scanner::lex_error(const std::string &text) {
  // lexer can not look at token list

  fmt::print(text + "\n");
  n_errors++;
  if (n_errors>max_errors)
    exit(-3);
}
void Eprp_scanner::scan_error(const std::string &text) {
  scan_raw_msg("error", text, true);
  n_errors++;
  if (n_errors>max_errors)
    exit(-3);
}

void Eprp_scanner::scan_warn(const std::string &text) {
  scan_raw_msg("warning", text, true);
  n_warnings++;
  if (n_warnings>max_warnings)
    exit(-3);
}

void Eprp_scanner::parser_error(const std::string &text) {
  scan_raw_msg("error", text, false);
  n_errors++;
  if (n_errors>max_errors)
    exit(-3);
}

void Eprp_scanner::parser_warn(const std::string &text) {
  scan_raw_msg("warning", text, false);
  n_warnings++;
  if (n_warnings>max_warnings)
    exit(-3);
}

void Eprp_scanner::scan_raw_msg(const std::string &cat, const std::string &text, bool third) const {

  // Look at buffer for previous line change

  size_t max_pos = scanner_pos;
  if (max_pos>=token_list.size())
    max_pos = token_list.size()-1;

  int line_pos_start = 0;
  for(int i=token_list[max_pos].pos;i>0;i--) {
    char c = buffer[i];
    if (c == '\n' || c == '\r' || c == '\f') {
      line_pos_start = i;
      break;
    }
  }
  int line_pos_end = buffer_sz;
  for(size_t i=token_list[max_pos].pos;i<buffer_sz;i++) {
    char c = buffer[i];
    if (c == '\n' || c == '\r' || c == '\f') {
      line_pos_end = i;
      break;
    }
  }

  std::string line_txt(&buffer[line_pos_start],line_pos_end-line_pos_start);

  int line = scan_calc_lineno();
  int col  = token_list[max_pos].pos - line_pos_start;

  std::string first_1 = fmt::format("{}:{}:{} {}: {}", buffer_name, line, col, cat, text);
  std::string second_1 = line_txt;

  fmt::print(first_1 + "\n");
  fmt::print(second_1 + "\n");

  if (!third)
    return;

  std::string third_1(col, ' ');
  std::string third_2(token_list[max_pos].len, '^');
  fmt::print(third_1 + third_2 + "\n");
}

void Eprp_scanner::dump_token() const {
  size_t pos = scanner_pos;
  if (pos>=token_list.size())
    pos = token_list.size();

  auto &t = token_list[pos];
  std::string raw(&buffer[t.pos], t.len);
  fmt::print("tok:{} pos:{} len:{} raw:{}\n",t.tok, t.pos, t.len, raw);
}

