#include <cilk/os_label.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <ostream>

// Label checks. These sit on spawn/sync paths, not per-access, so they are
// always compiled in: a label that overran data[] would turn into wrong race
// verdicts rather than a crash.
#define CILKPRACE_LABEL_CHECK(cond, ...)                                       \
  do {                                                                         \
    if (__builtin_expect(!(cond), 0)) {                                        \
      fprintf(stderr, "cilkprace: label overflow: ");                          \
      fprintf(stderr, __VA_ARGS__);                                            \
      fprintf(stderr, "\n");                                                   \
      abort();                                                                 \
    }                                                                          \
  } while (0)

// Move the words below end_idx's word into a new chunk, so the window again
// has room for a spawn's scan (see prepare_spawn). Runs about once per 50
// levels of growth, and never for labels that stay within the window.
void os_label::spill() {
  unsigned k = end_idx / 64; // at most CILKPRACE_ANCHOR_WORDS - 1
  CILKPRACE_LABEL_CHECK(base <= UINT32_MAX - 2 * window_bits,
                        "label reached %u bits", (unsigned)end());
  os_label_chunk *c = (os_label_chunk *)malloc(sizeof(os_label_chunk));
  if (!c) {
    fprintf(stderr, "cilkprace: out of memory for a label chunk\n");
    abort();
  }
  c->parent = anchor;
  c->base = base;
  c->nwords = k;
  memcpy(c->bits, data, k * sizeof(uint64_t));
  memmove(data, data + k, (CILKPRACE_ANCHOR_WORDS - k) * sizeof(uint64_t));
  memset(data + CILKPRACE_ANCHOR_WORDS - k, 0, k * sizeof(uint64_t));
  anchor = c;
  base += k * 64;
  end_idx -= k * 64;
}

// Move the window back until it holds bit idx, pulling chunks back in, for a
// sync whose frame's spawns spilled its start. The top words fall off, which
// is fine: restore_on_sync clears everything above its counter.
void os_label::rewind(uint32_t idx) {
  while (base > idx && anchor) {
    const os_label_chunk *c = anchor;
    unsigned k = c->nwords;
    memmove(data + k, data, (CILKPRACE_ANCHOR_WORDS - k) * sizeof(uint64_t));
    memcpy(data, c->bits, k * sizeof(uint64_t));
    base = c->base;
    end_idx += k * 64;
    anchor = c->parent;
  }
}

// Word w (bits [64w, 64w + 64)) of the whole label.
uint64_t os_label::word_at(uint32_t w) const {
  if (w >= base / 64)
    return w - base / 64 < CILKPRACE_ANCHOR_WORDS ? data[w - base / 64] : 0;
  for (const os_label_chunk *c = anchor; c; c = c->parent)
    if (w >= c->base / 64)
      return w - c->base / 64 < c->nwords ? c->bits[w - c->base / 64] : 0;
  return 0;
}

namespace {
// Reads a label's words from the top down, following its chunk chain.
struct word_cursor {
  const os_label &l;
  const os_label_chunk *c; // chunk holding the last word read, if any
  bool in_chunk = false;
  explicit word_cursor(const os_label &l) : l(l), c(l.anchor) {}
  uint64_t at(uint32_t w) { // w must not increase between calls
    if (w >= l.base / 64) {
      in_chunk = false;
      return w - l.base / 64 < CILKPRACE_ANCHOR_WORDS ? l.data[w - l.base / 64] : 0;
    }
    while (c && w < c->base / 64)
      c = c->parent;
    in_chunk = c != nullptr;
    return c && w - c->base / 64 < c->nwords ? c->bits[w - c->base / 64] : 0;
  }
};
} // namespace

// lca of labels with different anchors: at least one has spilled, so their
// windows may start at different places. Compare whole-label words from the
// top of the shorter label down, stopping once both sides are reading the same
// chunk -- everything from there down is shared. The lowest differing word
// holds the first difference.
unsigned os_label::lca_far(const os_label &other) const {
  unsigned last_idx = other.end() < end() ? other.end() : end();
  word_cursor a(*this), b(other);
  unsigned first_diff = last_idx;
  for (uint32_t w = (last_idx + 63) / 64; w-- > 0;) {
    uint64_t x = a.at(w), y = b.at(w);
    if (a.in_chunk && b.in_chunk && a.c == b.c)
      break;
    uint64_t diff = x ^ y;
    if (w == last_idx / 64)
      diff &= (1ull << (last_idx % 64)) - 1;
    if (diff)
      first_diff = w * 64 + __builtin_ctzll(diff);
  }
  return first_diff;
}

