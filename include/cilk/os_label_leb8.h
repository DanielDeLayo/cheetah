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
    size_t num_bytes = (end_idx + 7) / 8;
    if (num_bytes == 0) num_bytes = 1;
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
    for (size_t i = 0; i < num_bytes; ++i) {
      vec.push_back(bytes[i]);
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
