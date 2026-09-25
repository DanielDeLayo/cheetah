#ifndef _OS_LABEL_LEB8_ANCHOR_H
#define _OS_LABEL_LEB8_ANCHOR_H

#pragma GCC visibility push(default)

// leb8-single's label with no depth limit, for leb8-ptr. Same encoding and
// same operations, but only the newest bits live inline, in a window; when a
// spawn would outgrow it, the older words move to an os_label_chunk (see
// prepare_spawn). Each level costs 4 bits, and every spawn nests one level
// until its frame syncs, so a label grows with spawn nesting depth plus spawns
// per sync region, without bound. Labels of shallow programs never leave the
// window.
//
// Width of the window, in 64-bit words. Not a CMake setting: the runtime and
// the race detector both take it from here, so they cannot disagree. At 5
// words a leb8-ptr table record is 64 bytes.
#ifndef CILKPRACE_ANCHOR_WORDS
#define CILKPRACE_ANCHOR_WORDS 5
#endif
#include <cstdint>
#include <ostream>
#include <vector>

static_assert(CILKPRACE_ANCHOR_WORDS >= 2, "a spawn needs one word of headroom");

// Words a label spilled out of its window. Immutable once made and never
// freed: labels in leb8-ptr's table keep pointing at chunks long after the
// frame that made them returns.
struct os_label_chunk {
  const os_label_chunk *parent; // the chunk holding the words before these
  uint32_t base;                // index in the whole label of bits[0]'s bit 0
  uint32_t nwords;              // at most CILKPRACE_ANCHOR_WORDS - 1
  uint64_t bits[CILKPRACE_ANCHOR_WORDS - 1];
};

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
  //
  // data[] holds bits [base, base + 64 * CILKPRACE_ANCHOR_WORDS) of the whole
  // label, and the chain from anchor holds bits [0, base). end_idx is relative
  // to the window, as in leb8-single, so the whole label is end() bits long;
  // restore points and lca results count bits of the whole label. base is a
  // multiple of 64, so an index's bit type (index % 4) is the same either way.
  // Two labels with the same anchor have the same bits below base. For a label
  // that never left its window, base is 0 and anchor null.
  uint64_t data[CILKPRACE_ANCHOR_WORDS] = {};
  const os_label_chunk *anchor = nullptr;
  uint32_t base = 0;
  uint32_t end_idx = 0;

  static constexpr unsigned window_bits = CILKPRACE_ANCHOR_WORDS * 64;
  static constexpr int scan_max_low_offset_bytes = sizeof(data) - sizeof(uint64_t);

  // Call on the parent before copying its label to a spawned child, so both
  // share any chunk it makes.
  void prepare_spawn() {
    if (__builtin_expect(end_idx / 8 * 8 + 72 > window_bits, 0))
      spill();
  }
  uint32_t end() const { return base + end_idx; }

  // What the runtime resets the tool word after this label to when the label
  // changes (see tool_word in pedigree-internal.h): the top bit says whether
  // it has left its window, so leb8-ptr's inline checks can tell from the word
  // they load anyway, without loading anchor.
  static constexpr uint64_t tool_word_deep = 1ull << 63;
  uint64_t fresh_tool_word() const { return anchor ? tool_word_deep : 0; }
  void append_left_child();
  void append_right_child();
  uint32_t get_restore_point() const;
  void restore_on_sync(uint32_t restore_idx);
  unsigned lca(const os_label &other) const;

  std::vector<uint8_t> to_vector() const;

  // Out of line: the rare paths, for labels that outgrow the window.
  void spill();
  void rewind(uint32_t idx);
  unsigned lca_far(const os_label &other) const;
  uint64_t word_at(uint32_t w) const;

  unsigned lca_window(const os_label &other, unsigned last_idx) const;
};

static_assert(sizeof(os_label) == CILKPRACE_ANCHOR_WORDS * 8 + 16,
              "os_label must be the window, anchor, base and end_idx");
static_assert(alignof(os_label) == 8, "os_label must be 8-byte aligned");

// Inline, for leb8-ptr's out-of-line paths: CSI links the tool bitcode as
// available_externally, so anything it calls that is not inlined must come
// from a runtime lib, and the dylib does not export these.
__attribute__((always_inline)) inline unsigned os_label::lca_window(const os_label &other,
                                                                  unsigned last_idx) const {
  unsigned full_words = last_idx / 64;
  for (unsigned i = 0; i < full_words; ++i) {
    if (data[i] != other.data[i])
      return i * 64 + __builtin_ctzll(data[i] ^ other.data[i]);
  }
  unsigned remain_bits = last_idx % 64;
  if (remain_bits == 0)
    return last_idx;
  return full_words * 64 +
         __builtin_ctzll((data[full_words] ^ other.data[full_words]) | (1ull << remain_bits));
}

__attribute__((always_inline)) inline unsigned os_label::lca(const os_label &other) const {
  if (__builtin_expect(anchor != other.anchor, 0))
    return lca_far(other);
  unsigned last_idx = other.end_idx < end_idx ? other.end_idx : end_idx;
  return base + lca_window(other, last_idx);
}

#ifdef ENABLE_LABEL_PRINTING
std::ostream &operator<<(std::ostream &os, const os_label &l);
#endif

#pragma GCC visibility pop

#endif // _OS_LABEL_LEB8_ANCHOR_H
