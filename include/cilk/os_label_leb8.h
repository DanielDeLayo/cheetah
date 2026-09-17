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

    constexpr int vec_len = 2;
    using block_t = uint64_t __attribute__((ext_vector_type(vec_len)));
    using mask_t = bool __attribute__((ext_vector_type(vec_len)));
    uint64_t full_blocks = last_idx / (8 * sizeof(block_t));
    uint64_t final_block_idx = last_idx % (8 * sizeof(block_t));

    if (__builtin_expect(full_blocks == 0, 1)) {
      unsigned cmp_msk;
      *(mask_t*)&cmp_msk = __builtin_convertvector(
          ((const block_t*)data)[0] != ((const block_t*)other.data)[0], mask_t);
      cmp_msk |= 1ull << (final_block_idx / 64);
      unsigned firstdiff = __builtin_ctzll(cmp_msk);
      uint64_t cmp_bits = (data[firstdiff] ^ other.data[firstdiff]);
      cmp_bits |= 1ull << (final_block_idx % 64);
      return 64 * firstdiff + __builtin_ctzll(cmp_bits);
    }

    for (unsigned i = 0; i <= full_blocks; i++) {
      unsigned cmp_msk;
      *(mask_t*)&cmp_msk = __builtin_convertvector(
          ((const block_t*)data)[i] != ((const block_t*)other.data)[i], mask_t);
      if (i == full_blocks) {
        cmp_msk |= 1ull << (final_block_idx / 64);
      }
      if (cmp_msk) {
        unsigned firstdiff = i * vec_len + __builtin_ctzll(cmp_msk);
        uint64_t cmp_bits = (data[firstdiff] ^ other.data[firstdiff]);
        if (i == full_blocks) {
          cmp_bits |= 1ull << (final_block_idx % 64);
        }
        return 64 * firstdiff + __builtin_ctzll(cmp_bits);
      }
    }
    __builtin_unreachable();
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