void os_label::append_left_child() {
  int scan_low_offset_bytes = end_idx / 8;

  if (scan_low_offset_bytes > scan_max_low_offset_bytes) {
    scan_low_offset_bytes = scan_max_low_offset_bytes;
  }

  uint64_t *scan_low_addr = (uint64_t *)((uint8_t *)data + scan_low_offset_bytes);

  uint64_t scan_val = *scan_low_addr;
  uint64_t scan_low_idx = end_idx - scan_low_offset_bytes * 8;
  // prepare_spawn leaves room, so these only guard against a caller that
  // skipped it: a full window clamps the scan, and without the first check the
  // shift below is out of range and end_idx wraps back into the last word,
  // aliasing an ancestor's label instead of failing.
  CILKPRACE_LABEL_CHECK(scan_low_idx < 64, "spawn at %u bits", (unsigned)end());

  uint64_t scan_val_capped = scan_val | 1ull << scan_low_idx;
  int high_set_idx = 63 - __builtin_clzll(scan_val_capped);
  end_idx = ((high_set_idx + 2) | 3) + 1 + scan_low_offset_bytes * 8;

  CILKPRACE_LABEL_CHECK(end_idx <= window_bits, "spawn advanced label to %u bits",
                        (unsigned)end());
}

void os_label::append_right_child() {
  append_left_child();
  assert(end_idx % 4 == 0);
  assert(end_idx > 0);
  uint64_t p_idx = end_idx - 1;
  data[p_idx / 64ull] |= 1ull << (p_idx % 64ull);
}

void os_label::restore_on_sync(uint32_t restore_idx) {
  // The frame's spawns may have spilled its own start out of the window.
  if (restore_idx < base)
    rewind(restore_idx);
  unsigned rel_restore = restore_idx - base;
  CILKPRACE_LABEL_CHECK(rel_restore < window_bits, "sync restore point %u bits",
                        (unsigned)restore_idx);
  int scan_low_offset_bytes = rel_restore / 8;

  if (scan_low_offset_bytes > scan_max_low_offset_bytes) {
    scan_low_offset_bytes = scan_max_low_offset_bytes;
  }

  uint64_t *scan_low_addr = (uint64_t *)((uint8_t *)data + scan_low_offset_bytes);
  uint64_t scan_low_idx = rel_restore - scan_low_offset_bytes * 8;
  CILKPRACE_LABEL_CHECK(scan_low_idx < 64,
                        "sync scan offset %llu bits", (unsigned long long)scan_low_idx);
  assert(scan_low_idx % 4 == 0);

  uint64_t scan_val = *scan_low_addr;
  uint64_t p_mask = 0x8888888888888888ull << scan_low_idx;

  // Clear continuations
  uint64_t conts = scan_val & p_mask;
  scan_val &= ~conts & (conts - 1);

  // Increment s value.
  uint64_t carries = scan_val & (p_mask >> 1ull);
  scan_val |= p_mask;
  scan_val += 1ull << scan_low_idx;
  scan_val &= ~p_mask;
  scan_val |= carries;

  *scan_low_addr = scan_val;

  uint8_t *clear_low_addr = (uint8_t *)(scan_low_addr + 1);

  unsigned clear_high_offset_bytes = end_idx / 8 + sizeof(uint64_t);
  if (clear_high_offset_bytes > sizeof(data)) {
    clear_high_offset_bytes = sizeof(data);
  }
  uint8_t *clear_high_addr = (uint8_t *)data + clear_high_offset_bytes;
  if (clear_low_addr < clear_high_addr)
    memset(clear_low_addr, 0, clear_high_addr - clear_low_addr);

  end_idx = rel_restore;
}

uint32_t os_label::get_restore_point() const {
  return end();
}

std::vector<uint8_t> os_label::to_vector() const {
  std::vector<uint8_t> vec;
  auto get_nibble = [&](size_t idx) -> uint8_t {
    return (word_at(idx / 16) >> ((idx % 16) * 4)) & 0xF;
  };

  size_t curr_nibble = 0;
  uint8_t n0 = get_nibble(curr_nibble);
  uint8_t s0 = n0 & 0x7;
  vec.push_back(s0 * 2);

  while (true) {
    uint8_t n = get_nibble(curr_nibble);
    bool has_carry = (n & 0x4) != 0;
    size_t p_nibble = has_carry ? (curr_nibble + 1) : curr_nibble;
    size_t p_bit_idx = p_nibble * 4 + 3;
    size_t next_level_nibble = p_nibble + 1;

    if (p_bit_idx >= end()) {
      break;
    }

    uint8_t next_n = get_nibble(next_level_nibble);
    uint8_t s_curr = next_n & 0x7;
    if (s_curr > 0) {
      vec.push_back(s_curr * 2 + 1);
    } else {
      bool is_continuation = (get_nibble(p_nibble) & 0x8) != 0;
      vec.push_back(is_continuation ? 0 : 1);
    }

    curr_nibble = next_level_nibble;
  }

  return vec;
}

#ifdef ENABLE_LABEL_PRINTING
std::ostream &operator<<(std::ostream &os, const os_label &l) {
  for (uint32_t i = 0; i < (l.end() + 63) / 64; i++) {
    os << std::hex << std::setw(16) << std::setfill('0') << l.word_at(i) << " ";
  }
  os << ": " << std::dec << l.end();
  return os;
}
#endif
