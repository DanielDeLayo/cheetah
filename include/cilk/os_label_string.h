#ifndef _OS_LABEL_STRING_H
#define _OS_LABEL_STRING_H

#pragma GCC visibility push(default)

#include <cstddef>
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <ostream>

constexpr size_t __code_max_length = 255 * 8;
constexpr size_t __code_nbytes = __code_max_length / 8;

using byte_string = uint8_t[__code_nbytes];

// Represents your status relative to the other label.
// Either you've synced since them, you're within the range, or your're simply
// in parallel
enum range_check { synced, within, parallel, identical };

static inline void check_label_value_overflow(uint64_t current_val,
                                              uint64_t inc, uint64_t max_val) {
    if (current_val + inc < current_val) {
        fprintf(stderr,
                "[CilkPrace Error] Label value integer overflow: %llu + %llu "
                "wrapped around!\n",
                (unsigned long long)current_val, (unsigned long long)inc);
        exit(EXIT_FAILURE);
    }
    if (current_val + inc > max_val) {
        fprintf(stderr,
                "[CilkPrace Error] Label value overflow: %llu + %llu exceeds "
                "max value (%llu)!\n",
                (unsigned long long)current_val, (unsigned long long)inc,
                (unsigned long long)max_val);
        exit(EXIT_FAILURE);
    }
}

static inline void check_label_length_overflow(size_t current_len,
                                               size_t additional_len,
                                               size_t max_len) {
    if (current_len + additional_len < current_len) {
        fprintf(stderr,
                "[CilkPrace Error] Label length size_t overflow: %zu + %zu "
                "wrapped around!\n",
                current_len, additional_len);
        exit(EXIT_FAILURE);
    }
    if (current_len + additional_len >= max_len) {
        fprintf(stderr,
                "[CilkPrace Error] Label length overflow: current length %zu + "
                "added %zu exceeds max capacity (%zu)!\n",
                current_len, additional_len, max_len);
        exit(EXIT_FAILURE);
    }
}

struct os_label {
    byte_string labels = {0};
    uint8_t offset = 0;

  public:
    // Encoding: offset-span labeling DOI:10.1145/125826.125861
    // TODO: 2 value bits, 1 child-direction bit, and 1 continuation bit
    // TODO: Gray code stuff? Right align, grow left, etc.
    bool is_unraceable() const {
        return offset == 0 && labels[0] == 0;
    }
    bool is_empty() const {
        return is_unraceable();
    }
    void clear() {
        offset = 0;
        labels[0] = 0;
    }

    void append_left_child() {
        check_label_length_overflow(offset, 1, __code_nbytes);
        labels[++offset] = 0;
    }

    bool is_serial() const {
        return offset == 0;
    }

    bool is_identical(const os_label &rhs) const {
        if (offset != rhs.offset)
            return false;
        return memcmp(labels, rhs.labels, offset + 1) == 0;
    }

    void copy_from(const os_label &src) {
        offset = src.offset;
        memcpy(labels, src.labels, src.offset + 1);
    }

    void append_right_child() {
        check_label_length_overflow(offset, 1, __code_nbytes);
        labels[++offset] = 1;
    }

    // This is always called with the parent's frame. We only have to undo
    // conts. But there's a problem because this is called without a promise
    // that it's real. Just the keyword. 
    // We have to reset and store conts every time we enter a new cilked function that may spawn.
    void restore_on_sync(uint8_t conts);

    size_t inline calc_matching_prefix_length(const os_label &rhs) const {
        size_t min_offset = offset < rhs.offset ? offset : rhs.offset;
        size_t min_len = min_offset + 1;
        size_t num_matches = 0;

        // Compare in 8-byte chunks
        while (num_matches + 8 <= min_len) {
            uint64_t v1, v2;
            __builtin_memcpy(&v1, &labels[num_matches], sizeof(uint64_t));
            __builtin_memcpy(&v2, &rhs.labels[num_matches], sizeof(uint64_t));
            
            if (v1 == v2) {
                num_matches += 8;
            } else {
                uint64_t diff = v1 ^ v2;
                return num_matches + (__builtin_ctzll(diff) / 8);
            }
        }

        // Find the exact mismatch point in the remaining bytes (at most 7 bytes)
        for (; num_matches < min_len; num_matches++) {
            if (labels[num_matches] != rhs.labels[num_matches])
                break;
        }
        
        return num_matches;
    }

    // Returns true if in parallel
    bool is_parallel(const os_label &rhs) const {
        // 4 cases:
        // 1. No LCA-- series
        // 2. LCA is one of them-- parent child, series
        // 3. LCA, both have longer labels. series or parallel depends on parity
        // 4. Identical labels-- series
        size_t num_matches = calc_matching_prefix_length(rhs);

        // Cases 1, 2, and 4. Offsets are indices of the last elements
        if (num_matches == 0 || num_matches == offset + 1 ||
            num_matches == rhs.offset + 1)
            return false;

        // Case 3: The tricky one.

        // Read the first label that differs. Bounds checking handled above.
        // In the first difference, if the parity is the same, then it's a
        // parent-child relationship. Otherwise, it's two children (and in
        // parallel).
        uint8_t left = labels[num_matches];
        uint8_t right = rhs.labels[num_matches];

        // Parity check
        return left % 2 != right % 2;
    }

    // Should fixup LCA range?
    range_check range_relation(const os_label &rhs,
                               const bool &is_range) const;


    // Return a clean vector of the current label values
    std::vector<uint8_t> to_vector() const {
        std::vector<uint8_t> vec;
        vec.reserve(offset + 1);
        for (size_t i = 0; i <= offset; i++) {
            vec.push_back(labels[i]);
        }
        return vec;
    }

    // Fixup parallel LCA range
    void expand_parallel_range(os_label &rhs) const {
        // This function handles a single case.
        // 1. We are in parallel, but outside the range.
        // That is, we just shrink the prefix to the LCA
        size_t num_matches = calc_matching_prefix_length(rhs);

        for (size_t i = num_matches; i <= rhs.offset; i++) {
            rhs.labels[i] = 0;
        }
        rhs.offset = num_matches;
    }

    inline friend std::ostream &operator<<(std::ostream &os, const os_label &l) {
        os << "Length: " << l.offset + 1 << ", Label:";
        for (size_t i = 0; i <= l.offset; i++)
            os << " " << (uint64_t)l.labels[i]; // (uint8s are unfortunately chars)
        return os;
    }
};

#pragma GCC visibility pop

#endif // _OS_LABEL_STRING_H
