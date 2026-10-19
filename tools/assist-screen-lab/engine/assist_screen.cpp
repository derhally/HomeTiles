#include "assist_screen.h"

#include <ctype.h>
#include <math.h>
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Provided by the host (the panel: ui_font_for_size; the lab: its own list).
// Returns nullptr for a size the panel does not carry.
const lv_font_t* assist_screen_font(int px);

namespace assist_screen {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;
constexpr float kCoordLimit = 30000.0f;
constexpr int kMaxLiteralPoints = 16;
constexpr int kMaxPolylines = 8;
constexpr int kMaxLogs = 4;
// Each top-level element is its own redraw region (the rest share the last
// one), so a changing readout at one side does not redraw the other side.
constexpr int kMaxRegions = 24;

// ---------------------------------------------------------------------------
// JSON: a bounded DOM over the file text. Strings stay in the source and are
// unescaped when copied out.
// ---------------------------------------------------------------------------
enum class J : uint8_t { Null, Bool, Num, Str, Arr, Obj };

struct JNode {
  J type;
  uint8_t boolean;
  uint16_t key_off, key_len;  // the key in the parent object
  uint16_t str_off, str_len;  // a string's raw text
  uint16_t line;
  int16_t first, next;        // children, siblings
  float num;
};

// ---------------------------------------------------------------------------
// Expressions: compiled to a small stack machine.
// ---------------------------------------------------------------------------
enum Op : uint8_t {
  kConst, kLoad, kAdd, kSub, kMul, kDiv, kMod, kNeg, kNot, kLt, kLe, kGt, kGe, kEq, kNe, kAnd, kOr,
  kJz, kJmp, kCall,
};

struct Code {
  uint8_t op;
  uint8_t argc;
  uint16_t arg;  // slot, function id or jump target
  float k;
};

struct Expr {
  uint16_t start = 0, len = 0;  // len 0: absent
};

enum Fn : uint8_t {
  fSin, fCos, fTan, fAsin, fAcos, fAtan, fAtan2, fSqrt, fAbs, fMin, fMax, fClamp, fClamp01, fMix,
  fFloor, fCeil, fRound, fFract, fFmod, fExp, fPow, fLog, fHash, fRand, fFrand, fStep, fSmooth, fSign,
  fFnCount,
};
struct FnDef {
  const char* name;
  uint8_t argc;
};
constexpr FnDef kFns[fFnCount] = {
    {"sin", 1},   {"cos", 1},     {"tan", 1},   {"asin", 1},  {"acos", 1},       {"atan", 1},
    {"atan2", 2}, {"sqrt", 1},    {"abs", 1},   {"min", 2},   {"max", 2},        {"clamp", 3},
    {"clamp01", 1}, {"mix", 3},   {"floor", 1}, {"ceil", 1},  {"round", 1},      {"fract", 1},
    {"mod", 2},   {"exp", 1},     {"pow", 2},   {"log", 1},   {"hash", 2},       {"rand", 2},
    {"frand", 0}, {"step", 2},    {"smoothstep", 3}, {"sign", 1},
};

// ---------------------------------------------------------------------------
// Elements and the draw list.
// ---------------------------------------------------------------------------
enum Kind : uint8_t { kLine, kArc, kDisc, kRect, kFrame, kPoly, kText, kLog, kGroup, kKindCount };
constexpr const char* kKindNames[kKindCount] = {"line", "arc", "disc", "rect", "frame",
                                                "poly", "text", "log", "group"};

// Properties by kind; their index is the element's p[] slot.
struct PropDef {
  const char* name;
  float def;
  bool required;
};
constexpr int kMaxProps = 10;
constexpr PropDef kProps[kKindCount][kMaxProps] = {
    // line
    {{"x1", 0, true}, {"y1", 0, true}, {"x2", 0, true}, {"y2", 0, true}, {"width", 1, false},
     {"color", 0xFFFFFF, false}, {"alpha", 1, false}, {"dash", 0, false}, {"gap", 0, false}},
    // arc
    {{"x", 0, true}, {"y", 0, true}, {"radius", 0, true}, {"start", 0, false}, {"end", 360, false},
     {"width", 1, false}, {"color", 0xFFFFFF, false}, {"alpha", 1, false}},
    // disc
    {{"x", 0, true}, {"y", 0, true}, {"radius", 0, true}, {"color", 0xFFFFFF, false}, {"alpha", 1, false},
     {"grad", 0, false}},
    // rect
    {{"x1", 0, true}, {"y1", 0, true}, {"x2", 0, true}, {"y2", 0, true}, {"color", 0xFFFFFF, false},
     {"alpha", 1, false}, {"radius", 0, false}},
    // frame
    {{"x1", 0, true}, {"y1", 0, true}, {"x2", 0, true}, {"y2", 0, true}, {"width", 1, false},
     {"color", 0xFFFFFF, false}, {"alpha", 1, false}},
    // poly (x and y per generated point)
    {{"x", 0, false}, {"y", 0, false}, {"width", 1, false}, {"color", 0xFFFFFF, false}, {"alpha", 1, false}},
    // text
    {{"x", 0, true}, {"y", 0, true}, {"color", 0xFFFFFF, false}, {"alpha", 1, false}, {"width", 200, false},
     {"every", 1, false}},
    // log
    {{"x", 0, true}, {"y", 0, true}, {"color", 0xFFFFFF, false}, {"alpha", 1, false}, {"width", 200, false},
     {"every", 1, false}, {"line_height", 20, false}, {"push", 1, false}, {"pop", 0, false}},
    // group
    {},
};
enum { kL_X1, kL_Y1, kL_X2, kL_Y2, kL_W, kL_C, kL_A, kL_DASH, kL_GAP };
enum { kA_X, kA_Y, kA_R, kA_S, kA_E, kA_W, kA_C, kA_A };
enum { kD_X, kD_Y, kD_R, kD_C, kD_A, kD_G };
enum { kR_X1, kR_Y1, kR_X2, kR_Y2, kR_C, kR_A, kR_RAD };
enum { kF_X1, kF_Y1, kF_X2, kF_Y2, kF_W, kF_C, kF_A };
enum { kP_X, kP_Y, kP_W, kP_C, kP_A };
enum { kT_X, kT_Y, kT_C, kT_A, kT_W, kT_EVERY };
enum { kG_X, kG_Y, kG_C, kG_A, kG_W, kG_EVERY, kG_LH, kG_PUSH, kG_POP };

enum ElemFlag : uint8_t { kGlow = 1, kRound = 2, kFill = 4, kLiteral = 8, kRoundSet = 16 };

struct Elem {
  uint8_t kind;
  uint8_t flags;
  uint8_t font;
  uint8_t align;
  uint8_t on;  // bit n = drawn on Shape n
  uint16_t repeat;
  int16_t index_slot;
  uint16_t let_first, let_count;
  uint16_t end;  // first element after this one's children
  Expr cond;
  Expr p[kMaxProps];
  // poly
  uint8_t poly;    // index into polys
  // text and log
  uint16_t seg_first, seg_count;
  uint8_t text_first;  // first text buffer
  uint8_t rows;
  uint8_t log;         // index into logs
};

struct Let {
  int16_t slot;
  Expr expr;
};

struct Var {
  int16_t slot;
  uint8_t kind;  // 0 derived, 1 state
  Expr expr;     // derived value or state init
  Expr next;     // state step (may be absent)
  Expr every;    // seconds between steps (may be absent)
  Expr alt[kShapeCount];  // a layout's own derived value (may be absent)
  uint16_t frames;  // frames between steps (0: every frame)
  float acc;
};

struct Segment {
  uint16_t lit_off, lit_len;  // literal text in the string pool
  Expr expr;                  // or a value
  char fmt[8];                // printf conversion without '%', e.g. "03d"
};

struct Poly {
  uint16_t pts;         // first point in the pool
  uint16_t count;       // points per copy
  uint8_t keep;         // older copies kept
  uint8_t frames;       // frames between copies
  uint8_t width;        // older copies' width
  uint8_t stored;       // older copies filled
  uint8_t head;         // newest older copy
  int16_t i_slot, u_slot;
  float alpha[kMaxTrail];
  Expr lit[kMaxLiteralPoints][2];
};

struct Log {
  uint8_t count;  // rows shown
  int16_t count_slot;
};

enum CmdKind : uint8_t { cLine, cArc, cDisc, cRect, cPoly, cText };
enum CmdFlag : uint8_t { cfRound = 1, cfGrad = 2 };

struct Cmd {
  uint8_t kind;
  uint8_t flags;
  uint8_t width;
  uint8_t opa;
  uint32_t color;
  float a, b, c, d, e;
  uint16_t x1, x2;  // poly: first point, count; text: buffer, font; line: dash, gap; rect: radius
};

struct Region {
  lv_area_t box, last_box;
  bool valid, last_valid;
  uint32_t hash, last_hash;
  uint32_t first_cmd, end_cmd;
};

}  // namespace

struct Screen {
  Info info;
  // Load-time only.
  const char* src;
  size_t src_len;
  JNode json[kMaxJsonNodes];
  int json_count;
  char* error;
  size_t error_size;
  bool failed;
  // Compiled.
  Code code[kMaxOps];
  int code_count;
  char names[kMaxSlots][24];
  int slot_count;
  float slots[kMaxSlots];
  uint8_t slot_kind[kMaxSlots];  // 0 free name, 1 builtin, 2 var
  struct {
    char name[24];
    uint32_t color;
  } colors[kMaxColors];
  int color_count;
  Var vars[kMaxVars];
  int var_count;
  Let lets[kMaxLets];
  int let_count;
  Elem elems[kMaxElements];
  int elem_count;
  Segment segs[kMaxSegments];
  int seg_count;
  char pool[kStringPool];
  int pool_used;
  Poly polys[kMaxPolylines];
  int poly_count;
  int16_t points[kPointPool][2];
  int point_count;
  Log logs[kMaxLogs];
  int log_count;
  char texts[kMaxTexts][kTextBytes];
  int text_count;
  bool text_ready[kMaxTexts];
  // Per frame.
  Cmd cmds[kMaxFrameCmds];
  uint32_t cmd_count;
  uint32_t frame_no;
  uint32_t rng;
  Region regions[kMaxRegions];
  int region_count;  // used this frame
  int region;        // being built
  int s_t, s_dt, s_frame, s_level, s_mode, s_answering, s_r, s_cx, s_cy, s_w, s_h, s_wide, s_square, s_tall;
  uint8_t shape;
};

namespace {

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------
bool fail(Screen* s, int line, const char* fmt, ...) {
  if (s->failed) return false;
  s->failed = true;
  if (!s->error || !s->error_size) return false;
  int used = 0;
  if (line > 0) used = snprintf(s->error, s->error_size, "line %d: ", line);
  if (used < 0 || static_cast<size_t>(used) >= s->error_size) return false;
  va_list args;
  va_start(args, fmt);
  vsnprintf(s->error + used, s->error_size - used, fmt, args);
  va_end(args);
  return false;
}

// ---------------------------------------------------------------------------
// JSON parser
// ---------------------------------------------------------------------------
struct JParser {
  Screen* s;
  size_t pos;
  int line;
};

void skipSpace(JParser& p) {
  while (p.pos < p.s->src_len) {
    const char c = p.s->src[p.pos];
    if (c == '\n') ++p.line;
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      ++p.pos;
    } else {
      break;
    }
  }
}

int newNode(JParser& p, J type) {
  if (p.s->json_count >= static_cast<int>(sizeof(p.s->json) / sizeof(p.s->json[0]))) {
    fail(p.s, p.line, "the file has too many values (limit %d)",
         static_cast<int>(sizeof(p.s->json) / sizeof(p.s->json[0])));
    return -1;
  }
  JNode& n = p.s->json[p.s->json_count];
  memset(&n, 0, sizeof(n));
  n.type = type;
  n.first = n.next = -1;
  n.line = static_cast<uint16_t>(p.line);
  return p.s->json_count++;
}

