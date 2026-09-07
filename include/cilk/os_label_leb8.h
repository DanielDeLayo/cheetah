#ifndef _OS_LABEL_LEB8_H
#define _OS_LABEL_LEB8_H

#include <cstddef>
#include <cstdint>
#include <cstring>
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
    uint8_t data[59] = {0};
    uint8_t offset =
        0; // Index of the last block. 0-initialized means 1 block at index 0.

    static constexpr size_t max_blocks = sizeof(data) * 2;

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
    __attribute__((always_inline))
    inline size_t calc_matching_block_length(const os_label &rhs) const {
        size_t min_offset = offset < rhs.offset ? offset : rhs.offset;
        size_t min_blocks = min_offset + 1;
        const uint64_t *w1 = reinterpret_cast<const uint64_t *>(data);
        const uint64_t *w2 = reinterpret_cast<const uint64_t *>(rhs.data);

        uint64_t diff = w1[0] ^ w2[0];
        if (__builtin_expect(min_offset < 16, 1)) {
            if (diff != 0) {
                size_t nibble_idx = __builtin_ctzll(diff) >> 2;
                return nibble_idx < min_blocks ? nibble_idx : min_blocks;
            }
            return min_blocks;
        }

        if (diff != 0) {
            return __builtin_ctzll(diff) >> 2;
        }

        size_t min_bytes = (min_blocks + 1) >> 1;
        size_t num_words = (min_bytes + 7) >> 3;
        for (size_t i = 1; i < num_words; i++) {
            if (w1[i] != w2[i]) {
                size_t nibble_idx = (i << 4) + (__builtin_ctzll(w1[i] ^ w2[i]) >> 2);
                return nibble_idx < min_blocks ? nibble_idx : min_blocks;
            }
        }
        return min_blocks;
    }

    __attribute__((noinline, cold, preserve_most, visibility("default")))
    bool is_identical_slow(const os_label &rhs) const {
        size_t min_blocks = offset + 1;
        size_t min_bytes = (min_blocks + 1) >> 1;
        size_t num_words = (min_bytes + 7) >> 3;
        const uint64_t *w1 = reinterpret_cast<const uint64_t *>(data);
        const uint64_t *w2 = reinterpret_cast<const uint64_t *>(rhs.data);

        for (size_t i = 0; i < num_words - 1; i++) {
            if (w1[i] != w2[i])
                return false;
        }

        size_t last_word = num_words - 1;
        size_t rem_blocks = min_blocks - (last_word << 4);
        uint64_t mask =
            (rem_blocks == 16) ? ~0ULL : ((1ULL << (rem_blocks * 4)) - 1);
        return ((w1[last_word] ^ w2[last_word]) & mask) == 0;
    }

    __attribute__((visibility("default")))
    bool is_prefix_slow(const os_label &full) const {
        size_t min_blocks = offset + 1;
        size_t min_bytes = (min_blocks + 1) >> 1;
        size_t num_words = (min_bytes + 7) >> 3;
        const uint64_t *w1 = reinterpret_cast<const uint64_t *>(data);
        const uint64_t *w2 = reinterpret_cast<const uint64_t *>(full.data);

        for (size_t i = 0; i < num_words - 1; i++) {
            if (w1[i] != w2[i])
                return false;
        }

        size_t last_word = num_words - 1;
        size_t rem_blocks = min_blocks - (last_word << 4);
        uint64_t mask =
            (rem_blocks == 16) ? ~0ULL : ((1ULL << (rem_blocks * 4)) - 1);
        return ((w1[last_word] ^ w2[last_word]) & mask) == 0;
    }

    __attribute__((always_inline))
    bool is_prefix_of(const os_label &full) const {
        if (__builtin_expect(offset > full.offset, 0))
            return false;
        const uint64_t *w1 = reinterpret_cast<const uint64_t *>(data);
        const uint64_t *w2 = reinterpret_cast<const uint64_t *>(full.data);
        if (__builtin_expect(offset < 15, 1)) {
            uint64_t mask = (1ULL << ((offset + 1) << 2)) - 1;
            return ((w1[0] ^ w2[0]) & mask) == 0;
        }
        if (offset == 15) {
            return w1[0] == w2[0];
        }
        if (__builtin_expect(offset < 31, 1)) {
            if (w1[0] != w2[0]) return false;
            uint64_t mask = (1ULL << ((offset - 15) << 2)) - 1;
            return ((w1[1] ^ w2[1]) & mask) == 0;
        }
        if (offset == 31) {
            return w1[0] == w2[0] && w1[1] == w2[1];
        }
        return is_prefix_slow(full);
    }

    __attribute__((always_inline))
    bool is_identical(const os_label &rhs) const {
        if (offset != rhs.offset)
            return false;
        const uint64_t *w1 = reinterpret_cast<const uint64_t *>(data);
        const uint64_t *w2 = reinterpret_cast<const uint64_t *>(rhs.data);
        if (__builtin_expect(offset < 15, 1)) {
            uint64_t mask = (1ULL << ((offset + 1) << 2)) - 1;
            return ((w1[0] ^ w2[0]) & mask) == 0;
        }
        if (offset == 15) {
            return w1[0] == w2[0];
        }
        if (__builtin_expect(offset < 31, 1)) {
            if (w1[0] != w2[0]) return false;
            uint64_t mask = (1ULL << ((offset - 15) << 2)) - 1;
            return ((w1[1] ^ w2[1]) & mask) == 0;
        }
        if (offset == 31) {
            return w1[0] == w2[0] && w1[1] == w2[1];
        }
        return is_identical_slow(rhs);
    }

    void copy_from(const os_label &src) {
        offset = src.offset;
        if (__builtin_expect(src.offset < 15, 1)) {
            *reinterpret_cast<uint64_t *>(data) =
                *reinterpret_cast<const uint64_t *>(src.data);
            return;
        }
        size_t bytes = (src.offset + 2) >> 1;
        memcpy(data, src.data, bytes);
    }

    bool is_serial() const {
        for (size_t i = 0; i < offset; i++) {
            if ((get_block(i) & 8) == 0)
                return false;
        }
        return true;
    }

    // Finds the start of the level containing block 'i'
    __attribute__((always_inline))
    inline size_t find_level_start(size_t i) const {
        if (i == 0)
            return 0;

        const uint64_t *w = reinterpret_cast<const uint64_t *>(data);
        if (__builtin_expect(i < 16, 1)) {
            uint64_t valid_mask = (1ULL << (i << 2)) - 1;
            uint64_t c_zeros = (~w[0] & 0x8888888888888888ULL) & valid_mask;
            if (c_zeros != 0) {
                return ((63 - __builtin_clzll(c_zeros)) >> 2) + 1;
            }
            return 0;
        }

        size_t word_idx = i >> 4;
        size_t rem = i & 15;

        if (rem > 0) {
            uint64_t valid_mask = (1ULL << (rem << 2)) - 1;
            uint64_t c_zeros =
                (~w[word_idx] & 0x8888888888888888ULL) & valid_mask;
            if (c_zeros != 0) {
                return (word_idx << 4) +
                       ((63 - __builtin_clzll(c_zeros)) >> 2) + 1;
            }
        }

        while (word_idx > 0) {
            word_idx--;
            uint64_t c_zeros = ~w[word_idx] & 0x8888888888888888ULL;
            if (c_zeros != 0) {
                return (word_idx << 4) +
                       ((63 - __builtin_clzll(c_zeros)) >> 2) + 1;
            }
        }

        return 0;
    }

  public:
    bool is_unraceable() const { return offset <= 1; }
    bool is_empty() const { return is_unraceable(); }
    void clear() {
        offset = 0;
        data[0] = 0;
    }

    void append_left_child() {
        if (__builtin_expect(offset + 1 >= max_blocks, 0)) {
            fprintf(stderr, "[CilkPrace Error] Label length overflow!\n");
            exit(EXIT_FAILURE);
        }
        offset++;
        if ((offset & 1) == 0) {
            data[offset >> 1] &= 0xF0;
        } else {
            data[offset >> 1] &= 0x0F;
        }
    }

    void append_right_child() {
        if (__builtin_expect(offset + 1 >= max_blocks, 0)) {
            fprintf(stderr, "[CilkPrace Error] Label length overflow!\n");
            exit(EXIT_FAILURE);
        }
        offset++;
        if ((offset & 1) == 0) {
            data[offset >> 1] = (data[offset >> 1] & 0xF0) | 0x01;
        } else {
            data[offset >> 1] = (data[offset >> 1] & 0x0F) | 0x10;
        }
    }

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
            if (offset + 1 >= max_blocks) {
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
        if (__builtin_expect(is_empty() || rhs.is_empty(), 0))
            return false;

        size_t i = calc_matching_block_length(rhs);

        if (i == offset + 1 || i == rhs.offset + 1) {
            // One is a prefix of the other -> series
            return false;
        }

        // Fast-path: if block i is the start of a level
        if (i == 0 || (get_block(i - 1) & 8) == 0) {
            uint8_t mask = (i & 1) ? 0x10 : 0x01;
            return ((data[i >> 1] ^ rhs.data[i >> 1]) & mask) != 0;
        }

        // Find the start of the diverging level
        size_t level_start = find_level_start(i);
        uint8_t mask = (level_start & 1) ? 0x10 : 0x01;
        return ((data[level_start >> 1] ^ rhs.data[level_start >> 1]) & mask) != 0;
    }

    // Should fixup LCA range?
    __attribute__((always_inline))
    inline range_check range_relation(const os_label &rhs, const bool &is_range) const {
        if (__builtin_expect(rhs.is_empty(), 0)) {
            if (is_empty())
                return identical;
            return is_range ? within : synced;
        }
        if (__builtin_expect(is_empty(), 0)) {
            return is_range ? within : synced;
        }

        size_t i = calc_matching_block_length(rhs);

        if (__builtin_expect(i == offset + 1 && i == rhs.offset + 1, 0))
            return identical;

        if (i == rhs.offset + 1)
            return is_range ? within : synced;

        if (i == offset + 1)
            return is_range ? within : synced;

        if (i == 0 || (get_block(i - 1) & 8) == 0) {
            uint8_t mask = (i & 1) ? 0x10 : 0x01;
            if ((data[i >> 1] ^ rhs.data[i >> 1]) & mask)
                return parallel;
            return synced;
        }

        size_t level_start = find_level_start(i);
        uint8_t mask = (level_start & 1) ? 0x10 : 0x01;
        if ((data[level_start >> 1] ^ rhs.data[level_start >> 1]) & mask) {
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

        if (level_start == 0) {
            rhs.clear();
        } else {
            // Truncate rhs to the LCA continuation
            rhs.offset = level_start - 1;
        }
    }

#ifdef ENABLE_LABEL_PRINTING
    inline friend std::ostream &operator<<(std::ostream &os,
                                           const os_label &l) {
        std::vector<uint8_t> vec = l.to_vector();
        os << "Length: " << vec.size() << ", Label:";
        for (uint64_t v : vec)
            os << " " << v;
        return os;
    }
#endif
};

#endif // _OS_LABEL_LEB8_H
