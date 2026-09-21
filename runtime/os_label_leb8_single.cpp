#include <cilk/os_label.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iomanip>
#include <ostream>

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

  assert(end_idx <= sizeof(data) * 8);
}

void os_label::append_right_child() {
  append_left_child();
  assert(end_idx % 4 == 0);
  assert(end_idx > 0);
  uint64_t p_idx = end_idx - 1;
  data[p_idx / 64ull] |= 1ull << (p_idx % 64ull);
}

void os_label::restore_on_sync(uint16_t restore_idx) {
  int scan_low_offset_bytes = restore_idx / 8;

  if (scan_low_offset_bytes > scan_max_low_offset_bytes) {
    scan_low_offset_bytes = scan_max_low_offset_bytes;
  }

  uint64_t *scan_low_addr = (uint64_t *)((uint8_t *)data + scan_low_offset_bytes);
  uint64_t scan_low_idx = restore_idx - scan_low_offset_bytes * 8;
  assert(scan_low_idx < 64);
  assert(scan_low_idx % 4 == 0);

  uint64_t scan_val = *scan_low_addr;
  uint64_t p_mask = 0x8888888888888888ull << scan_low_idx;

  // Clear continuations
  uint64_t conts = scan_val & p_mask;
  scan_val &= ~conts & (conts - 1);

  // Increment s value.
  uint64_t carries = scan_val & (p_mask >> 1ull);
  scan_val |= p_mask;
  scan_val += 1 << scan_low_idx;
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

bool os_label::is_identical(const os_label &other) const {
  if (end_idx != other.end_idx)
    return false;
  unsigned words = (end_idx + 63) / 64;
  for (unsigned i = 0; i < words; ++i) {
    if (data[i] != other.data[i])
      return false;
  }
  return true;
}

unsigned os_label::lca(const os_label &other) const {
  unsigned last_idx = std::min(other.end_idx, end_idx);
  if (__builtin_expect(last_idx <= 64, 1)) {
    uint64_t diff = data[0] ^ other.data[0];
    if (diff != 0) {
      unsigned ctz = __builtin_ctzll(diff);
      return ctz < last_idx ? ctz : last_idx;
    }
    return last_idx;
  }

  unsigned full_words = last_idx / 64;
  unsigned match_bits = 0;
  for (unsigned i = 0; i < full_words; i++) {
    if (data[i] != other.data[i]) {
      return match_bits + __builtin_ctzll(data[i] ^ other.data[i]);
    }
    match_bits += 64;
  }

  unsigned remain_bits = last_idx % 64;
  return match_bits +
         __builtin_ctzll((data[full_words] ^ other.data[full_words]) |
                         (1ull << remain_bits));
}

std::vector<uint8_t> os_label::to_vector() const {
  std::vector<uint8_t> vec;
  auto get_nibble = [&](size_t idx) -> uint8_t {
    if (idx >= 7 * 16)
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
  for (int i = 0; i < 7; i++) {
    os << std::hex << std::setw(16) << std::setfill('0') << l.data[i] << " ";
  }
  os << ": " << l.end_idx;
  return os;
}
#endif