bool parseString(JParser& p, uint16_t* off, uint16_t* len) {
  ++p.pos;  // opening quote
  const size_t start = p.pos;
  while (p.pos < p.s->src_len && p.s->src[p.pos] != '"') {
    if (p.s->src[p.pos] == '\n') return fail(p.s, p.line, "a string runs past the end of its line");
    if (p.s->src[p.pos] == '\\') ++p.pos;
    ++p.pos;
  }
  if (p.pos >= p.s->src_len) return fail(p.s, p.line, "a string is not closed");
  *off = static_cast<uint16_t>(start);
  *len = static_cast<uint16_t>(p.pos - start);
  ++p.pos;
  return true;
}

int parseValue(JParser& p, int depth);

int parseContainer(JParser& p, int depth, bool object) {
  const int node = newNode(p, object ? J::Obj : J::Arr);
  if (node < 0) return -1;
  ++p.pos;
  int last = -1;
  skipSpace(p);
  if (p.pos < p.s->src_len && p.s->src[p.pos] == (object ? '}' : ']')) {
    ++p.pos;
    return node;
  }
  while (true) {
    skipSpace(p);
    uint16_t key_off = 0, key_len = 0;
    if (object) {
      if (p.pos >= p.s->src_len || p.s->src[p.pos] != '"') {
        fail(p.s, p.line, "expected a quoted key");
        return -1;
      }
      if (!parseString(p, &key_off, &key_len)) return -1;
      skipSpace(p);
      if (p.pos >= p.s->src_len || p.s->src[p.pos] != ':') {
        fail(p.s, p.line, "expected ':' after a key");
        return -1;
      }
      ++p.pos;
    }
    const int child = parseValue(p, depth + 1);
    if (child < 0) return -1;
    p.s->json[child].key_off = key_off;
    p.s->json[child].key_len = key_len;
    if (last < 0) {
      p.s->json[node].first = static_cast<int16_t>(child);
    } else {
      p.s->json[last].next = static_cast<int16_t>(child);
    }
    last = child;
    skipSpace(p);
    if (p.pos >= p.s->src_len) {
      fail(p.s, p.line, "the file ends inside %s", object ? "an object" : "an array");
      return -1;
    }
    const char c = p.s->src[p.pos++];
    if (c == ',') continue;
    if (c == (object ? '}' : ']')) return node;
    fail(p.s, p.line, "expected ',' or '%c'", object ? '}' : ']');
    return -1;
  }
}

int parseValue(JParser& p, int depth) {
  if (depth > kMaxJsonDepth) {
    fail(p.s, p.line, "nested too deeply (limit %d)", kMaxJsonDepth);
    return -1;
  }
  skipSpace(p);
  if (p.pos >= p.s->src_len) {
    fail(p.s, p.line, "a value is missing");
    return -1;
  }
  const char c = p.s->src[p.pos];
  if (c == '{' || c == '[') return parseContainer(p, depth, c == '{');
  if (c == '"') {
    const int node = newNode(p, J::Str);
    if (node < 0) return -1;
    if (!parseString(p, &p.s->json[node].str_off, &p.s->json[node].str_len)) return -1;
    return node;
  }
  if (c == 't' || c == 'f' || c == 'n') {
    const char* word = c == 't' ? "true" : (c == 'f' ? "false" : "null");
    const size_t len = strlen(word);
    if (p.pos + len > p.s->src_len || strncmp(p.s->src + p.pos, word, len) != 0) {
      fail(p.s, p.line, "unexpected text");
      return -1;
    }
    p.pos += len;
    const int node = newNode(p, c == 'n' ? J::Null : J::Bool);
    if (node >= 0) p.s->json[node].boolean = c == 't';
    return node;
  }
  if (c == '-' || (c >= '0' && c <= '9')) {
    char buf[32];
    size_t n = 0;
    while (p.pos < p.s->src_len && n + 1 < sizeof(buf) && strchr("+-.0123456789eE", p.s->src[p.pos])) {
      buf[n++] = p.s->src[p.pos++];
    }
    buf[n] = '\0';
    char* end = nullptr;
    const double value = strtod(buf, &end);
    if (!end || *end) {
      fail(p.s, p.line, "bad number '%s'", buf);
      return -1;
    }
    const int node = newNode(p, J::Num);
    if (node >= 0) p.s->json[node].num = static_cast<float>(value);
    return node;
  }
  fail(p.s, p.line, "unexpected character '%c'", c);
  return -1;
}

bool keyIs(const Screen* s, const JNode& n, const char* key) {
  const size_t len = strlen(key);
  return n.key_len == len && strncmp(s->src + n.key_off, key, len) == 0;
}

int child(const Screen* s, int node, const char* key) {
  for (int c = s->json[node].first; c >= 0; c = s->json[c].next) {
    if (keyIs(s, s->json[c], key)) return c;
  }
  return -1;
}

// Copies a string value (unescaped) into out.
bool copyString(Screen* s, const JNode& n, char* out, size_t size) {
  size_t used = 0;
  for (uint16_t i = 0; i < n.str_len; ++i) {
    char c = s->src[n.str_off + i];
    if (c == '\\' && i + 1 < n.str_len) {
      const char e = s->src[n.str_off + ++i];
      c = e == 'n' ? '\n' : (e == 't' ? '\t' : e);
    }
    if (used + 1 >= size) return fail(s, n.line, "text too long (limit %d bytes)", static_cast<int>(size) - 1);
    out[used++] = c;
  }
  out[used] = '\0';
  return true;
}

void keyText(const Screen* s, const JNode& n, char* out, size_t size) {
  const size_t len = n.key_len < size - 1 ? n.key_len : size - 1;
  memcpy(out, s->src + n.key_off, len);
  out[len] = '\0';
}

// ---------------------------------------------------------------------------
// Names and slots
// ---------------------------------------------------------------------------
int findSlot(const Screen* s, const char* name, size_t len) {
  for (int i = 0; i < s->slot_count; ++i) {
    if (strlen(s->names[i]) == len && strncmp(s->names[i], name, len) == 0) return i;
  }
  return -1;
}

