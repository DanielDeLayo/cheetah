#include <cilk/os_label.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <ostream>

// Label-capacity checks. These sit on spawn/sync paths, not per-access, so they
// are always compiled in: the old asserts vanished under NDEBUG and a label that
// overruns data[] silently corrupts end_idx and, inside shadow_label, write_depth
// and the seqlock -- i.e. it turns into wrong race verdicts rather than a crash.
#define CILKPRACE_LABEL_CHECK(cond, ...)                                       \
  do {                                                                         \
    if (__builtin_expect(!(cond), 0)) {                                        \
      fprintf(stderr, "cilkprace: label overflow: ");                          \
      fprintf(stderr, __VA_ARGS__);                                            \
      fprintf(stderr,                                                          \
              "\n  capacity %zu bits (CILKPRACE_LABEL_WORDS=%d). Note the "    \
              "limit is spawns per sync region, not nesting depth: each spawn " \
              "in a frame advances the label 4 bits until its sync.\n",        \
              sizeof(data) * 8, CILKPRACE_LABEL_WORDS);                        \
      abort();                                                                 \
    }                                                                          \
  } while (0)

void os_label::append_left_child() {
  int scan_low_offset_bytes = end_idx / 8;

  if (scan_low_offset_bytes > scan_max_low_offset_bytes) {
    scan_low_offset_bytes = scan_max_low_offset_bytes;
  }

  uint64_t *scan_low_addr = (uint64_t *)((uint8_t *)data + scan_low_offset_bytes);

  uint64_t scan_val = *scan_low_addr;
  uint64_t scan_low_idx = end_idx - scan_low_offset_bytes * 8;
  assert(scan_low_idx < 64);

  uint64_t scan_val_capped = scan_val | 1ull << scan_low_idx;
  int high_set_idx = 63 - __builtin_clzll(scan_val_capped);
  end_idx = ((high_set_idx + 2) | 3) + 1 + scan_low_offset_bytes * 8;

  CILKPRACE_LABEL_CHECK(end_idx <= sizeof(data) * 8,
                        "spawn advanced label to %u bits", (unsigned)end_idx);
}

void os_label::append_right_child() {
  append_left_child();
  assert(end_idx % 4 == 0);
  assert(end_idx > 0);
  uint64_t p_idx = end_idx - 1;
  CILKPRACE_LABEL_CHECK(p_idx / 64ull < CILKPRACE_LABEL_WORDS,
                        "P-bit write at word %llu", (unsigned long long)(p_idx / 64ull));
  data[p_idx / 64ull] |= 1ull << (p_idx % 64ull);
}

void os_label::restore_on_sync(uint16_t restore_idx) {
  CILKPRACE_LABEL_CHECK(restore_idx <= sizeof(data) * 8,
                        "sync restore point %u bits", (unsigned)restore_idx);
  int scan_low_offset_bytes = restore_idx / 8;

  if (scan_low_offset_bytes > scan_max_low_offset_bytes) {
    scan_low_offset_bytes = scan_max_low_offset_bytes;
  }

  uint64_t *scan_low_addr = (uint64_t *)((uint8_t *)data + scan_low_offset_bytes);
  uint64_t scan_low_idx = restore_idx - scan_low_offset_bytes * 8;
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
  assert(clear_low_addr <= clear_high_addr);
  memset(clear_low_addr, 0, clear_high_addr - clear_low_addr);

  end_idx = restore_idx;
}

uint16_t os_label::get_restore_point() const {
  return end_idx;
}

std::vector<uint8_t> os_label::to_vector() const {
  std::vector<uint8_t> vec;
  auto get_nibble = [&](size_t idx) -> uint8_t {
    if (idx >= CILKPRACE_LABEL_WORDS * 16)
      return 0;
    return (data[idx / 16] >> ((idx % 16) * 4)) & 0xF;
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

    if (p_bit_idx >= end_idx) {
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
  for (int i = 0; i < CILKPRACE_LABEL_WORDS; i++) {
    os << std::hex << std::setw(16) << std::setfill('0') << l.data[i] << " ";
  }
  os << ": " << l.end_idx;
  return os;
}
#endif
