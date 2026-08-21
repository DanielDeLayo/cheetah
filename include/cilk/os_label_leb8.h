#ifndef _OS_LABEL_LEB8_H
#define _OS_LABEL_LEB8_H

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

// Represents your status relative to the other label.
// Either you've synced since them, you're within the range, or your're simply
// in parallel
enum range_check { synced, within, parallel, identical };

struct os_label {
    // LEB8 encoded bit array. Each byte holds two 4-bit blocks.
    // Block format: [C (1 bit), P (3 bits)] where C is continuation.
    // 62 is so it's properly aligned 8 byte aligned
    uint8_t data[62] = {0};
    uint16_t offset =
        0; // Index of the last block. 0-initialized means 1 block at index 0.

    inline uint8_t get_block(size_t index) const {
        if ((index & 1) == 0) {
            return data[index >> 1] & 0x0F;
        } else {
            return data[index >> 1] >> 4;
        }
    }

    inline void set_block(size_t index, uint8_t block) {
        if ((index & 1) == 0) {
            data[index >> 1] = (data[index >> 1] & 0xF0) | (block & 0x0F);
        } else {
            data[index >> 1] =
                (data[index >> 1] & 0x0F) | ((block & 0x0F) << 4);
        }
    }

    // Finds the exact block index where this and rhs diverge
    __attribute__((always_inline)) size_t
    calc_matching_block_length(const os_label &rhs) const {
        size_t min_offset = offset < rhs.offset ? offset : rhs.offset;
        size_t min_blocks = min_offset + 1;
        size_t min_bytes = (min_blocks + 1) >> 1;
        size_t num_matches = 0;

        // Compare in 8-byte chunks (16 blocks at a time)
        while (num_matches + 8 <= min_bytes) {
            uint64_t v1 =
                *reinterpret_cast<const uint64_t *>(&data[num_matches]);
            uint64_t v2 =
                *reinterpret_cast<const uint64_t *>(&rhs.data[num_matches]);

            if (v1 == v2) {
                num_matches += 8;
            } else {
                uint64_t diff = v1 ^ v2;
                size_t byte_diff = __builtin_ctzll(diff) / 8;
                size_t byte_idx = num_matches + byte_diff;
                uint8_t b1 = data[byte_idx];
                uint8_t b2 = rhs.data[byte_idx];
                return (byte_idx << 1) + ((b1 & 0x0F) == (b2 & 0x0F) ? 1 : 0);
            }
        }

        // Compare remaining bytes
        for (; num_matches < min_bytes; num_matches++) {
            if (data[num_matches] != rhs.data[num_matches]) {
                uint8_t b1 = data[num_matches];
                uint8_t b2 = rhs.data[num_matches];
                return (num_matches << 1) +
                       ((b1 & 0x0F) == (b2 & 0x0F) ? 1 : 0);
            }
        }

        return min_blocks;
    }

    __attribute__((always_inline)) inline bool
    is_identical(const os_label &rhs) const {
        if (__builtin_expect(offset != rhs.offset, 0))
            return false;
        size_t bytes = (offset + 2) >> 1;
        if (__builtin_expect(bytes <= 8, 1)) {
            uint64_t v1 = *reinterpret_cast<const uint64_t *>(data);
            uint64_t v2 = *reinterpret_cast<const uint64_t *>(rhs.data);
            uint64_t mask =
                (bytes == 8) ? ~0ULL : ((1ULL << (bytes * 8)) - 1);
            return ((v1 ^ v2) & mask) == 0;
        }
        return memcmp(data, rhs.data, bytes) == 0;
    }

    __attribute__((always_inline)) inline void copy_from(const os_label &src) {
        offset = src.offset;
        size_t bytes = (src.offset + 2) >> 1;
        if (__builtin_expect(bytes <= 8, 1)) {
            *reinterpret_cast<uint64_t *>(data) =
                *reinterpret_cast<const uint64_t *>(src.data);
        } else if (__builtin_expect(bytes <= 16, 1)) {
            *reinterpret_cast<uint64_t *>(data) =
                *reinterpret_cast<const uint64_t *>(src.data);
            *reinterpret_cast<uint64_t *>(&data[8]) =
                *reinterpret_cast<const uint64_t *>(&src.data[8]);
        } else {
            memcpy(data, src.data, bytes);
        }
    }

    __attribute__((always_inline)) bool is_serial() const {
        return offset == 0;
    }

    // Finds the start of the level containing block 'i'
    __attribute__((always_inline)) size_t find_level_start(size_t i) const {
        if (i == 0)
            return 0;
        size_t level_start = i;

        // Block-by-block if i is odd (not aligned to byte)
        if ((level_start & 1) == 1) {
            if ((get_block(level_start - 1) & 8) == 0)
                return level_start;
            level_start--;
        }

        if (level_start == 0)
            return 0;

        // Scan backwards byte-by-byte
        size_t byte_idx = (level_start >> 1) - 1;
        while (true) {
            uint8_t b = data[byte_idx];
            if ((b & 0x80) == 0) { // Upper block C is 0
                return (byte_idx << 1) + 2;
            }
            if ((b & 0x08) == 0) { // Lower block C is 0
                return (byte_idx << 1) + 1;
            }
            if (byte_idx == 0)
                break;
            byte_idx--;
        }
        return 0;
    }

    void push_level(uint64_t V) {
        do {
            if (offset + 1 >= 128) {
                fprintf(stderr, "[CilkPrace Error] Label length overflow!\n");
                exit(EXIT_FAILURE);
            }
            uint8_t payload = V & 7;
            V >>= 3;
            uint8_t continuation = (V > 0) ? 1 : 0;
            offset++;
            set_block(offset, (continuation << 3) | payload);
        } while (V > 0);
    }