bool validName(const char* name) {
  if (!name[0] || !(isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) return false;
  for (const char* c = name; *c; ++c) {
    if (!(isalnum(static_cast<unsigned char>(*c)) || *c == '_')) return false;
  }
  return true;
}

int addSlot(Screen* s, const char* name, int line, uint8_t kind) {
  if (!validName(name) || strlen(name) >= sizeof(s->names[0])) {
    fail(s, line, "'%s' is not a valid name (letters, digits, _; up to 23)", name);
    return -1;
  }
  for (int f = 0; f < fFnCount; ++f) {
    if (strcmp(kFns[f].name, name) == 0) {
      fail(s, line, "'%s' is a function name", name);
      return -1;
    }
  }
  for (int c = 0; c < s->color_count; ++c) {
    if (strcmp(s->colors[c].name, name) == 0) {
      fail(s, line, "'%s' is already a color name", name);
      return -1;
    }
  }
  const int found = findSlot(s, name, strlen(name));
  if (found >= 0) {
    if (s->slot_kind[found] == 1 || kind == 1) {
      fail(s, line, "'%s' is a built-in name", name);
      return -1;
    }
    if (s->slot_kind[found] == 2 && kind == 2) {
      fail(s, line, "'%s' is declared twice", name);
      return -1;
    }
    return found;  // a let or index name reused by another element
  }
  if (s->slot_count >= kMaxSlots) {
    fail(s, line, "too many names (limit %d)", kMaxSlots);
    return -1;
  }
  strcpy(s->names[s->slot_count], name);
  s->slot_kind[s->slot_count] = kind;
  s->slots[s->slot_count] = 0.0f;
  return s->slot_count++;
}

// ---------------------------------------------------------------------------
// Expression compiler (Pratt parser)
// ---------------------------------------------------------------------------
enum Tok : uint8_t { tEnd, tNum, tName, tOp, tLParen, tRParen, tComma, tQuestion, tColon };

struct Lexer {
  Screen* s;
  const char* text;
  size_t len, pos;
  int line;
  Tok tok;
  float num;
  const char* name;
  size_t name_len;
  char op[3];
  int depth;   // value stack depth while compiling
  int max_depth;
  int nest;    // parser recursion
};

bool lex(Lexer& L) {
  while (L.pos < L.len && (L.text[L.pos] == ' ' || L.text[L.pos] == '\t')) ++L.pos;
  if (L.pos >= L.len) {
    L.tok = tEnd;
    return true;
  }
  const char c = L.text[L.pos];
  if (isdigit(static_cast<unsigned char>(c)) || (c == '.' && L.pos + 1 < L.len && isdigit(static_cast<unsigned char>(L.text[L.pos + 1])))) {
    char buf[32];
    size_t n = 0;
    const bool hex = c == '0' && L.pos + 1 < L.len && (L.text[L.pos + 1] == 'x' || L.text[L.pos + 1] == 'X');
    while (L.pos < L.len && n + 1 < sizeof(buf) &&
           (isalnum(static_cast<unsigned char>(L.text[L.pos])) || L.text[L.pos] == '.')) {
      buf[n++] = L.text[L.pos++];
    }
    buf[n] = '\0';
    char* end = nullptr;
    L.num = hex ? static_cast<float>(strtoul(buf, &end, 16)) : strtof(buf, &end);
    if (!end || *end) return fail(L.s, L.line, "bad number '%s' in '%.*s'", buf, static_cast<int>(L.len), L.text);
    L.tok = tNum;
    return true;
  }
  if (c == '#') {
    // A color literal: #RRGGBB.
    size_t n = 0;
    while (L.pos + 1 + n < L.len && isxdigit(static_cast<unsigned char>(L.text[L.pos + 1 + n]))) ++n;
    if (n != 6) return fail(L.s, L.line, "a color needs six hex digits: '%.*s'", static_cast<int>(L.len), L.text);
    char buf[7];
    memcpy(buf, L.text + L.pos + 1, 6);
    buf[6] = '\0';
    L.num = static_cast<float>(strtoul(buf, nullptr, 16));
    L.pos += 7;
    L.tok = tNum;
    return true;
  }
  if (isalpha(static_cast<unsigned char>(c)) || c == '_') {
    L.name = L.text + L.pos;
    L.name_len = 0;
    while (L.pos < L.len && (isalnum(static_cast<unsigned char>(L.text[L.pos])) || L.text[L.pos] == '_')) {
      ++L.pos;
      ++L.name_len;
    }
    L.tok = tName;
    return true;
  }
  ++L.pos;
  switch (c) {
    case '(': L.tok = tLParen; return true;
    case ')': L.tok = tRParen; return true;
    case ',': L.tok = tComma; return true;
    case '?': L.tok = tQuestion; return true;
    case ':': L.tok = tColon; return true;
    default: break;
  }
  L.op[0] = c;
  L.op[1] = '\0';
  L.op[2] = '\0';
  if (L.pos < L.len) {
    const char d = L.text[L.pos];
    if ((strchr("<>=!", c) && d == '=') || (c == '&' && d == '&') || (c == '|' && d == '|')) {
      L.op[1] = d;
      ++L.pos;
    }
  }
  if (!strchr("+-*/%<>!", c) && strcmp(L.op, "==") && strcmp(L.op, "&&") && strcmp(L.op, "||")) {
    return fail(L.s, L.line, "unexpected '%s' in '%.*s'", L.op, static_cast<int>(L.len), L.text);
  }
  L.tok = tOp;
  return true;
}

bool emit(Lexer& L, uint8_t op, uint16_t arg = 0, float k = 0.0f, uint8_t argc = 0) {
  Screen* s = L.s;
  if (s->code_count >= static_cast<int>(sizeof(s->code) / sizeof(s->code[0]))) {
    return fail(s, L.line, "the expressions are too long in total (limit %d steps)",
                static_cast<int>(sizeof(s->code) / sizeof(s->code[0])));
  }
  s->code[s->code_count++] = {op, argc, arg, k};
  return true;
}

void push(Lexer& L, int n) {
  L.depth += n;
  if (L.depth > L.max_depth) L.max_depth = L.depth;
}

bool parseExpr(Lexer& L, int min_prec);

int binaryPrec(const Lexer& L) {
  if (L.tok == tQuestion) return 1;
  if (L.tok != tOp) return -1;
  const char* o = L.op;
  if (!strcmp(o, "||")) return 2;
  if (!strcmp(o, "&&")) return 3;
  if (!strcmp(o, "==") || !strcmp(o, "!=")) return 4;
  if (!strcmp(o, "<") || !strcmp(o, "<=") || !strcmp(o, ">") || !strcmp(o, ">=")) return 5;
  if (!strcmp(o, "+") || !strcmp(o, "-")) return 6;
  if (!strcmp(o, "*") || !strcmp(o, "/") || !strcmp(o, "%")) return 7;
  return -1;
}

bool parsePrimary(Lexer& L) {
  if (++L.nest > kMaxExprDepth) return fail(L.s, L.line, "expression nested too deeply");
  bool ok = true;
  if (L.tok == tNum) {
    ok = emit(L, kConst, 0, L.num) && lex(L);
    push(L, 1);
  } else if (L.tok == tOp && (L.op[0] == '-' || L.op[0] == '!') && L.op[1] == '\0') {
    const uint8_t op = L.op[0] == '-' ? kNeg : kNot;
    ok = lex(L) && parsePrimary(L) && emit(L, op);
  } else if (L.tok == tOp && L.op[0] == '+' && L.op[1] == '\0') {
    ok = lex(L) && parsePrimary(L);
  } else if (L.tok == tLParen) {
    ok = lex(L) && parseExpr(L, 0);
    if (ok && L.tok != tRParen) ok = fail(L.s, L.line, "missing ')' in '%.*s'", static_cast<int>(L.len), L.text);
    if (ok) ok = lex(L);
  } else if (L.tok == tName) {
    const char* name = L.name;
    const size_t len = L.name_len;
    if (!lex(L)) return false;
    if (L.tok == tLParen) {
      int fn = -1;
      for (int f = 0; f < fFnCount; ++f) {
        if (strlen(kFns[f].name) == len && !strncmp(kFns[f].name, name, len)) fn = f;
      }
      if (fn < 0) return fail(L.s, L.line, "unknown function '%.*s'", static_cast<int>(len), name);
      if (!lex(L)) return false;
      int argc = 0;
      if (L.tok != tRParen) {
        while (true) {
          if (!parseExpr(L, 0)) return false;
          ++argc;
          if (L.tok == tComma) {
            if (!lex(L)) return false;
            continue;
          }
          break;
        }
      }
      if (L.tok != tRParen) return fail(L.s, L.line, "missing ')' after %s(", kFns[fn].name);
      if (argc != kFns[fn].argc) {
        return fail(L.s, L.line, "%s() takes %d values, not %d", kFns[fn].name, kFns[fn].argc, argc);
      }
      ok = emit(L, kCall, static_cast<uint16_t>(fn), 0.0f, static_cast<uint8_t>(argc)) && lex(L);
      L.depth -= argc;
      push(L, 1);
    } else {
      float constant = 0.0f;
      bool is_const = false;
      if (len == 2 && !strncmp(name, "pi", 2)) {
        constant = kPi;
        is_const = true;
      } else if (len == 3 && !strncmp(name, "tau", 3)) {
        constant = kTwoPi;
        is_const = true;
      }
      for (int c = 0; !is_const && c < L.s->color_count; ++c) {
        if (strlen(L.s->colors[c].name) == len && !strncmp(L.s->colors[c].name, name, len)) {
          constant = static_cast<float>(L.s->colors[c].color);
          is_const = true;
        }
      }
      if (is_const) {
        ok = emit(L, kConst, 0, constant);
      } else {
        const int slot = findSlot(L.s, name, len);
        if (slot < 0) return fail(L.s, L.line, "unknown name '%.*s'", static_cast<int>(len), name);
        ok = emit(L, kLoad, static_cast<uint16_t>(slot));
      }
      push(L, 1);
    }
  } else {
    ok = fail(L.s, L.line, "a value is missing in '%.*s'", static_cast<int>(L.len), L.text);
  }
  --L.nest;
  return ok;
}

bool parseExpr(Lexer& L, int min_prec) {
  if (++L.nest > kMaxExprDepth) return fail(L.s, L.line, "expression nested too deeply");
  if (!parsePrimary(L)) return false;
  while (true) {
    const int prec = binaryPrec(L);
    if (prec < 0 || prec < min_prec) break;
    if (L.tok == tQuestion) {
      // cond ? a : b, right associative, via jumps.
      if (!lex(L)) return false;
      const int jz = L.s->code_count;
      if (!emit(L, kJz)) return false;
      --L.depth;
      if (!parseExpr(L, 0)) return false;
      if (L.tok != tColon) return fail(L.s, L.line, "missing ':' in '%.*s'", static_cast<int>(L.len), L.text);
      if (!lex(L)) return false;
      const int jmp = L.s->code_count;
      if (!emit(L, kJmp)) return false;
      --L.depth;
      L.s->code[jz].arg = static_cast<uint16_t>(L.s->code_count);
      if (!parseExpr(L, 1)) return false;
      L.s->code[jmp].arg = static_cast<uint16_t>(L.s->code_count);
      continue;
    }
    char op[3];
    memcpy(op, L.op, sizeof(op));
    if (!lex(L) || !parseExpr(L, prec + 1)) return false;
    uint8_t code = kAdd;
    if (!strcmp(op, "+")) code = kAdd;
    else if (!strcmp(op, "-")) code = kSub;
    else if (!strcmp(op, "*")) code = kMul;
    else if (!strcmp(op, "/")) code = kDiv;
    else if (!strcmp(op, "%")) code = kMod;
    else if (!strcmp(op, "<")) code = kLt;
    else if (!strcmp(op, "<=")) code = kLe;
    else if (!strcmp(op, ">")) code = kGt;
    else if (!strcmp(op, ">=")) code = kGe;
    else if (!strcmp(op, "==")) code = kEq;
    else if (!strcmp(op, "!=")) code = kNe;
    else if (!strcmp(op, "&&")) code = kAnd;
    else if (!strcmp(op, "||")) code = kOr;
    if (!emit(L, code)) return false;
    --L.depth;
  }
  --L.nest;
  return true;
}

// Compiles text into code; jump targets are absolute code indexes.
bool compileText(Screen* s, const char* text, size_t len, int line, Expr* out) {
  Lexer L{};
  L.s = s;
  L.text = text;
  L.len = len;
  L.line = line;
  const int start = s->code_count;
  if (!lex(L) || !parseExpr(L, 0)) return false;
  if (L.tok != tEnd) return fail(s, line, "unexpected text after the expression '%.*s'", static_cast<int>(len), text);
  if (L.max_depth > kMaxExprDepth) return fail(s, line, "expression too complex: '%.*s'", static_cast<int>(len), text);
  out->start = static_cast<uint16_t>(start);
  out->len = static_cast<uint16_t>(s->code_count - start);
  return true;
}

// A JSON number, string expression or "#RRGGBB" color.
bool compileNode(Screen* s, int node, Expr* out) {
  const JNode& n = s->json[node];
  if (n.type == J::Num) {
    Lexer L{};
    L.s = s;
    L.line = n.line;
    out->start = static_cast<uint16_t>(s->code_count);
    if (!emit(L, kConst, 0, n.num)) return false;
    out->len = 1;
    return true;
  }
  if (n.type == J::Bool) {
    Lexer L{};
    L.s = s;
    L.line = n.line;
    out->start = static_cast<uint16_t>(s->code_count);
    if (!emit(L, kConst, 0, n.boolean ? 1.0f : 0.0f)) return false;
    out->len = 1;
    return true;
  }
  if (n.type != J::Str) return fail(s, n.line, "expected a number or an expression");
  return compileText(s, s->src + n.str_off, n.str_len, n.line, out);
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------
uint32_t nextRand(Screen* s) {
  uint32_t x = s->rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  s->rng = x ? x : 0x9E3779B9u;
  return s->rng;
}

float callFn(Screen* s, uint8_t fn, const float* a) {
  switch (fn) {
    case fSin: return sinf(a[0]);
    case fCos: return cosf(a[0]);
    case fTan: return tanf(a[0]);
    case fAsin: return asinf(a[0] < -1 ? -1 : (a[0] > 1 ? 1 : a[0]));
    case fAcos: return acosf(a[0] < -1 ? -1 : (a[0] > 1 ? 1 : a[0]));
    case fAtan: return atanf(a[0]);
    case fAtan2: return atan2f(a[0], a[1]);
    case fSqrt: return a[0] > 0 ? sqrtf(a[0]) : 0.0f;
    case fAbs: return fabsf(a[0]);
    case fMin: return a[0] < a[1] ? a[0] : a[1];
    case fMax: return a[0] > a[1] ? a[0] : a[1];
    case fClamp: return a[0] < a[1] ? a[1] : (a[0] > a[2] ? a[2] : a[0]);
    case fClamp01: return a[0] < 0 ? 0.0f : (a[0] > 1 ? 1.0f : a[0]);
    case fMix: return a[0] + (a[1] - a[0]) * a[2];
    case fFloor: return floorf(a[0]);
    case fCeil: return ceilf(a[0]);
    case fRound: return roundf(a[0]);
    case fFract: return a[0] - floorf(a[0]);
    case fFmod: {
      // Always positive for a positive divisor, like GLSL mod().
      if (a[1] == 0.0f) return 0.0f;
      return a[0] - a[1] * floorf(a[0] / a[1]);
    }
    case fExp: return expf(a[0] > 80 ? 80 : a[0]);
    case fPow: return powf(a[0], a[1]);
    case fLog: return a[0] > 0 ? logf(a[0]) : 0.0f;
    case fHash: {
      // The built-in styles' fixed per-index fraction (assist_styles_lines.cpp).
      const int i = static_cast<int>(a[0]), salt = static_cast<int>(a[1]);
      int v = (i * 7 + salt * 13) % 23;
      if (v < 0) v += 23;
      return static_cast<float>(v) / 22.0f;
    }
    case fRand: {
      int lo = static_cast<int>(a[0]), hi = static_cast<int>(a[1]);
      if (hi < lo) {
        const int t = lo;
        lo = hi;
        hi = t;
      }
      const uint32_t span = static_cast<uint32_t>(hi - lo) + 1u;
      return static_cast<float>(lo + static_cast<int>(nextRand(s) % span));
    }
    case fFrand: return static_cast<float>(nextRand(s) >> 8) / 16777216.0f;
    case fStep: return a[1] < a[0] ? 0.0f : 1.0f;
    case fSmooth: {
      float x = a[1] == a[0] ? 0.0f : (a[2] - a[0]) / (a[1] - a[0]);
      x = x < 0 ? 0 : (x > 1 ? 1 : x);
      return x * x * (3 - 2 * x);
    }
    case fSign: return a[0] > 0 ? 1.0f : (a[0] < 0 ? -1.0f : 0.0f);
    default: return 0.0f;
  }
}

float eval(Screen* s, const Expr& e, float fallback = 0.0f) {
  if (!e.len) return fallback;
  float stack[kMaxExprDepth + 2];
  int sp = 0;
  const int end = e.start + e.len;
  for (int pc = e.start; pc < end;) {
    const Code& c = s->code[pc++];
    switch (c.op) {
      case kConst: stack[sp++] = c.k; break;
      case kLoad: stack[sp++] = s->slots[c.arg]; break;
      case kAdd: --sp; stack[sp - 1] += stack[sp]; break;
      case kSub: --sp; stack[sp - 1] -= stack[sp]; break;
      case kMul: --sp; stack[sp - 1] *= stack[sp]; break;
      case kDiv: --sp; stack[sp - 1] = stack[sp] == 0.0f ? 0.0f : stack[sp - 1] / stack[sp]; break;
      case kMod: --sp; stack[sp - 1] = stack[sp] == 0.0f ? 0.0f : fmodf(stack[sp - 1], stack[sp]); break;
      case kNeg: stack[sp - 1] = -stack[sp - 1]; break;
      case kNot: stack[sp - 1] = stack[sp - 1] == 0.0f ? 1.0f : 0.0f; break;
      case kLt: --sp; stack[sp - 1] = stack[sp - 1] < stack[sp]; break;
      case kLe: --sp; stack[sp - 1] = stack[sp - 1] <= stack[sp]; break;
      case kGt: --sp; stack[sp - 1] = stack[sp - 1] > stack[sp]; break;
      case kGe: --sp; stack[sp - 1] = stack[sp - 1] >= stack[sp]; break;
      case kEq: --sp; stack[sp - 1] = stack[sp - 1] == stack[sp]; break;
      case kNe: --sp; stack[sp - 1] = stack[sp - 1] != stack[sp]; break;
      case kAnd: --sp; stack[sp - 1] = stack[sp - 1] != 0.0f && stack[sp] != 0.0f; break;
      case kOr: --sp; stack[sp - 1] = stack[sp - 1] != 0.0f || stack[sp] != 0.0f; break;
      case kJz:
        if (stack[--sp] == 0.0f) pc = c.arg;
        break;
      case kJmp: pc = c.arg; break;
      case kCall: {
        sp -= c.argc;
        stack[sp] = callFn(s, static_cast<uint8_t>(c.arg), stack + sp);
        ++sp;
        break;
      }
      default: break;
    }
  }
  const float v = sp > 0 ? stack[sp - 1] : fallback;
  return isfinite(v) ? v : 0.0f;
}

// Clamped and snapped to half pixels: a shape that moved less than that (a
// radius still easing in) leaves the draw list as it was, so nothing redraws.
float coord(float v) {
  v = v < -kCoordLimit ? -kCoordLimit : (v > kCoordLimit ? kCoordLimit : v);
  return roundf(v * 2.0f) * 0.5f;
}

uint8_t opaOf(float alpha) {
  if (alpha <= 0.0f) return 0;
  if (alpha >= 1.0f) return 255;
  return static_cast<uint8_t>(alpha * 255.0f);
}

uint32_t colorOf(float v) {
  if (v <= 0.0f) return 0;
  if (v >= 16777215.0f) return 0xFFFFFF;
  return static_cast<uint32_t>(v);
}

uint8_t widthOf(float v) {
  if (v < 1.0f) return 1;
  if (v > 120.0f) return 120;
  return static_cast<uint8_t>(v + 0.5f);
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------
bool readFlag(Screen* s, int node, const char* key, bool def, bool* out) {
  const int c = child(s, node, key);
  *out = def;
  if (c < 0) return true;
  if (s->json[c].type != J::Bool) return fail(s, s->json[c].line, "'%s' must be true or false", key);
  *out = s->json[c].boolean;
  return true;
}

bool readInt(Screen* s, int node, const char* key, int def, int lo, int hi, int* out) {
  const int c = child(s, node, key);
  *out = def;
  if (c < 0) return true;
  if (s->json[c].type != J::Num) return fail(s, s->json[c].line, "'%s' must be a whole number", key);
  const float v = s->json[c].num;
  if (v != floorf(v) || v < lo || v > hi) {
    return fail(s, s->json[c].line, "'%s' must be a whole number from %d to %d", key, lo, hi);
  }
  *out = static_cast<int>(v);
  return true;
}

// Splits "text {expr:fmt} more" into segments.
bool compileTemplate(Screen* s, int node, uint16_t* first, uint16_t* count) {
  const JNode& n = s->json[node];
  if (n.type != J::Str) return fail(s, n.line, "a text must be a string");
  char text[160];
  if (!copyString(s, n, text, sizeof(text))) return false;
  *first = static_cast<uint16_t>(s->seg_count);
  *count = 0;
  const char* p = text;
  while (*p) {
    if (s->seg_count >= kMaxSegments) return fail(s, n.line, "too many text parts (limit %d)", kMaxSegments);
    Segment& seg = s->segs[s->seg_count];
    seg = Segment{};
    if (*p == '{') {
      const char* close = strchr(p, '}');
      if (!close) return fail(s, n.line, "'{' without '}' in \"%s\"", text);
      // The format follows the last ':' that is not inside a ?: (none can be
      // after the expression's own parts, so take the last ':' if the text
      // after it looks like a format).
      const char* colon = nullptr;
      for (const char* q = close - 1; q > p; --q) {
        if (*q == ':') {
          colon = q;
          break;
        }
      }
      const char* expr_end = close;
      if (colon) {
        const char* f = colon + 1;
        size_t len = static_cast<size_t>(close - f);
        bool looks = len > 0 && len < sizeof(seg.fmt) && strchr("dfxX", f[len - 1]);
        for (size_t i = 0; looks && i + 1 < len; ++i) looks = strchr("0123456789.-+ ", f[i]) != nullptr;
        if (looks) {
          memcpy(seg.fmt, f, len);
          seg.fmt[len] = '\0';
          expr_end = colon;
        }
      }
      if (!seg.fmt[0]) strcpy(seg.fmt, "g");
      if (!compileText(s, p + 1, static_cast<size_t>(expr_end - p - 1), n.line, &seg.expr)) return false;
      p = close + 1;
    } else {
      const char* brace = strchr(p, '{');
      const size_t len = brace ? static_cast<size_t>(brace - p) : strlen(p);
      if (s->pool_used + static_cast<int>(len) > kStringPool) return fail(s, n.line, "too much fixed text");
      memcpy(s->pool + s->pool_used, p, len);
      seg.lit_off = static_cast<uint16_t>(s->pool_used);
      seg.lit_len = static_cast<uint16_t>(len);
      s->pool_used += static_cast<int>(len);
      p += len;
    }
    ++s->seg_count;
    ++*count;
  }
  return true;
}

uint32_t exprCost(const Screen* s, const Elem& e) {
  uint32_t cost = e.cond.len;
  for (int i = 0; i < kMaxProps; ++i) cost += e.p[i].len;
  for (int i = 0; i < e.let_count; ++i) cost += s->lets[e.let_first + i].expr.len;
  for (int i = 0; i < e.seg_count; ++i) cost += s->segs[e.seg_first + i].expr.len * (e.kind == kLog ? 1 : 1);
  return cost;
}

bool loadElements(Screen* s, int array, int depth, uint32_t mult, uint32_t* ops, uint32_t* cmds);

constexpr const char* kShapeNames[kShapeCount] = {"wide", "square", "tall"};

int shapeNamed(const char* name) {
  for (int i = 0; i < kShapeCount; ++i) {
    if (!strcmp(kShapeNames[i], name)) return i;
  }
  return -1;
}

// "on": ["wide", "square"] -> a bit per shape; absent = every shape.
bool readShapes(Screen* s, int node, uint8_t* out) {
  *out = (1u << kShapeCount) - 1u;
  if (node < 0) return true;
  if (s->json[node].type != J::Arr) return fail(s, s->json[node].line, "\"on\" is a list of shapes (wide, square, tall)");
  *out = 0;
  for (int c = s->json[node].first; c >= 0; c = s->json[c].next) {
    char name[16];
    const int shape = s->json[c].type == J::Str && copyString(s, s->json[c], name, sizeof(name)) ? shapeNamed(name) : -1;
    if (shape < 0) return fail(s, s->json[c].line, "unknown shape (wide, square, tall)");
    *out |= static_cast<uint8_t>(1u << shape);
  }
  return true;
}

bool loadElement(Screen* s, int node, int depth, uint32_t mult, uint32_t* ops, uint32_t* cmds) {
  const JNode& n = s->json[node];
  if (n.type != J::Obj) return fail(s, n.line, "an item must be an object");
  if (s->elem_count >= kMaxElements) return fail(s, n.line, "too many items (limit %d)", kMaxElements);
  const int type_node = child(s, node, "type");
  if (type_node < 0 || s->json[type_node].type != J::Str) return fail(s, n.line, "an item needs a \"type\"");
  char type[16];
  if (!copyString(s, s->json[type_node], type, sizeof(type))) return false;
  int kind = -1;
  for (int k = 0; k < kKindCount; ++k) {
    if (!strcmp(kKindNames[k], type)) kind = k;
  }
  if (kind < 0) return fail(s, n.line, "unknown item type '%s'", type);
  const int index = s->elem_count++;
  Elem& e = s->elems[index];
  e = Elem{};
  e.kind = static_cast<uint8_t>(kind);
  e.index_slot = -1;
  e.repeat = 1;

  // Every key must be known: a typo is an error, not a silent default.
  static const char* const kCommon[] = {"type", "repeat", "as", "let", "if", "glow", "round", "items",
                                        "font", "align", "text", "line", "rows", "id", "points", "count",
                                        "trail", "fill", "note", "on"};
  for (int c = n.first; c >= 0; c = s->json[c].next) {
    bool known = false;
    for (const char* k : kCommon) known = known || keyIs(s, s->json[c], k);
    for (int i = 0; i < kMaxProps && kProps[kind][i].name; ++i) known = known || keyIs(s, s->json[c], kProps[kind][i].name);
    if (!known) {
      char key[32];
      keyText(s, s->json[c], key, sizeof(key));
      return fail(s, s->json[c].line, "a %s has no '%s'", type, key);
    }
  }

  if (!readShapes(s, child(s, node, "on"), &e.on)) return false;
  int repeat = 1;
  if (!readInt(s, node, "repeat", 1, 1, kMaxRepeat, &repeat)) return false;
  e.repeat = static_cast<uint16_t>(repeat);
  const int as_node = child(s, node, "as");
  if (as_node >= 0 || repeat > 1 || kind == kLog) {
    // A log's index is its row, k.
    char name[24];
    strcpy(name, kind == kLog ? "k" : "i");
    if (as_node >= 0 && !copyString(s, s->json[as_node], name, sizeof(name))) return false;
    e.index_slot = static_cast<int16_t>(addSlot(s, name, n.line, 0));
    if (e.index_slot < 0) return false;
  }
  const int let_node = child(s, node, "let");
  e.let_first = static_cast<uint16_t>(s->let_count);
  if (let_node >= 0) {
    if (s->json[let_node].type != J::Obj) return fail(s, s->json[let_node].line, "'let' must be an object");
    for (int c = s->json[let_node].first; c >= 0; c = s->json[c].next) {
      if (s->let_count >= kMaxLets) return fail(s, s->json[c].line, "too many 'let' names (limit %d)", kMaxLets);
      char name[24];
      keyText(s, s->json[c], name, sizeof(name));
      Let& let = s->lets[s->let_count];
      // Compiled before the name exists: a let cannot read itself.
      if (!compileNode(s, c, &let.expr)) return false;
      let.slot = static_cast<int16_t>(addSlot(s, name, s->json[c].line, 0));
      if (let.slot < 0) return false;
      ++s->let_count;
      ++e.let_count;
    }
  }
  const int if_node = child(s, node, "if");
  if (if_node >= 0 && !compileNode(s, if_node, &e.cond)) return false;

  bool flag = false;
  if (!readFlag(s, node, "glow", false, &flag)) return false;
  if (flag) e.flags |= kGlow;
  const int round_node = child(s, node, "round");
  if (!readFlag(s, node, "round", kind == kLine || kind == kArc, &flag)) return false;
  if (flag) e.flags |= kRound;
  if (round_node >= 0) e.flags |= kRoundSet;
  if (!readFlag(s, node, "fill", false, &flag)) return false;
  if (flag) e.flags |= kFill;

  // A generated poly's x and y read its point index i and position u (0..1).
  int16_t poly_i = -1, poly_u = -1;
  if (kind == kPoly && child(s, node, "points") < 0) {
    poly_i = static_cast<int16_t>(addSlot(s, "i", n.line, 0));
    poly_u = static_cast<int16_t>(addSlot(s, "u", n.line, 0));
    if (poly_i < 0 || poly_u < 0) return false;
  }
  for (int i = 0; i < kMaxProps && kProps[kind][i].name; ++i) {
    const int c = child(s, node, kProps[kind][i].name);
    if (c >= 0) {
      if (!compileNode(s, c, &e.p[i])) return false;
    } else if (kProps[kind][i].required) {
      return fail(s, n.line, "a %s needs '%s'", type, kProps[kind][i].name);
    }
  }

  const uint32_t here = mult * e.repeat;
  uint32_t draws = 0;
  switch (kind) {
    case kLine:
    case kArc: draws = (e.flags & kGlow) ? 2 : 1; break;
    case kDisc:
    case kRect: draws = 1; break;
    case kFrame: draws = 4; break;
    case kPoly: {
      if (s->poly_count >= kMaxPolylines) return fail(s, n.line, "too many polylines (limit %d)", kMaxPolylines);
      if (e.repeat != 1 || mult != 1) return fail(s, n.line, "a poly cannot repeat (use its 'count')");
      e.poly = static_cast<uint8_t>(s->poly_count);
      Poly& poly = s->polys[s->poly_count++];
      poly = Poly{};
      poly.i_slot = poly.u_slot = -1;
      const int points = child(s, node, "points");
      int count = 0;
      if (points >= 0) {
        if (s->json[points].type != J::Arr) return fail(s, s->json[points].line, "'points' must be a list of [x, y]");
        for (int p = s->json[points].first; p >= 0; p = s->json[p].next) {
          if (count >= kMaxLiteralPoints) return fail(s, s->json[p].line, "too many listed points (limit %d)", kMaxLiteralPoints);
          const int px = s->json[p].first;
          const int py = px >= 0 ? s->json[px].next : -1;
          if (s->json[p].type != J::Arr || px < 0 || py < 0 || s->json[py].next >= 0) {
            return fail(s, s->json[p].line, "a point must be [x, y]");
          }
          if (!compileNode(s, px, &poly.lit[count][0]) || !compileNode(s, py, &poly.lit[count][1])) return false;
          ++count;
        }
        e.flags |= kLiteral;
      } else {
        if (!readInt(s, node, "count", 0, 2, kMaxPolyPoints, &count)) return false;
        if (count < 2 || !e.p[kP_X].len || !e.p[kP_Y].len) {
          return fail(s, n.line, "a poly needs 'points', or 'count' with 'x' and 'y' (using i and u)");
        }
        poly.i_slot = poly_i;
        poly.u_slot = poly_u;
      }
      if (count < 2) return fail(s, n.line, "a poly needs at least two points");
      poly.count = static_cast<uint16_t>(count);
      const int trail = child(s, node, "trail");
      if (trail >= 0) {
        if (s->json[trail].type != J::Obj) return fail(s, s->json[trail].line, "'trail' must be an object");
        int keep = 0, frames = 1, width = 1;
        if (!readInt(s, trail, "keep", 0, 0, kMaxTrail, &keep) || !readInt(s, trail, "frames", 1, 1, 60, &frames) ||
            !readInt(s, trail, "width", 1, 1, 20, &width)) {
          return false;
        }
        poly.keep = static_cast<uint8_t>(keep);
        poly.frames = static_cast<uint8_t>(frames);
        poly.width = static_cast<uint8_t>(width);
        const int alpha = child(s, trail, "alpha");
        int a = 0;
        if (alpha >= 0) {
          if (s->json[alpha].type != J::Arr) return fail(s, s->json[alpha].line, "trail 'alpha' must be a list of numbers");
          for (int c = s->json[alpha].first; c >= 0 && a < kMaxTrail; c = s->json[c].next, ++a) {
            if (s->json[c].type != J::Num) return fail(s, s->json[c].line, "trail 'alpha' must be a list of numbers");
            poly.alpha[a] = s->json[c].num;
          }
        }
        for (; a < kMaxTrail; ++a) poly.alpha[a] = 0.3f / (a + 1);
      }
      const int needed = count * (1 + poly.keep);
      if (s->point_count + needed > kPointPool) return fail(s, n.line, "the polylines need too many points (limit %d)", kPointPool);
      poly.pts = static_cast<uint16_t>(s->point_count);
      s->point_count += needed;
      draws = (count - 1) * ((e.flags & kGlow) ? 2 : 1) + (count - 1) * poly.keep;
      *ops += exprCost(s, e) - e.p[kP_X].len - e.p[kP_Y].len + (e.p[kP_X].len + e.p[kP_Y].len) * count;
      if (e.flags & kLiteral) {
        for (int p = 0; p < count; ++p) *ops += poly.lit[p][0].len + poly.lit[p][1].len;
      }
      *cmds += draws;
      e.end = static_cast<uint16_t>(s->elem_count);
      return true;
    }
    case kText:
    case kLog: {
      if (e.repeat != 1 || mult != 1) return fail(s, n.line, "a %s cannot repeat", type);
      int font = 16;
      if (!readInt(s, node, "font", 16, 1, 96, &font)) return false;
      if (!assist_screen_font(font)) return fail(s, n.line, "no font of size %d on the panel (16, 20, 24, 28, 32, 40, 48)", font);
      e.font = static_cast<uint8_t>(font);
      const int align = child(s, node, "align");
      e.align = LV_TEXT_ALIGN_LEFT;
      if (align >= 0) {
        char a[8];
        if (!copyString(s, s->json[align], a, sizeof(a))) return false;
        if (!strcmp(a, "center")) e.align = LV_TEXT_ALIGN_CENTER;
        else if (!strcmp(a, "right")) e.align = LV_TEXT_ALIGN_RIGHT;
        else if (strcmp(a, "left")) return fail(s, n.line, "'align' is left, center or right");
      }
      const int text = child(s, node, kind == kText ? "text" : "line");
      if (text < 0) return fail(s, n.line, "a %s needs '%s'", type, kind == kText ? "text" : "line");
      if (!compileTemplate(s, text, &e.seg_first, &e.seg_count)) return false;
      int rows = 1;
      if (kind == kLog) {
        if (s->log_count >= kMaxLogs) return fail(s, n.line, "too many logs (limit %d)", kMaxLogs);
        if (!readInt(s, node, "rows", 4, 1, kMaxLogRows, &rows)) return false;
        e.log = static_cast<uint8_t>(s->log_count);
        Log& log = s->logs[s->log_count++];
        log.count = 0;
        log.count_slot = -1;
        const int id = child(s, node, "id");
        if (id >= 0) {
          char name[18];  // "<name>_count" fits a slot name
          char slot_name[24];
          if (!copyString(s, s->json[id], name, sizeof(name))) return false;
          snprintf(slot_name, sizeof(slot_name), "%s_count", name);
          log.count_slot = static_cast<int16_t>(findSlot(s, slot_name, strlen(slot_name)));  // declareLogCounts
          if (log.count_slot < 0) return fail(s, n.line, "a log's \"id\" is a name (letters, digits, _)");
        }
      }
      if (s->text_count + rows > kMaxTexts) return fail(s, n.line, "too many text lines (limit %d)", kMaxTexts);
      e.text_first = static_cast<uint8_t>(s->text_count);
      e.rows = static_cast<uint8_t>(rows);
      s->text_count += rows;
      draws = static_cast<uint32_t>(rows);
      break;
    }
    case kGroup: {
      const int items = child(s, node, "items");
      if (items < 0 || s->json[items].type != J::Arr) return fail(s, n.line, "a group needs 'items'");
      if (depth + 1 > kMaxGroupDepth) return fail(s, n.line, "groups nested too deeply (limit %d)", kMaxGroupDepth);
      *ops += exprCost(s, e) * here;
      if (!loadElements(s, items, depth + 1, here, ops, cmds)) return false;
      s->elems[index].end = static_cast<uint16_t>(s->elem_count);
      return true;
    }
    default: break;
  }
  if (kind != kGroup && child(s, node, "items") >= 0) return fail(s, n.line, "only a group has 'items'");
  *ops += exprCost(s, e) * here;
  *cmds += draws * here;
  e.end = static_cast<uint16_t>(s->elem_count);
  return true;
}

bool loadElements(Screen* s, int array, int depth, uint32_t mult, uint32_t* ops, uint32_t* cmds) {
  for (int c = s->json[array].first; c >= 0; c = s->json[c].next) {
    if (!loadElement(s, c, depth, mult, ops, cmds)) return false;
  }
  return true;
}

// Every log's "<id>_count" is declared before anything compiles, so shapes
// and vars before the log (drawn under it) can use it too.
bool declareLogCounts(Screen* s, int array, int depth) {
  if (array < 0 || s->json[array].type != J::Arr || depth > kMaxGroupDepth) return true;
  for (int c = s->json[array].first; c >= 0; c = s->json[c].next) {
    if (s->json[c].type != J::Obj) continue;
    const int type = child(s, c, "type");
    char kind[16] = "";
    if (type >= 0 && s->json[type].type == J::Str && s->json[type].str_len < sizeof(kind)) copyString(s, s->json[type], kind, sizeof(kind));
    if (!strcmp(kind, "group")) {
      if (!declareLogCounts(s, child(s, c, "items"), depth + 1)) return false;
      continue;
    }
    const int id = child(s, c, "id");
    if (strcmp(kind, "log") || id < 0 || s->json[id].type != J::Str) continue;
    char name[18];  // "<name>_count" fits a slot name
    char slot_name[24];
    if (!copyString(s, s->json[id], name, sizeof(name))) return false;
    snprintf(slot_name, sizeof(slot_name), "%s_count", name);
    if (addSlot(s, slot_name, s->json[id].line, 2) < 0) return false;
  }
  return true;
}

// Reach keys over the values already in out.
bool readReach(Screen* s, int node, Reach* out) {
  if (node < 0) return true;
  if (s->json[node].type != J::Obj) return fail(s, s->json[node].line, "\"reach\" is an object");
  for (int c = s->json[node].first; c >= 0; c = s->json[c].next) {
    if (s->json[c].type != J::Num) return fail(s, s->json[c].line, "reach values are numbers");
    if (keyIs(s, s->json[c], "half_r")) out->half_r = s->json[c].num;
    else if (keyIs(s, s->json[c], "side_r")) out->side_r = s->json[c].num;
    else if (keyIs(s, s->json[c], "side_px")) out->side_px = s->json[c].num;
    else return fail(s, s->json[c].line, "reach has half_r, side_r and side_px");
  }
  return true;
}

// "layouts": {"tall": {"reach": {...}, "vars": {"name": "expr"}}, ...}: per
// shape, reach keys over the file's and other formulas for derived vars. The
// worst case per frame counts the longest formula of each var.
bool loadLayouts(Screen* s, int node, uint32_t* ops) {
  if (node < 0) return true;
  if (s->json[node].type != J::Obj) return fail(s, s->json[node].line, "\"layouts\" is an object");
  for (int c = s->json[node].first; c >= 0; c = s->json[c].next) {
    char name[16];
    keyText(s, s->json[c], name, sizeof(name));
    const int shape = shapeNamed(name);
    if (shape < 0) return fail(s, s->json[c].line, "unknown layout '%s' (wide, square, tall)", name);
    if (s->json[c].type != J::Obj) return fail(s, s->json[c].line, "a layout is an object");
    s->info.layouts |= static_cast<uint8_t>(1u << shape);
    for (int f = s->json[c].first; f >= 0; f = s->json[f].next) {
      if (keyIs(s, s->json[f], "reach")) {
        if (!readReach(s, f, &s->info.reach[shape])) return false;
      } else if (keyIs(s, s->json[f], "vars")) {
        if (s->json[f].type != J::Obj) return fail(s, s->json[f].line, "a layout's \"vars\" is an object");
        for (int v = s->json[f].first; v >= 0; v = s->json[v].next) {
          char key[24];
          keyText(s, s->json[v], key, sizeof(key));
          const int slot = findSlot(s, key, strlen(key));
          Var* var = nullptr;
          for (int k = 0; k < s->var_count && slot >= 0; ++k) {
            if (s->vars[k].slot == slot) var = &s->vars[k];
          }
          if (!var) return fail(s, s->json[v].line, "layout var '%s' is not in \"vars\"", key);
          if (var->kind != 0) return fail(s, s->json[v].line, "a layout can only change a derived var ('%s' is state)", key);
          if (!compileNode(s, v, &var->alt[shape])) return false;
        }
      } else if (!keyIs(s, s->json[f], "note")) {
        char key[24];
        keyText(s, s->json[f], key, sizeof(key));
        return fail(s, s->json[f].line, "a layout has reach, vars and note, not '%s'", key);
      }
    }
  }
  // ops holds each derived var's own formula; add what a longer layout one costs.
  for (int k = 0; k < s->var_count; ++k) {
    const Var& v = s->vars[k];
    uint16_t longest = v.expr.len;
    for (const Expr& alt : v.alt) longest = alt.len > longest ? alt.len : longest;
    *ops += longest - v.expr.len;
  }
  return true;
}

bool loadFile(Screen* s) {
  JParser p{s, 0, 1};
  const int root = parseValue(p, 0);
  if (root < 0) return false;
  skipSpace(p);
  if (p.pos != s->src_len) return fail(s, p.line, "text after the end of the file");
  if (s->json[root].type != J::Obj) return fail(s, 1, "the file must be one JSON object");

  static const char* const kTop[] = {"format", "id", "name", "needs", "label_color", "reach", "colors",
                                     "vars", "items", "note", "layouts"};
  for (int c = s->json[root].first; c >= 0; c = s->json[c].next) {
    bool known = false;
    for (const char* k : kTop) known = known || keyIs(s, s->json[c], k);
    if (!known) {
      char key[32];
      keyText(s, s->json[c], key, sizeof(key));
      return fail(s, s->json[c].line, "unknown top-level key '%s'", key);
    }
  }
  int format = 0;
  if (!readInt(s, root, "format", 0, 1, 1, &format)) return false;
  if (format != 1) return fail(s, 1, "\"format\": 1 is required");
  const int id = child(s, root, "id");
  if (id < 0 || s->json[id].type != J::Str || !copyString(s, s->json[id], s->info.id, sizeof(s->info.id))) {
    return fail(s, 1, "\"id\" is required ([a-z0-9_], up to 32)");
  }
  for (const char* c = s->info.id; *c; ++c) {
    if (!(islower(static_cast<unsigned char>(*c)) || isdigit(static_cast<unsigned char>(*c)) || *c == '_')) {
      return fail(s, s->json[id].line, "\"id\" may only hold a-z, 0-9 and _");
    }
  }
  const int name = child(s, root, "name");
  if (name >= 0 && !copyString(s, s->json[name], s->info.name, sizeof(s->info.name))) return false;
  if (!s->info.name[0]) strcpy(s->info.name, s->info.id);

  const int needs = child(s, root, "needs");
  if (needs >= 0) {
    if (s->json[needs].type != J::Arr) return fail(s, s->json[needs].line, "\"needs\" is a list");
    for (int c = s->json[needs].first; c >= 0; c = s->json[c].next) {
      char need[16];
      if (s->json[c].type != J::Str || !copyString(s, s->json[c], need, sizeof(need))) return fail(s, s->json[c].line, "unknown need");
      if (!strcmp(need, "wide")) s->info.needs |= kNeedWide;
      else if (!strcmp(need, "tear_free")) s->info.needs |= kNeedTearFree;
      else return fail(s, s->json[c].line, "unknown need '%s' (wide, tear_free)", need);
    }
  }

  if (!declareLogCounts(s, child(s, root, "items"), 0)) return false;

  // Colors first: expressions may use their names.
  const int colors = child(s, root, "colors");
  if (colors >= 0) {
    if (s->json[colors].type != J::Obj) return fail(s, s->json[colors].line, "\"colors\" is an object");
    for (int c = s->json[colors].first; c >= 0; c = s->json[c].next) {
      if (s->color_count >= kMaxColors) return fail(s, s->json[c].line, "too many colors (limit %d)", kMaxColors);
      char value[12];
      if (s->json[c].type != J::Str || !copyString(s, s->json[c], value, sizeof(value)) || value[0] != '#' ||
          strlen(value) != 7) {
        return fail(s, s->json[c].line, "a color is \"#RRGGBB\"");
      }
      keyText(s, s->json[c], s->colors[s->color_count].name, sizeof(s->colors[0].name));
      if (!validName(s->colors[s->color_count].name)) return fail(s, s->json[c].line, "a color name is letters, digits and _");
      // A name means one thing: built-in names (and log counts) come first.
      if (findSlot(s, s->colors[s->color_count].name, strlen(s->colors[s->color_count].name)) >= 0) {
        return fail(s, s->json[c].line, "'%s' is already a name, not a color", s->colors[s->color_count].name);
      }
      s->colors[s->color_count].color = static_cast<uint32_t>(strtoul(value + 1, nullptr, 16));
      ++s->color_count;
    }
  }
  s->info.label_color = 0xC8D2E6;
  const int label = child(s, root, "label_color");
  if (label >= 0) {
    Expr e;
    if (!compileNode(s, label, &e)) return false;
    s->info.label_color = colorOf(eval(s, e));
  }
  Reach base{2.0f, 0.0f, 0.0f};
  if (!readReach(s, child(s, root, "reach"), &base)) return false;
  for (Reach& r : s->info.reach) r = base;

  uint32_t ops = 0, cmds = 0;
  // Vars: all names first, so any expression may read any var (a later one
  // holds its value from the frame before).
  const int vars = child(s, root, "vars");
  if (vars >= 0) {
    if (s->json[vars].type != J::Obj) return fail(s, s->json[vars].line, "\"vars\" is an object");
    for (int c = s->json[vars].first; c >= 0; c = s->json[c].next) {
      if (s->var_count >= kMaxVars) return fail(s, s->json[c].line, "too many vars (limit %d)", kMaxVars);
      char key[24];
      keyText(s, s->json[c], key, sizeof(key));
      Var& v = s->vars[s->var_count++];
      v = Var{};
      v.slot = static_cast<int16_t>(addSlot(s, key, s->json[c].line, 2));
      if (v.slot < 0) return false;
    }
    int k = 0;
    for (int c = s->json[vars].first; c >= 0; c = s->json[c].next, ++k) {
      Var& v = s->vars[k];
      const JNode& n = s->json[c];
      if (n.type == J::Obj) {
        v.kind = 1;
        for (int f = n.first; f >= 0; f = s->json[f].next) {
          if (!keyIs(s, s->json[f], "init") && !keyIs(s, s->json[f], "next") && !keyIs(s, s->json[f], "every") &&
              !keyIs(s, s->json[f], "frames")) {
            return fail(s, s->json[f].line, "a state var has init, next, every or frames");
          }
        }
        const int init = child(s, c, "init");
        const int next = child(s, c, "next");
        const int every = child(s, c, "every");
        int frames = 0;
        if (init >= 0 && !compileNode(s, init, &v.expr)) return false;
        if (next >= 0 && !compileNode(s, next, &v.next)) return false;
        if (every >= 0 && !compileNode(s, every, &v.every)) return false;
        if (!readInt(s, c, "frames", 0, 0, 3600, &frames)) return false;
        v.frames = static_cast<uint16_t>(frames);
        ops += v.next.len + v.every.len;
      } else {
        if (!compileNode(s, c, &v.expr)) return false;
        ops += v.expr.len;
      }
    }
  }
  if (!loadLayouts(s, child(s, root, "layouts"), &ops)) return false;
  const int items = child(s, root, "items");
  if (items < 0 || s->json[items].type != J::Arr) return fail(s, 1, "\"items\" (a list) is required");
  if (!loadElements(s, items, 0, 1, &ops, &cmds)) return false;
  if (ops > kMaxFrameOps) {
    return fail(s, 0, "too much work per frame: %lu expression steps (limit %lu)", static_cast<unsigned long>(ops),
                static_cast<unsigned long>(kMaxFrameOps));
  }
  if (cmds > kMaxFrameCmds) {
    return fail(s, 0, "too many shapes per frame: %lu (limit %lu)", static_cast<unsigned long>(cmds),
                static_cast<unsigned long>(kMaxFrameCmds));
  }
  s->info.frame_ops = ops;
  s->info.frame_cmds = cmds;
  return true;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
// out = the box around a and b (lv_area_join is private in LVGL 9.6).
void joinArea(lv_area_t* out, const lv_area_t* a, const lv_area_t* b) {
  const lv_area_t r{a->x1 < b->x1 ? a->x1 : b->x1, a->y1 < b->y1 ? a->y1 : b->y1, a->x2 > b->x2 ? a->x2 : b->x2,
                    a->y2 > b->y2 ? a->y2 : b->y2};
  *out = r;
}

void grow(Screen* s, float x1, float y1, float x2, float y2) {
  lv_area_t a;
  a.x1 = static_cast<int32_t>(floorf(fminf(x1, x2))) - 2;
  a.y1 = static_cast<int32_t>(floorf(fminf(y1, y2))) - 2;
  a.x2 = static_cast<int32_t>(ceilf(fmaxf(x1, x2))) + 2;
  a.y2 = static_cast<int32_t>(ceilf(fmaxf(y1, y2))) + 2;
  Region& r = s->regions[s->region];
  if (!r.valid) {
    r.box = a;
    r.valid = true;
    return;
  }
  joinArea(&r.box, &r.box, &a);
}

Cmd* addCmd(Screen* s) {
  if (s->cmd_count >= kMaxFrameCmds) return nullptr;
  Cmd* c = &s->cmds[s->cmd_count++];
  memset(c, 0, sizeof(*c));
  return c;
}

void addLine(Screen* s, float x1, float y1, float x2, float y2, float width, uint32_t color, float alpha, bool round,
             uint16_t dash = 0, uint16_t gap = 0) {
  if (alpha < 0.02f) return;
  Cmd* c = addCmd(s);
  if (!c) return;
  c->kind = cLine;
  c->flags = round ? cfRound : 0;
  c->width = widthOf(width);
  c->opa = opaOf(alpha);
  c->color = color;
  c->a = coord(x1);
  c->b = coord(y1);
  c->c = coord(x2);
  c->d = coord(y2);
  c->x1 = dash;
  c->x2 = gap;
  const float h = width / 2.0f + 1.0f;
  grow(s, c->a - h, c->b - h, c->c + h, c->d + h);
  grow(s, c->c - h, c->d - h, c->a + h, c->b + h);
}

void addArc(Screen* s, float x, float y, float radius, float start, float end, float width, uint32_t color, float alpha,
            bool round) {
  if (alpha < 0.02f || radius < 1.0f) return;
  Cmd* c = addCmd(s);
  if (!c) return;
  c->kind = cArc;
  c->flags = round ? cfRound : 0;
  c->width = widthOf(width);
  c->opa = opaOf(alpha);
  c->color = color;
  c->a = coord(x);
  c->b = coord(y);
  c->c = roundf(fminf(radius, 4000.0f));
  // Whole degrees, as drawn; a full ring stays full.
  const bool full = end - start >= 360.0f;
  c->d = full ? 0.0f : truncf(fmodf(start, 360.0f));
  c->e = full ? 360.0f : c->d + truncf(end - start);
  grow(s, c->a - c->c - 1, c->b - c->c - 1, c->a + c->c + 1, c->b + c->c + 1);
}

void formatText(Screen* s, const Elem& e, char* out) {
  size_t used = 0;
  out[0] = '\0';
  for (int i = 0; i < e.seg_count; ++i) {
    const Segment& seg = s->segs[e.seg_first + i];
    char part[kTextBytes];
    if (seg.expr.len) {
      const float v = eval(s, seg.expr);
      char fmt[10] = "%";
      strcat(fmt, seg.fmt);
      const char conv = seg.fmt[strlen(seg.fmt) - 1];
      if (conv == 'd' || conv == 'x' || conv == 'X') {
        // Conversions with 'l' for long; build "%<flags>l<conv>".
        char lfmt[12];
        const size_t n = strlen(fmt);
        memcpy(lfmt, fmt, n - 1);
        lfmt[n - 1] = 'l';
        lfmt[n] = conv;
        lfmt[n + 1] = '\0';
        const long iv = static_cast<long>(fmaxf(-2e9f, fminf(2e9f, v)));
        if (conv == 'd') snprintf(part, sizeof(part), lfmt, iv);
        else snprintf(part, sizeof(part), lfmt, static_cast<unsigned long>(iv < 0 ? 0 : iv));
      } else {
        snprintf(part, sizeof(part), fmt, static_cast<double>(v));
      }
    } else {
      const size_t n = seg.lit_len < sizeof(part) - 1 ? seg.lit_len : sizeof(part) - 1;
      memcpy(part, s->pool + seg.lit_off, n);
      part[n] = '\0';
    }
    const size_t n = strlen(part);
    if (used + n >= kTextBytes) break;
    memcpy(out + used, part, n + 1);
    used += n;
  }
}

void addText(Screen* s, const Elem& e, int buffer, float x, float y, float width, uint32_t color, float alpha) {
  if (alpha < 0.02f || !s->texts[buffer][0]) return;
  Cmd* c = addCmd(s);
  if (!c) return;
  c->kind = cText;
  c->opa = opaOf(alpha);
  c->color = color;
  c->a = coord(x);
  c->b = coord(y);
  c->c = fminf(fmaxf(width, 1.0f), 2000.0f);
  c->width = e.align;
  c->x1 = static_cast<uint16_t>(buffer);
  c->x2 = e.font;
  const lv_font_t* font = assist_screen_font(e.font);
  grow(s, c->a, c->b, c->a + c->c, c->b + (font ? lv_font_get_line_height(font) : e.font));
}

void runLets(Screen* s, const Elem& e) {
  for (int i = 0; i < e.let_count; ++i) {
    const Let& let = s->lets[e.let_first + i];
    s->slots[let.slot] = eval(s, let.expr);
  }
}

void buildPoly(Screen* s, const Elem& e) {
  Poly& poly = s->polys[e.poly];
  int16_t (*now)[2] = &s->points[poly.pts];
  // Older copies: a ring after the current points.
  if (poly.keep && s->frame_no % poly.frames == 0) {
    poly.head = static_cast<uint8_t>((poly.head + poly.keep - 1) % poly.keep);
    memcpy(&s->points[poly.pts + poly.count * (1 + poly.head)], now, sizeof(now[0]) * poly.count);
    if (poly.stored < poly.keep) ++poly.stored;
  }
  runLets(s, e);  // once, before the points
  for (int i = 0; i < poly.count; ++i) {
    float x, y;
    if (e.flags & kLiteral) {
      x = eval(s, poly.lit[i][0]);
      y = eval(s, poly.lit[i][1]);
    } else {
      s->slots[poly.i_slot] = static_cast<float>(i);
      s->slots[poly.u_slot] = static_cast<float>(i) / static_cast<float>(poly.count - 1);
      x = eval(s, e.p[kP_X]);
      y = eval(s, e.p[kP_Y]);
    }
    now[i][0] = static_cast<int16_t>(coord(x));
    now[i][1] = static_cast<int16_t>(coord(y));
  }
  const uint32_t color = colorOf(eval(s, e.p[kP_C], 0xFFFFFF));
  const float width = eval(s, e.p[kP_W], 1.0f);
  const bool round = (e.flags & kRoundSet) ? (e.flags & kRound) : true;
  // Oldest first, so newer copies draw over them.
  for (int k = poly.stored - 1; k >= 0; --k) {
    const int slot = (poly.head + k) % poly.keep;
    const float alpha = poly.alpha[k];
    if (alpha < 0.02f) continue;
    Cmd* c = addCmd(s);
    if (!c) return;
    c->kind = cPoly;
    c->flags = round ? cfRound : 0;
    c->width = poly.width;
    c->opa = opaOf(alpha);
    c->color = color;
    c->x1 = static_cast<uint16_t>(poly.pts + poly.count * (1 + slot));
    c->x2 = poly.count;
  }
  const float alpha = eval(s, e.p[kP_A], 1.0f);
  if (alpha >= 0.02f) {
    if (e.flags & kGlow) {
      Cmd* g = addCmd(s);
      if (!g) return;
      g->kind = cPoly;
      g->flags = round ? cfRound : 0;
      g->width = widthOf(width * 4.0f);
      g->opa = opaOf(alpha * 0.16f);
      g->color = color;
      g->x1 = poly.pts;
      g->x2 = poly.count;
    }
    Cmd* c = addCmd(s);
    if (!c) return;
    c->kind = cPoly;
    c->flags = round ? cfRound : 0;
    c->width = widthOf(width);
    c->opa = opaOf(alpha);
    c->color = color;
    c->x1 = poly.pts;
    c->x2 = poly.count;
  }
  // The box covers every copy kept.
  const float h = (width > poly.width ? width : poly.width) * 2.0f + 2.0f;
  for (int k = 0; k <= poly.stored; ++k) {
    const int base = poly.pts + (k == 0 ? 0 : poly.count * (1 + (poly.head + k - 1) % poly.keep));
    for (int i = 0; i < poly.count; ++i) {
      grow(s, s->points[base + i][0] - h, s->points[base + i][1] - h, s->points[base + i][0] + h,
           s->points[base + i][1] + h);
    }
  }
}

void buildOne(Screen* s, const Elem& e) {
  const bool glow = e.flags & kGlow;
  switch (e.kind) {
    case kLine: {
      const float w = eval(s, e.p[kL_W], 1.0f);
      const uint32_t color = colorOf(eval(s, e.p[kL_C], 0xFFFFFF));
      const float alpha = eval(s, e.p[kL_A], 1.0f);
      const float x1 = eval(s, e.p[kL_X1]), y1 = eval(s, e.p[kL_Y1]);
      const float x2 = eval(s, e.p[kL_X2]), y2 = eval(s, e.p[kL_Y2]);
      const float dash = eval(s, e.p[kL_DASH]), gap = eval(s, e.p[kL_GAP]);
      const uint16_t d = static_cast<uint16_t>(fmaxf(0.0f, fminf(dash, 500.0f)));
      const uint16_t g = static_cast<uint16_t>(fmaxf(0.0f, fminf(gap, 500.0f)));
      const bool round = e.flags & kRound;
      if (glow) addLine(s, x1, y1, x2, y2, w * 4.0f, color, alpha * 0.16f, round, d, g);
      addLine(s, x1, y1, x2, y2, w, color, alpha, round, d, g);
      break;
    }
    case kArc: {
      const float w = eval(s, e.p[kA_W], 1.0f);
      const uint32_t color = colorOf(eval(s, e.p[kA_C], 0xFFFFFF));
      const float alpha = eval(s, e.p[kA_A], 1.0f);
      const float x = eval(s, e.p[kA_X]), y = eval(s, e.p[kA_Y]), radius = eval(s, e.p[kA_R]);
      const float start = eval(s, e.p[kA_S], 0.0f), end = eval(s, e.p[kA_E], 360.0f);
      const bool round = e.flags & kRound;
      if (glow) addArc(s, x, y, radius, start, end, w * 4.0f, color, alpha * 0.16f, round);
      addArc(s, x, y, radius, start, end, w, color, alpha, round);
      break;
    }
    case kDisc: {
      const float alpha = eval(s, e.p[kD_A], 1.0f);
      const float radius = eval(s, e.p[kD_R]);
      if (alpha < 0.02f || radius < 1.0f) break;
      Cmd* c = addCmd(s);
      if (!c) break;
      c->kind = cDisc;
      c->opa = opaOf(alpha);
      c->color = colorOf(eval(s, e.p[kD_C], 0xFFFFFF));
      c->a = coord(eval(s, e.p[kD_X]));
      c->b = coord(eval(s, e.p[kD_Y]));
      c->c = roundf(fminf(radius, 4000.0f) * 2.0f) * 0.5f;
      // grad: the colour at the bottom, shading down from color at the top.
      if (e.p[kD_G].len) {
        c->flags = cfGrad;
        c->d = static_cast<float>(colorOf(eval(s, e.p[kD_G])));
      }
      grow(s, c->a - c->c, c->b - c->c, c->a + c->c, c->b + c->c);
      break;
    }
    case kRect: {
      const float alpha = eval(s, e.p[kR_A], 1.0f);
      if (alpha < 0.02f) break;
      Cmd* c = addCmd(s);
      if (!c) break;
      c->kind = cRect;
      c->opa = opaOf(alpha);
      c->color = colorOf(eval(s, e.p[kR_C], 0xFFFFFF));
      c->a = coord(eval(s, e.p[kR_X1]));
      c->b = coord(eval(s, e.p[kR_Y1]));
      c->c = coord(eval(s, e.p[kR_X2]));
      c->d = coord(eval(s, e.p[kR_Y2]));
      c->x1 = static_cast<uint16_t>(fmaxf(0.0f, fminf(eval(s, e.p[kR_RAD]), 2000.0f)));
      grow(s, c->a, c->b, c->c, c->d);
      break;
    }
    case kFrame: {
      const float w = eval(s, e.p[kF_W], 1.0f);
      const uint32_t color = colorOf(eval(s, e.p[kF_C], 0xFFFFFF));
      const float alpha = eval(s, e.p[kF_A], 1.0f);
      const float x1 = eval(s, e.p[kF_X1]), y1 = eval(s, e.p[kF_Y1]);
      const float x2 = eval(s, e.p[kF_X2]), y2 = eval(s, e.p[kF_Y2]);
      addLine(s, x1, y1, x2, y1, w, color, alpha, false);
      addLine(s, x2, y1, x2, y2, w, color, alpha, false);
      addLine(s, x2, y2, x1, y2, w, color, alpha, false);
      addLine(s, x1, y2, x1, y1, w, color, alpha, false);
      break;
    }
    case kText: {
      const int every = static_cast<int>(fmaxf(1.0f, eval(s, e.p[kT_EVERY], 1.0f)));
      if (!s->text_ready[e.text_first] || s->frame_no % every == 0) {
        formatText(s, e, s->texts[e.text_first]);
        s->text_ready[e.text_first] = true;
      }
      addText(s, e, e.text_first, eval(s, e.p[kT_X]), eval(s, e.p[kT_Y]), eval(s, e.p[kT_W], 200.0f),
              colorOf(eval(s, e.p[kT_C], 0xFFFFFF)), eval(s, e.p[kT_A], 1.0f));
      break;
    }
    case kLog: {
      // Rows were updated in the log pass; k is the row for color and alpha.
      const Log& log = s->logs[e.log];
      const float x = eval(s, e.p[kG_X]), y = eval(s, e.p[kG_Y]);
      const float lh = eval(s, e.p[kG_LH], 20.0f);
      const float width = eval(s, e.p[kG_W], 200.0f);
      for (int k = 0; k < log.count; ++k) {
        if (e.index_slot >= 0) s->slots[e.index_slot] = static_cast<float>(k);
        addText(s, e, e.text_first + k, x, y + k * lh, width, colorOf(eval(s, e.p[kG_C], 0xFFFFFF)),
                eval(s, e.p[kG_A], 1.0f));
      }
      break;
    }
    default: break;
  }
}

void buildRange(Screen* s, int first, int end, int depth) {
  for (int i = first; i < end;) {
    const Elem& e = s->elems[i];
    if (depth == 0) {
      // A new region starts with each top-level element.
      if (s->region_count < kMaxRegions) {
        s->region = s->region_count++;
        s->regions[s->region].first_cmd = s->cmd_count;
      }
    }
    if (!(e.on & (1u << s->shape))) {
      i = e.end;
      continue;
    }
    if (e.kind == kPoly) {
      if (!e.cond.len || eval(s, e.cond) != 0.0f) buildPoly(s, e);
      i = e.end;
      continue;
    }
    for (int k = 0; k < e.repeat; ++k) {
      if (e.index_slot >= 0 && e.kind != kLog) s->slots[e.index_slot] = static_cast<float>(k);
      runLets(s, e);
      if (e.cond.len && eval(s, e.cond) == 0.0f) continue;
      if (e.kind == kGroup) {
        if (depth < kMaxGroupDepth) buildRange(s, i + 1, e.end, depth + 1);
      } else {
        buildOne(s, e);
      }
    }
    i = e.end;
  }
}

// Logs push and pop their rows before the frame's shapes.
void stepLogs(Screen* s, bool start) {
  for (int i = 0; i < s->elem_count; ++i) {
    const Elem& e = s->elems[i];
    if (e.kind != kLog) continue;
    Log& log = s->logs[e.log];
    if (start) {
      log.count = 0;
      if (e.flags & kFill) {
        for (int k = 0; k < e.rows; ++k) formatText(s, e, s->texts[e.text_first + k]);
        log.count = e.rows;
      }
    } else {
      const int every = static_cast<int>(fmaxf(1.0f, eval(s, e.p[kG_EVERY], 1.0f)));
      if (s->frame_no % every == 0) {
        if (eval(s, e.p[kG_PUSH], 1.0f) != 0.0f) {
          if (log.count == e.rows) {
            for (int k = 0; k + 1 < e.rows; ++k) memcpy(s->texts[e.text_first + k], s->texts[e.text_first + k + 1], kTextBytes);
            --log.count;
          }
          formatText(s, e, s->texts[e.text_first + log.count]);
          ++log.count;
        } else if (eval(s, e.p[kG_POP], 0.0f) != 0.0f && log.count > 0) {
          for (int k = 0; k + 1 < e.rows; ++k) memcpy(s->texts[e.text_first + k], s->texts[e.text_first + k + 1], kTextBytes);
          --log.count;
        }
      }
    }
    if (log.count_slot >= 0) s->slots[log.count_slot] = log.count;
  }
}

uint32_t hashBytes(uint32_t h, const void* data, size_t n) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
  return h;
}

void setInputs(Screen* s, const Inputs& in) {
  s->slots[s->s_t] = in.t;
  s->slots[s->s_dt] = in.dt;
  s->slots[s->s_frame] = static_cast<float>(s->frame_no);
  s->slots[s->s_level] = in.level;
  s->slots[s->s_mode] = in.mode;
  s->slots[s->s_answering] = in.answering ? 1.0f : 0.0f;
  s->slots[s->s_r] = in.r;
  s->slots[s->s_cx] = in.cx;
  s->slots[s->s_cy] = in.cy;
  s->slots[s->s_w] = in.w;
  s->slots[s->s_h] = in.h;
  s->shape = shapeOf(in.w, in.h);
  s->slots[s->s_wide] = s->shape == kWide ? 1.0f : 0.0f;
  s->slots[s->s_square] = s->shape == kSquare ? 1.0f : 0.0f;
  s->slots[s->s_tall] = s->shape == kTall ? 1.0f : 0.0f;
}

}  // namespace

size_t screenSize() { return sizeof(Screen); }

Screen* init(void* memory) {
  Screen* s = new (memory) Screen;
  memset(static_cast<void*>(s), 0, sizeof(Screen));
  return s;
}

bool load(Screen* s, const char* text, size_t length, char* error, size_t error_size) {
  memset(static_cast<void*>(s), 0, sizeof(Screen));
  s->error = error;
  s->error_size = error_size;
  if (error && error_size) error[0] = '\0';
  if (!text || length == 0) return fail(s, 0, "the file is empty");
  if (length > kMaxFileBytes) {
    return fail(s, 0, "the file is %lu bytes (limit %lu)", static_cast<unsigned long>(length),
                static_cast<unsigned long>(kMaxFileBytes));
  }
  s->src = text;
  s->src_len = length;
  const char* builtins[] = {"t", "dt", "frame", "level", "mode", "answering", "r", "cx", "cy", "w", "h",
                            "wide", "square", "tall"};
  int* targets[] = {&s->s_t, &s->s_dt, &s->s_frame, &s->s_level, &s->s_mode, &s->s_answering,
                    &s->s_r, &s->s_cx, &s->s_cy, &s->s_w, &s->s_h, &s->s_wide, &s->s_square, &s->s_tall};
  for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); ++i) *targets[i] = addSlot(s, builtins[i], 0, 1);
  const bool ok = loadFile(s);
  // The file text and its DOM are not needed after this.
  s->src = nullptr;
  s->src_len = 0;
  s->error = nullptr;
  if (!ok) {
    s->elem_count = 0;
    s->var_count = 0;
  }
  return ok;
}

