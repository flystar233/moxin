#include "edge_drm.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

// 算法照抄 edge-tts 的 drm.py（2026 版）：
//   data = "<Windows 文件时间刻度>" + 信任令牌
//   SHA   = 改了初始状态的 SHA-256（不是标准 SHA-256，必须逐位照抄）
//   CS64  = 以 SHA 前 8 字节为密钥的一串 32 位混合
//   令牌  = CS64(16 位十六进制) + "-" + SHA(64 位十六进制)
namespace tts {
namespace {

constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr uint32_t INITIAL[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

// 预置的初始状态 / 前 16 个字的调度常量（把「56 个 0 字节 + 数据前 7 字节」那一块
// 的 0～13 轮提前算掉了；常数被改过，和标准 SHA-256 结果不同）
constexpr uint32_t STATE0[8] = {0xFD9FBEA2, 0x91243CE7, 0xBE499094, 0xCD2E1E0A,
                                0x6C006F49, 0x04BD35AB, 0xFEB0A7E4, 0x03EFFBD2};
constexpr uint32_t SCHED[14] = {0xEB24BF9E, 0x1558691F, 0xA0C09EE2, 0x0CFA32BA, 0xA622EE8C, 0xD1DD9491,
                                0xC0DD25A0, 0xF9E70AB6, 0xF0C31573, 0x7058CE4E, 0x7188E606, 0x00E5AB33,
                                0xD258B060, 0x72766564};
constexpr uint32_t ADJ = 0xC9DC06A7;
constexpr uint32_t SIGMA0_ADJ = 0x059C0B80;
constexpr uint32_t SIGMA1_ADJ = 0x000B92C0;

constexpr uint32_t COEF[14] = {0xE82E91B1, 0x44DE24C6, 0x3BEB65BC, 0xC89C5933, 0x8D1EC211, 0x314EF5D5, 0xE92491B1,
                               0xF0A5B9D5, 0x06064CF3, 0xDA79FA60, 0xBED20560, 0x89F9C9F8, 0x35FA38C6, 0xCB49E3C6};
constexpr uint32_t OFF[14] = {0xB32700D0, 0x88DFF51D, 0x29514038, 0xB328CBDF, 0xD405B4D5, 0x26E55F3C, 0x607801C6,
                              0x82B2C1FB, 0xFA51A3DA, 0x207CC464, 0x3C3C74F3, 0xD2D14B61, 0xBB04A2C1, 0xC5626167};
constexpr uint32_t CS_FACTOR = 0x82893933;
constexpr uint32_t CS_ADJ0 = 0xF2F90B1A;
constexpr uint32_t CS_ADJ1 = 0x0B1A0000;

// 改过一个轮常数的 K（只在预置那一块用）
uint32_t blockK(int i) { return i == 14 ? ADJ : K[i]; }

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t rol16(uint32_t x) { return (x << 16) | (x >> 16); }
inline uint32_t sigma0(uint32_t v) { return (v >> 7) ^ (v << 25) ^ (v >> 18) ^ (v << 14) ^ (v >> 3); }
inline uint32_t sigma1(uint32_t v) { return (v >> 17) ^ (v << 15) ^ (v >> 19) ^ (v << 13) ^ (v >> 10); }

inline uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
inline uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void expand(uint32_t *w, int from) {
  for (int i = from; i < 64; i++) w[i] = sigma1(w[i - 2]) + w[i - 7] + sigma0(w[i - 15]) + w[i - 16];
}

void rounds(uint32_t st[8], const uint32_t *w, int first, bool blockConst) {
  uint32_t a = st[0], b = st[1], c = st[2], d = st[3], e = st[4], f = st[5], g = st[6], h = st[7];
  for (int i = first; i < 64; i++) {
    uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + (g ^ (e & (f ^ g))) +
                  (blockConst ? blockK(i) : K[i]) + w[i];
    uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) | (c & (a | b)));
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  st[0] = a; st[1] = b; st[2] = c; st[3] = d;
  st[4] = e; st[5] = f; st[6] = g; st[7] = h;
}

// 预置状态：数据前 7 字节放进第 14、15 个字，从第 14 轮接着算
void shaInit(const uint8_t *d, uint32_t st[8]) {
  uint32_t w[64];
  memset(w, 0, sizeof(w));
  w[14] = ((uint32_t)d[0] << 16) | ((uint32_t)d[1] << 8) | d[2];
  w[15] = ((uint32_t)d[3] << 24) | ((uint32_t)d[4] << 16) | ((uint32_t)d[5] << 8) | d[6];
  for (int i = 16; i < 30; i++) {
    uint32_t v = SCHED[i - 16];
    if (i - 2 >= 14) v += (i == 16) ? (sigma1(w[i - 2]) ^ SIGMA1_ADJ) : sigma1(w[i - 2]);
    if (i - 7 >= 14) v += w[i - 7];
    if (i - 15 >= 14) v += (i == 29) ? (sigma0(w[i - 15]) ^ SIGMA0_ADJ) : sigma0(w[i - 15]);
    w[i] = v;
  }
  w[30] = sigma1(w[28]) + w[23] + sigma0(w[15]) + w[14] + ADJ - K[14];
  expand(w, 31);

  uint32_t work[8];
  memcpy(work, STATE0, sizeof(work));
  rounds(work, w, 14, true);
  for (int i = 0; i < 8; i++) st[i] = INITIAL[i] + work[i];
}

void shaDigest(const uint8_t *d, size_t n, uint8_t out[32]) {
  uint32_t st[8];
  shaInit(d, st);

  uint8_t buf[256];
  size_t blen = n - 7;
  memcpy(buf, d + 7, blen);
  buf[blen++] = 0x80;
  size_t length = 57 + n;
  size_t zeros = (55 - length) % 64;
  memset(buf + blen, 0, zeros);
  blen += zeros;
  uint64_t bits = (uint64_t)length * 8;
  for (int i = 0; i < 8; i++) buf[blen++] = (uint8_t)(bits >> (56 - 8 * i));

  for (size_t off = 0; off < blen; off += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = be32(buf + off + 4 * i);
    expand(w, 16);
    uint32_t work[8];
    memcpy(work, st, sizeof(work));
    rounds(work, w, 0, false);
    for (int i = 0; i < 8; i++) st[i] += work[i];
  }
  for (int i = 0; i < 8; i++) {
    out[4 * i + 0] = (uint8_t)(st[i] >> 24);
    out[4 * i + 1] = (uint8_t)(st[i] >> 16);
    out[4 * i + 2] = (uint8_t)(st[i] >> 8);
    out[4 * i + 3] = (uint8_t)st[i];
  }
}

// CS64：以 SHA 摘要前 8 字节为密钥的一串 32 位混合
void cs64(const uint8_t *d, size_t n, const uint8_t digest[32], uint32_t out[2]) {
  uint32_t keyA = le32(digest) | 1u;
  uint32_t keyB = le32(digest + 4) | 1u;
  uint32_t factors[2] = {keyA * CS_FACTOR, keyB * CS_FACTOR};

  uint32_t terms[64], offs[64];
  int nt = 0;
  for (int i = 0; i < 14; i++) {
    terms[nt] = COEF[i] * factors[i & 1];
    offs[nt] = OFF[i];
    nt++;
  }

  uint8_t buf[256];
  buf[0] = 0;  // 前面补一个 0 字节
  memcpy(buf + 1, d, n);
  size_t blen = 1 + n;
  while (blen % 4) buf[blen++] = 0;
  size_t nw = blen / 4;
  for (size_t i = 0; i < nw; i++) {
    uint32_t word = le32(buf + 4 * i);
    uint32_t term = word * ((i & 1) ? keyB : keyA);
    uint32_t off = rol16(word) * ((i & 1) ? 0xC31713DBu : 0xEF0569FBu);
    if (i == 0) {
      term += CS_ADJ0 * factors[0];
      off += CS_ADJ1;
    }
    terms[nt] = term;
    offs[nt] = off;
    nt++;
  }
  if (nw & 1) {  // 字数是奇数时补一个 0 字
    terms[nt] = 0;
    offs[nt] = 0;
    nt++;
  }

  uint32_t swapAcc = 0, swapLast = 0, state = 0, acc = 0;
  for (int i = 0; i < nt; i += 2) {
    uint32_t x = terms[i], y = terms[i + 1];

    uint32_t t = rol16(offs[i] + x);
    uint32_t q = t * 0x76F50F34u;
    swapAcc += rol16(q) * 0xC3EFEA97u + q;

    t = rol16(offs[i + 1] + y);
    q = t * 0x1881CE3Cu;
    swapLast = rol16(q) * 0x35BD1EC9u + q;
    swapAcc += swapLast;

    state = state * keyA + x;
    state = rol16(state) * 0xCF98B111u;
    state = rol16(state) * 0x87085B9Fu;
    state = rol16(state) * 0x12CEB96Du;
    state = rol16(state) * 0x257E1D83u;
    acc += state;

    t = state * keyB + y;
    t = rol16(t) * 0xA27416F5u;
    t = rol16(t) * 0xD38396FFu;
    state = rol16(rol16(t) * 0x7C932B89u) * 0xBFA49F69u;
    acc += state;
  }
  out[0] = swapLast ^ state;
  out[1] = swapAcc ^ acc;
}

}  // namespace

String edgeSecMsGec(uint64_t unixSec) {
  // 时间按 5 分钟一档；乘法用 double 是为了和 Python 的浮点结果逐位一致
  double ticks = (double)unixSec + 11644473600.0;
  ticks -= fmod(ticks, 300.0);
  ticks *= 1e7;
  char num[32];
  snprintf(num, sizeof(num), "%.0f", ticks);

  String data = String(num) + TTS_EDGE_TRUSTED_TOKEN;
  uint8_t digest[32];
  shaDigest((const uint8_t *)data.c_str(), data.length(), digest);
  uint32_t cs[2];
  cs64((const uint8_t *)data.c_str(), data.length(), digest, cs);

  char out[17];
  snprintf(out, sizeof(out), "%02X%02X%02X%02X%02X%02X%02X%02X", cs[0] & 0xFF, (cs[0] >> 8) & 0xFF,
           (cs[0] >> 16) & 0xFF, (cs[0] >> 24) & 0xFF, cs[1] & 0xFF, (cs[1] >> 8) & 0xFF, (cs[1] >> 16) & 0xFF,
           (cs[1] >> 24) & 0xFF);

  String token = String(out) + "-";
  for (int i = 0; i < 32; i++) {
    char b[3];
    snprintf(b, sizeof(b), "%02X", digest[i]);
    token += b;
  }
  return token;
}

}  // namespace tts
