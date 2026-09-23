#ifndef _OS_LABEL_LEB8_SINGLE_H
#define _OS_LABEL_LEB8_SINGLE_H

#pragma GCC visibility push(default)

#include "cilkprace_ablation.h"

// Width of the label, in 64-bit words. Each nesting level costs 4 bits, so W
// words allow 16*W levels of spawn nesting. The runtime aborts with a clear
// message if a program exceeds it (see append_left_child), so shrinking this is
// safe to try -- it cannot silently corrupt a label.
#ifndef CILKPRACE_LABEL_WORDS
#define CILKPRACE_LABEL_WORDS 6
#endif
#include <cstdint>
#include <ostream>
#include <vector>

struct alignas(8) os_label {
  // bit at index i indicates l or r at depth i
  // depths i = 4*k+[0,2] represent S nodes
  // depths i = 4*k+3 represent P nodes
  // ...76543210
  // ...PSSSPSSS
  //
  // At each spawn, we make use of the next unused p bit.
  // Left child is set to 0, right child is set to 1.
  //
  // At each sync, we need to increment S bits like a counter.
  // If this overflows a group of 3 s nodes, the carry should propagate through
  // the P bit, and the P bit should be set to 0 to be left unused
  //
  // Differentiating between an unused P bit vs a l/r child P bit is not
  // possible, so instead of popping a continuation, we use the parent's label
  // as reference
  uint64_t data[CILKPRACE_LABEL_WORDS] = {};
  uint16_t end_idx = 0;
  uint8_t _pad[6] = {};

  static constexpr int scan_max_low_offset_bytes = sizeof(data) - sizeof(uint64_t);

  void append_left_child();
  void append_right_child();
  uint16_t get_restore_point() const;
  void restore_on_sync(uint16_t restore_idx);
  bool is_identical(const os_label &other) const;
  unsigned lca(const os_label &other) const;

  std::vector<uint8_t> to_vector() const;
};

static_assert(sizeof(os_label) == CILKPRACE_LABEL_WORDS * 8 + 8,
              "os_label must be CILKPRACE_LABEL_WORDS words + end_idx + padding");
static_assert(alignof(os_label) == 8, "os_label must be 8-byte aligned");

// is_identical and lca are defined here rather than in the runtime .cpp so
// they inline into the race detector. The detector is compiled as bitcode and
// inlined into every instrumented function, but these two used to live in the
// opencilk dylib, so each call from that hot inlined code was an out-of-line
// cross-library stub call -- cholesky's mul_and_subT alone made 249 lca and
// 104 is_identical calls. Both are pure functions of two labels.
//
// Inline all of each, including the multi-word cases. Two-word labels are the
// common case, not a rare tail (about 190M of cholesky's ~520M checks), so
// moving the wide path back into the runtime made cholesky 1.4x slower. And it
// cannot be a noinline helper here: CSI links the tool bitcode as
// available_externally, so anything not inlined must come from a runtime lib.
// For the same reason both are always_inline: the dylib no longer exports
// them, so a call site the inliner declines (a cold branch, say) would be an
// undefined symbol at link time.
__attribute__((always_inline)) inline bool os_label::is_identical(const os_label &other) const {
  if (end_idx != other.end_idx)
    return false;
#if !CILKPRACE_ABL_LABEL_CMP_FASTPATH
  // Generic word loop, no width specialization.
  {
    unsigned full_words = end_idx / 64;
    for (unsigned i = 0; i < full_words; ++i) {
      if (data[i] != other.data[i])
        return false;
    }
    unsigned remain = end_idx % 64;
    if (remain != 0) {
      uint64_t mask = (1ULL << remain) - 1;
      return ((data[full_words] ^ other.data[full_words]) & mask) == 0;
    }
    return true;
  }
#else
  if (end_idx <= 64) {
    uint64_t mask = (end_idx == 64) ? ~0ULL : ((1ULL << end_idx) - 1);
    return ((data[0] ^ other.data[0]) & mask) == 0;
  }
  if (data[0] != other.data[0])
    return false;
  if (end_idx <= 128) {
    uint64_t mask = (end_idx == 128) ? ~0ULL : ((1ULL << (end_idx - 64)) - 1);
    return ((data[1] ^ other.data[1]) & mask) == 0;
  }
  if (data[1] != other.data[1])
    return false;
  unsigned full_words = end_idx / 64;
  for (unsigned i = 2; i < full_words; ++i) {
    if (data[i] != other.data[i])
      return false;
  }
  unsigned remain = end_idx % 64;
  if (remain != 0) {
    uint64_t mask = (1ULL << remain) - 1;
    return ((data[full_words] ^ other.data[full_words]) & mask) == 0;
  }
  return true;
#endif
}

__attribute__((always_inline)) inline unsigned os_label::lca(const os_label &other) const {
  unsigned last_idx = other.end_idx < end_idx ? other.end_idx : end_idx;
#if !CILKPRACE_ABL_LABEL_CMP_FASTPATH
  // Generic word loop, no width specialization.
  {
    unsigned full_words = last_idx / 64;
    for (unsigned i = 0; i < full_words; ++i) {
      if (data[i] != other.data[i])
        return i * 64 + __builtin_ctzll(data[i] ^ other.data[i]);
    }
    unsigned remain_bits = last_idx % 64;
    if (remain_bits == 0)
      return last_idx;
    return full_words * 64 +
           __builtin_ctzll((data[full_words] ^ other.data[full_words]) |
                           (1ull << remain_bits));
  }
#else
  if (last_idx < 64) {
    uint64_t diff = (data[0] ^ other.data[0]) | (1ull << last_idx);
    return __builtin_ctzll(diff);
  }
  if (data[0] != other.data[0]) {
    return __builtin_ctzll(data[0] ^ other.data[0]);
  }
  if (last_idx == 64) return 64;
  if (last_idx < 128) {
    uint64_t diff = (data[1] ^ other.data[1]) | (1ull << (last_idx - 64));
    return 64 + __builtin_ctzll(diff);
  }
  if (data[1] != other.data[1]) {
    return 64 + __builtin_ctzll(data[1] ^ other.data[1]);
  }
  if (last_idx == 128) return 128;
  unsigned full_words = last_idx / 64;
  for (unsigned i = 2; i < full_words; i++) {
    if (data[i] != other.data[i]) {
      return i * 64 + __builtin_ctzll(data[i] ^ other.data[i]);
    }
  }
  unsigned remain_bits = last_idx % 64;
  return full_words * 64 +
         __builtin_ctzll((data[full_words] ^ other.data[full_words]) |
                         (1ull << remain_bits));
#endif
}

#ifdef ENABLE_LABEL_PRINTING
std::ostream &operator<<(std::ostream &os, const os_label &l);
#endif

#pragma GCC visibility pop

#endif // _OS_LABEL_LEB8_SINGLE_H
