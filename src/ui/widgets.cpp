#include "widgets.h"

#include "text/utf8.h"

namespace ui {

static int P(int v) { return gfx::px(v); }

int margin() { return P(10); }
int pad() { return P(8); }
int titleH() { return gfx::lineH(Font::Ui) + P(14); }
int footH() { return gfx::lineH(Font::Small) + P(10); }
int bodyTop() { return titleH() + 2; }
int bodyBottom() { return gfx::H() - footH(); }

void frame(const String &title, const String &hint, const String &right) {
  const int W = gfx::W(), H = gfx::H(), M = margin(), th = titleH();
  const int base = th - P(10);
  int rw = right.length() ? gfx::textW(right, Font::Small) + P(8) : 0;
  gfx::text(M, base, gfx::fit(title, Font::Ui, W - 2 * M - rw), Font::Ui);
  if (rw) gfx::textRight(W - M, base, right, Font::Small);
  gfx::d().fillRect(M, th - P(3), W - 2 * M, P(2), EPD_BLACK);
  if (hint.length()) {
    gfx::hline(M, H - footH(), W - 2 * M);
    gfx::textCenter(W / 2, H - P(6), gfx::fit(hint, Font::Small, W - 2 * M), Font::Small);
  }
}

// ---------------------------------------------------------------------
// 折行
// ---------------------------------------------------------------------
static bool wordChar(uint32_t c) { return c < 0x80 && (isalnum(c) || strchr("._-:/@%#+=&?~'", (int)c)); }
// 不能放在行首的标点（收尾的）
static bool noStart(uint32_t c) {
  static const uint32_t L[] = {0xFF0C, 0x3002, 0x3001, 0xFF1B, 0xFF1A, 0xFF1F, 0xFF01, 0xFF09, 0x300D, 0x300F, 0x300B,
                               0x3009, 0x201D, 0x2019, 0x2026, 0x2014, ',', '.', ';', ':', '?', '!', ')', ']'};
  for (uint32_t x : L)
    if (c == x) return true;
  return false;
}
// 不能放在行尾的标点（开头的）
static bool noEnd(uint32_t c) {
  static const uint32_t L[] = {0xFF08, 0x300C, 0x300E, 0x300A, 0x3008, 0x201C, 0x2018, '(', '['};
  for (uint32_t x : L)
    if (c == x) return true;
  return false;
}

struct Unit {
  String s;
  int w;
  bool space;
  uint32_t first, last;
};

static std::vector<Unit> units(const String &s, Font f) {
  std::vector<Unit> out;
  const char *p = s.c_str();
  uint32_t n = s.length(), i = 0;
  while (i < n) {
    uint32_t st = i, cp = utf8Next(p, n, i), last = cp;
    if (wordChar(cp)) {
      while (i < n) {
        uint32_t j = i, c2 = utf8Next(p, n, j);
        if (!wordChar(c2)) break;
        i = j;
        last = c2;
      }
    }
    Unit u;
    u.s = s.substring(st, i);
    u.w = gfx::textW(u.s, f);
    u.space = cp == ' ' || cp == 0x3000;
    u.first = cp;
    u.last = last;
    out.push_back(u);
  }
  return out;
}

static std::vector<std::vector<Unit>> lay(const std::vector<Unit> &us, int maxW) {
  std::vector<std::vector<Unit>> lines(1);
  int w = 0;
  for (const Unit &u : us) {
    auto &cur = lines.back();
    if (cur.empty() && u.space) continue;  // 行首不放空格
    if (!cur.empty() && w + u.w > maxW && !u.space) {
      std::vector<Unit> carry;
      if (noStart(u.first) && cur.size() >= 2) {  // 收尾标点不放行首：把上一个字一起带下去
        carry.push_back(cur.back());
        cur.pop_back();
      }
      while (cur.size() >= 2 && noEnd(cur.back().last)) {  // 开头的括号、引号不留在行尾
        carry.insert(carry.begin(), cur.back());
        cur.pop_back();
      }
      while (!cur.empty() && cur.back().space) cur.pop_back();
      lines.push_back(carry);
      w = 0;
      for (const Unit &c : carry) w += c.w;
    }
    lines.back().push_back(u);
    w += u.w;
  }
  while (!lines.empty() && lines.back().empty()) lines.pop_back();
  return lines;
}

static String join(const std::vector<Unit> &l) {
  String s;
  for (const Unit &u : l) s += u.s;
  s.trim();
  return s;
}

std::vector<String> wrap(const String &s, Font f, int maxW, int maxLines, bool balance) {
  std::vector<String> out;
  if (!s.length() || maxLines < 1) return out;
  auto us = units(s, f);
  auto lines = lay(us, maxW);
  if (balance && lines.size() > 1 && (int)lines.size() <= maxLines) {
    // 找最窄的宽度，行数不变：各行就差不多一样长
    int lo = maxW / (int)lines.size(), hi = maxW;
    while (lo < hi) {
      int mid = (lo + hi) / 2;
      if (lay(us, mid).size() == lines.size()) hi = mid;
      else lo = mid + 1;
    }
    lines = lay(us, hi);
  }
  for (size_t k = 0; k < lines.size(); k++) {
    if ((int)out.size() == maxLines - 1 && k + 1 < lines.size()) {  // 放不下了：剩下的拼成一行截断
      String rest;
      for (size_t j = k; j < lines.size(); j++) rest += join(lines[j]);
      out.push_back(gfx::fit(rest, f, maxW));
      break;
    }
    out.push_back(join(lines[k]));
  }
  return out;
}

int paragraph(int cx, int y, const String &s, Font f, int maxW, int maxLines) {
  const int step = gfx::lineH(f) + P(4);
  for (const String &l : wrap(s, f, maxW, maxLines, true)) {
    gfx::textCenter(cx, y, l, f);
    y += step;
  }
  return y;
}

// ---------------------------------------------------------------------
BoxRect box(const String &title, const String &body, const String &hint) {
  const int W = gfx::W(), H = gfx::H();
  const int inner = W - 2 * margin() - 2 * P(16);
  auto t = wrap(title, Font::Ui, inner, 2, true);
  auto b = wrap(body, Font::Small, inner, 4, true);
  String h = hint.length() ? gfx::fit(hint, Font::Small, inner) : String();
  int tw = 0;
  for (auto &l : t) tw = max(tw, gfx::textW(l, Font::Ui));
  for (auto &l : b) tw = max(tw, gfx::textW(l, Font::Small));
  if (h.length()) tw = max(tw, gfx::textW(h, Font::Small));
  const int lu = gfx::lineH(Font::Ui), ls = gfx::lineH(Font::Small);
  const int gapU = P(5), gapS = P(4), gapTB = P(6), gapH = P(8), padV = P(15);
  const int nt = t.size(), nb = b.size();
  // 内容高度：标题几行、正文几行、提示一行，中间各留一点空
  int content = 0;
  if (nt) content += nt * lu + (nt - 1) * gapU;
  if (nb) content += (nt ? gapTB : 0) + nb * ls + (nb - 1) * gapS;
  if (h.length()) content += (nt || nb ? gapH : 0) + ls;
  BoxRect r;
  r.w = constrain(tw + P(48), P(160), W - 2 * margin());
  r.h = content + 2 * padV;
  r.x = (W - r.w) / 2;
  r.y = (H - r.h) / 2;
  gfx::d().fillRect(r.x, r.y, r.w, r.h, EPD_WHITE);
  gfx::d().drawRect(r.x, r.y, r.w, r.h, EPD_BLACK);
  gfx::d().drawRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, EPD_BLACK);
  int top = r.y + padV;  // 下一行字的顶
  for (int k = 0; k < nt; k++) {
    gfx::textCenter(W / 2, top + gfx::ascent(Font::Ui), t[k], Font::Ui);
    top += lu + (k + 1 < nt ? gapU : 0);
  }
  if (nb && nt) top += gapTB;
  for (int k = 0; k < nb; k++) {
    gfx::textCenter(W / 2, top + gfx::ascent(Font::Small), b[k], Font::Small);
    top += ls + (k + 1 < nb ? gapS : 0);
  }
  if (h.length()) {
    if (nt || nb) top += gapH;
    gfx::textCenter(W / 2, top + gfx::ascent(Font::Small), h, Font::Small);
  }
  return r;
}

}  // namespace ui
