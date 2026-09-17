#ifndef _OS_LABEL_LEB8_H
#define _OS_LABEL_LEB8_H

#include <cassert>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <vector>

#pragma pack(push, 2)
struct os_label {
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
  uint64_t data[7] = {};
  uint16_t end_idx = 0;

  unsigned lca(const os_label& other) {
    unsigned last_idx = std::min(other.end_idx, end_idx);
    unsigned full_words = last_idx / 64;

    unsigned match_bits = 0;
    for (unsigned i = 0; i < full_words; i++) {
      if (data[i] != other.data[i]) {
        return match_bits + __builtin_ctzll(data[i] ^ other.data[i]);
      }
      match_bits += 64;
    }

    unsigned remain_bits = last_idx % 64;
    return match_bits + __builtin_ctzll((data[full_words] ^ other.data[full_words]) | (1ull << remain_bits));
  }

  std::vector<uint8_t> to_vector() const {
    std::vector<uint8_t> vec;
    auto get_nibble = [&](size_t idx) -> uint8_t {
      if (idx >= 7 * 16) return 0;
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
};
#pragma pack(pop)


#ifdef ENABLE_LABEL_PRINTING
inline std::ostream &operator<<(std::ostream &os, const os_label &l) {
  for (int i = 0; i < 7; i++) {
    os << std::hex << std::setw(16) << std::setfill('0') << l.data[i] << " ";
  }
  os << ": " << l.end_idx;
  return os;
}
#endif


#endif // _OS_LABEL_LEB8_H