const Info& info(const Screen* s) { return s->info; }

uint32_t commandCount(const Screen* s) { return s->cmd_count; }

void reset(Screen* s, uint32_t seed) {
  s->rng = seed ? seed : 0x2545F491u;
  s->frame_no = 0;
  memset(s->regions, 0, sizeof(s->regions));
  s->region_count = 0;
  for (int i = 0; i < s->poly_count; ++i) {
    s->polys[i].stored = 0;
    s->polys[i].head = 0;
  }
  memset(s->text_ready, 0, sizeof(s->text_ready));
  for (int i = 0; i < s->var_count; ++i) {
    Var& v = s->vars[i];
    v.acc = 0.0f;
    s->slots[v.slot] = eval(s, v.expr);
  }
  stepLogs(s, true);
}

void frame(Screen* s, const Inputs& in) {
  ++s->frame_no;
  setInputs(s, in);
  for (int i = 0; i < s->var_count; ++i) {
    Var& v = s->vars[i];
    if (v.kind == 0) {
      s->slots[v.slot] = eval(s, v.alt[s->shape].len ? v.alt[s->shape] : v.expr);
      continue;
    }
    if (!v.next.len) continue;
    if (v.every.len) {
      v.acc += in.dt;
      if (v.acc < eval(s, v.every)) continue;
      v.acc = 0.0f;
    } else if (v.frames && s->frame_no % v.frames != 0) {
      continue;
    }
    s->slots[v.slot] = eval(s, v.next);
  }
  stepLogs(s, false);
  for (int i = 0; i < kMaxRegions; ++i) {
    Region& r = s->regions[i];
    r.last_box = r.box;
    r.last_valid = r.valid;
    r.last_hash = r.hash;
    r.valid = false;
    r.hash = 0;
  }
  s->cmd_count = 0;
  s->region_count = 0;
  s->region = 0;
  buildRange(s, 0, s->elem_count, 0);
  for (int i = 0; i < s->region_count; ++i) {
    Region& r = s->regions[i];
    r.end_cmd = i + 1 < s->region_count ? s->regions[i + 1].first_cmd : s->cmd_count;
    uint32_t h = 2166136261u;
    h = hashBytes(h, &s->cmds[r.first_cmd], sizeof(Cmd) * (r.end_cmd - r.first_cmd));
    for (uint32_t k = r.first_cmd; k < r.end_cmd; ++k) {
      const Cmd& c = s->cmds[k];
      if (c.kind == cPoly) h = hashBytes(h, &s->points[c.x1], sizeof(s->points[0]) * c.x2);
      if (c.kind == cText) h = hashBytes(h, s->texts[c.x1], strlen(s->texts[c.x1]));
    }
    r.hash = h;
  }
}