  public:
    bool is_empty() const { return offset == 0 && data[0] == 0; }

    __attribute__((always_inline)) void append_left_child() { push_level(0); }

    __attribute__((always_inline)) void append_right_child() { push_level(1); }

    __attribute__((always_inline)) void restore_on_sync(uint8_t conts) {
        if (conts == 0)
            return;

        // Drop 'conts' levels (the children)
        while (conts > 0 && offset > 0) {
            // Check current block
            if ((get_block(offset) & 8) == 0) {
                conts--;
            }
            offset--;
            if (conts == 0)
                break;

            // Align to byte boundary
            if ((offset & 1) == 0) {
                if ((get_block(offset) & 8) == 0) {
                    conts--;
                }
                if (offset == 0)
                    break;
                offset--;
            }

            // Scan 8-byte chunks backwards
            while (conts > 0 && offset >= 15) {
                size_t chunk_start_byte = (offset >> 1) - 7;
                uint64_t chunk = *reinterpret_cast<const uint64_t *>(
                    &data[chunk_start_byte]);
                // Count zeros in bit 3 and bit 7 positions
                uint64_t not_C = ~chunk & 0x8888888888888888ULL;
                int zeros = __builtin_popcountll(not_C);

                if (zeros < conts) {
                    conts -= zeros;
                    if (offset < 16) {
                        offset = 0;
                        break;
                    }
                    offset -= 16;
                } else {
                    // It's in this chunk, find exactly where
                    break;
                }
            }

            // Revert to byte-by-byte or block-by-block to finish the last few
            while (conts > 0 && offset > 0) {
                if ((get_block(offset) & 8) == 0) {
                    conts--;
                }
                offset--;
            }
        }

        // At this point, we've dropped the C=0 blocks of 'conts' levels.
        // But offset might be pointing to a C=1 block that belongs to the last
        // dropped level! We must drop all C=1 blocks until we hit the C=0 block
        // of the remaining parent level.
        while (offset > 0 && (get_block(offset) & 8) != 0) {
            offset--;
        }

        // Find the start block of the parent (now the last level)
        size_t parent_start = find_level_start(offset);

        // In-place addition of 2 to the parent's value (LEB8 ripple-carry)
        uint8_t carry = 2;
        for (size_t i = parent_start; i <= offset; i++) {
            uint8_t block = get_block(i);
            uint8_t payload = block & 7;
            uint8_t continuation = block & 8;

            uint8_t sum = payload + carry;
            set_block(i, continuation | (sum & 7));

            carry = sum >> 3;
            if (carry == 0)
                break;
        }

        // Handle overflow if the parent value grew to require a new block
        if (carry > 0) {
            if (offset + 1 >= 128) {
                fprintf(stderr, "[CilkPrace Error] Label length overflow!\n");
                exit(EXIT_FAILURE);
            }
            set_block(offset, get_block(offset) | 8);
            offset++;
            set_block(offset, carry);
        }
    }

    // Returns true if in parallel
    bool is_parallel(const os_label &rhs) const {
        if (is_empty() || rhs.is_empty())
            return false;

        size_t i = calc_matching_block_length(rhs);

        if (i == offset + 1 || i == rhs.offset + 1) {
            // One is a prefix of the other -> series
            return false;
        }

        // Find the start of the diverging level
        size_t level_start = find_level_start(i);

        uint8_t my_dir = get_block(level_start) & 1;
        uint8_t rhs_dir = rhs.get_block(level_start) & 1;

        return my_dir != rhs_dir;
    }

    // Should fixup LCA range?
    range_check range_relation(const os_label &rhs,
                               const bool &is_range) const {
        size_t i = calc_matching_block_length(rhs);

        if (i == offset + 1 && i == rhs.offset + 1)
            return identical;

        if (i == rhs.offset + 1) // rhs is prefix of this -> this is descendent
            return is_range ? within : synced;

        if (i == offset + 1) // this is prefix of rhs -> this is ancestor
            return is_range ? within : synced;

        // Check if parallel
        size_t level_start = find_level_start(i);

        uint8_t my_dir = get_block(level_start) & 1;
        uint8_t rhs_dir = rhs.get_block(level_start) & 1;

        if (my_dir != rhs_dir) {
            return parallel;
        }

        return synced;
    }

    // Return a clean vector of the current label values
    std::vector<uint8_t> to_vector() const {
        std::vector<uint8_t> vec;
        size_t i = 0;
        while (i <= offset) {
            uint64_t V = 0;
            int shift = 0;
            while (i <= offset) {
                uint8_t block = get_block(i++);
                V |= (uint64_t)(block & 7) << shift;
                shift += 3;
                if ((block & 8) == 0)
                    break;
            }
            vec.push_back(V);
        }
        return vec;
    }

    // Fixup parallel LCA range
    void expand_parallel_range(os_label &rhs) const {
        size_t i = calc_matching_block_length(rhs);
        size_t level_start = find_level_start(i);

        // Truncate rhs to the LCA
        rhs.offset = level_start > 0 ? level_start - 1 : 0;
        if (level_start == 0) {
            rhs.set_block(0, 0);
        } else {
            // Expand to the continuation (left child, which is represented by
            // pushing 0 payload)
            rhs.offset = level_start;
            rhs.set_block(rhs.offset, 0);
        }
    }

    inline friend std::ostream &operator<<(std::ostream &os,
                                           const os_label &l) {
        std::vector<uint8_t> vec = l.to_vector();
        os << "Length: " << vec.size() << ", Label:";
        for (uint64_t v : vec)
            os << " " << v;
        return os;
    }
};

#endif // _OS_LABEL_LEB8_H