int dirty(const Screen* s, lv_area_t* areas, int max) {
  if (max <= 0) return 0;
  int n = 0;
  for (int i = 0; i < kMaxRegions; ++i) {
    const Region& r = s->regions[i];
    if (!r.valid && !r.last_valid) continue;
    if (s->frame_no > 1 && r.valid == r.last_valid && r.hash == r.last_hash) continue;
    lv_area_t a = r.valid ? r.box : r.last_box;
    if (r.valid && r.last_valid) joinArea(&a, &a, &r.last_box);
    // Join it with an area it overlaps, or with the last one when full.
    bool joined = false;
    for (int k = 0; k < n && !joined; ++k) {
      lv_area_t both;
      joinArea(&both, &areas[k], &a);
      if (lv_area_get_size(&both) <= lv_area_get_size(&areas[k]) + lv_area_get_size(&a)) {
        areas[k] = both;
        joined = true;
      }
    }
    if (joined) continue;
    if (n < max) {
      areas[n++] = a;
    } else {
      joinArea(&areas[n - 1], &areas[n - 1], &a);
    }
  }
  return n;
}

void draw(const Screen* s, lv_layer_t* layer, int32_t ox, int32_t oy) {
  for (uint32_t i = 0; i < s->cmd_count; ++i) {
    const Cmd& c = s->cmds[i];
    switch (c.kind) {
      case cLine:
      case cPoly: {
        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.base.layer = layer;
        dsc.color = lv_color_hex(c.color);
        dsc.opa = c.opa;
        dsc.width = c.width;
        dsc.round_start = dsc.round_end = (c.flags & cfRound) != 0;
        if (c.kind == cLine) {
          if (c.x1 && c.x2) {
            dsc.dash_width = c.x1;
            dsc.dash_gap = c.x2;
          }
          dsc.p1.x = static_cast<lv_value_precise_t>(ox + c.a);
          dsc.p1.y = static_cast<lv_value_precise_t>(oy + c.b);
          dsc.p2.x = static_cast<lv_value_precise_t>(ox + c.c);
          dsc.p2.y = static_cast<lv_value_precise_t>(oy + c.d);
          lv_draw_line(layer, &dsc);
        } else {
          const int16_t (*pts)[2] = &s->points[c.x1];
          for (int k = 1; k < c.x2; ++k) {
            dsc.p1.x = static_cast<lv_value_precise_t>(ox + pts[k - 1][0]);
            dsc.p1.y = static_cast<lv_value_precise_t>(oy + pts[k - 1][1]);
            dsc.p2.x = static_cast<lv_value_precise_t>(ox + pts[k][0]);
            dsc.p2.y = static_cast<lv_value_precise_t>(oy + pts[k][1]);
            lv_draw_line(layer, &dsc);
          }
        }
        break;
      }
      case cArc: {
        lv_draw_arc_dsc_t dsc;
        lv_draw_arc_dsc_init(&dsc);
        dsc.base.layer = layer;
        dsc.color = lv_color_hex(c.color);
        dsc.opa = c.opa;
        dsc.width = c.width;
        dsc.rounded = (c.flags & cfRound) != 0;
        dsc.center.x = ox + static_cast<int32_t>(c.a);
        dsc.center.y = oy + static_cast<int32_t>(c.b);
        dsc.radius = static_cast<uint16_t>(c.c);
        if (c.e - c.d >= 360.0f) {
          dsc.start_angle = 0;
          dsc.end_angle = 360;
        } else {
          int32_t a0 = static_cast<int32_t>(c.d) % 360, a1 = static_cast<int32_t>(c.e) % 360;
          dsc.start_angle = a0 < 0 ? a0 + 360 : a0;
          dsc.end_angle = a1 < 0 ? a1 + 360 : a1;
        }
        lv_draw_arc(layer, &dsc);
        break;
      }
      case cDisc:
      case cRect: {
        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.base.layer = layer;
        dsc.bg_color = lv_color_hex(c.color);
        dsc.bg_opa = c.opa;
        lv_area_t area;
        if (c.kind == cDisc) {
          dsc.radius = LV_RADIUS_CIRCLE;
          area.x1 = ox + static_cast<int32_t>(c.a - c.c);
          area.y1 = oy + static_cast<int32_t>(c.b - c.c);
          area.x2 = ox + static_cast<int32_t>(c.a + c.c);
          area.y2 = oy + static_cast<int32_t>(c.b + c.c);
          if (c.flags & cfGrad) {
            const lv_color_t stops[2] = {dsc.bg_color, lv_color_hex(static_cast<uint32_t>(c.d))};
            lv_grad_init_stops(&dsc.bg_grad, stops, nullptr, nullptr, 2);
            lv_grad_vertical_init(&dsc.bg_grad);
          }
        } else {
          dsc.radius = c.x1;
          area.x1 = ox + static_cast<int32_t>(fminf(c.a, c.c));
          area.y1 = oy + static_cast<int32_t>(fminf(c.b, c.d));
          area.x2 = ox + static_cast<int32_t>(fmaxf(c.a, c.c));
          area.y2 = oy + static_cast<int32_t>(fmaxf(c.b, c.d));
        }
        lv_draw_rect(layer, &dsc, &area);
        break;
      }
      case cText: {
        const lv_font_t* font = assist_screen_font(c.x2);
        if (!font) break;
        lv_draw_label_dsc_t dsc;
        lv_draw_label_dsc_init(&dsc);
        dsc.base.layer = layer;
        dsc.color = lv_color_hex(c.color);
        dsc.opa = c.opa;
        dsc.font = font;
        dsc.align = static_cast<lv_text_align_t>(c.width);
        dsc.text = s->texts[c.x1];
        dsc.text_local = 1;
        lv_area_t area;
        area.x1 = ox + static_cast<int32_t>(c.a);
        area.y1 = oy + static_cast<int32_t>(c.b);
        area.x2 = area.x1 + static_cast<int32_t>(c.c) - 1;
        area.y2 = area.y1 + lv_font_get_line_height(font) - 1;
        lv_draw_label(layer, &dsc, &area);
        break;
      }
      default: break;
    }
  }
}

}  // namespace assist_screen
