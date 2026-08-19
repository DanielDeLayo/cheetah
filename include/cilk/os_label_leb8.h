#ifndef _OS_LABEL_LEB8_H
#define _OS_LABEL_LEB8_H

#include <cstddef>
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <ostream>

// Represents your status relative to the other label.
// Either you've synced since them, you're within the range, or your're simply
// in parallel
enum range_check { synced, within, parallel, identical };

struct os_label {
    // LEB8 encoded bit array. Each byte holds two 4-bit blocks.
    // Block format: [C (1 bit), P (3 bits)] where C is continuation.
    uint8_t data[64] = {0};
    uint16_t offset = 0; // Index of the last block. 0-initialized means 1 block at index 0.

    inline uint8_t get_block(size_t index) const {
        //TODO: Consider swapping to ternary operator
        if (index % 2 == 0) {
            return data[index / 2] & 0x0F;
        } else {
            return data[index / 2] >> 4;
        }
    }

    inline void set_block(size_t index, uint8_t block) {
        if (index % 2 == 0) {
            data[index / 2] = (data[index / 2] & 0xF0) | (block & 0x0F);
        } else {
            data[index / 2] = (data[index / 2] & 0x0F) | ((block & 0x0F) << 4);
        }
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
    bool is_empty() const {
        return offset == 0 && data[0] == 0;
    }

    __attribute__((always_inline)) void append_left_child() {
        push_level(0);
    }

    __attribute__((always_inline)) void append_right_child() {
        push_level(1);
    }

    __attribute__((always_inline)) void restore_on_sync(uint8_t conts) {
        if (conts == 0)
            return;
            
        // Drop 'conts' levels (the children)
        while (conts > 0 && offset > 0) {
            offset--;
            while (offset > 0 && (get_block(offset) & 8) != 0) {
                offset--;
            }
            conts--;
        }

        // Find the start block of the parent (now the last level)
        size_t parent_start = offset;
        while (parent_start > 0 && (get_block(parent_start - 1) & 8) != 0) {
            parent_start--;
        }

        // In-place addition of 2 to the parent's value (LEB8 ripple-carry)
        uint8_t carry = 2;
        for (size_t i = parent_start; i <= offset; i++) {
            uint8_t block = get_block(i);
            uint8_t payload = block & 7;
            uint8_t continuation = block & 8;
            
            uint8_t sum = payload + carry;
            set_block(i, continuation | (sum & 7));
            
            carry = sum >> 3;
            if (carry == 0) break;
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
        if (is_empty() || rhs.is_empty()) return false;

        size_t min_offset = offset < rhs.offset ? offset : rhs.offset;
        size_t min_blocks = min_offset + 1;
        size_t i = 0;
        while (i < min_blocks && get_block(i) == rhs.get_block(i)) {
            i++;
        }
        
        if (i == offset + 1 || i == rhs.offset + 1) {
            // One is a prefix of the other -> series
            return false;
        }
        
        // Find the start of the diverging level
        size_t level_start = i;
        while (level_start > 0 && (get_block(level_start - 1) & 8) != 0) {
            level_start--;
        }
        
        uint8_t my_dir = get_block(level_start) & 1;
        uint8_t rhs_dir = rhs.get_block(level_start) & 1;
        
        return my_dir != rhs_dir;
    }

    // Should fixup LCA range?
    range_check range_relation(const os_label &rhs,
                               const bool &is_range) const {
        size_t min_offset = offset < rhs.offset ? offset : rhs.offset;
        size_t min_blocks = min_offset + 1;
        size_t i = 0;
        while (i < min_blocks && get_block(i) == rhs.get_block(i)) {
            i++;
        }
        
        if (i == offset + 1 && i == rhs.offset + 1)
            return identical;
            
        if (i == rhs.offset + 1) // rhs is prefix of this -> this is descendent
            return is_range ? within : synced;
            
        if (i == offset + 1) // this is prefix of rhs -> this is ancestor
            return is_range ? within : synced;

        // Check if parallel
        size_t level_start = i;
        while (level_start > 0 && (get_block(level_start - 1) & 8) != 0) {
            level_start--;
        }
        
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
                if ((block & 8) == 0) break;
            }
            vec.push_back(V);
        }
        return vec;
    }

    // Fixup parallel LCA range
    void expand_parallel_range(os_label &rhs) const {
        size_t min_offset = offset < rhs.offset ? offset : rhs.offset;
        size_t min_blocks = min_offset + 1;
        size_t i = 0;
        while (i < min_blocks && get_block(i) == rhs.get_block(i)) {
            i++;
        }
        
        size_t level_start = i;
        while (level_start > 0 && (get_block(level_start - 1) & 8) != 0) {
            level_start--;
        }
        
        // Truncate rhs to the LCA
        rhs.offset = level_start > 0 ? level_start - 1 : 0;
        if (level_start == 0) {
            rhs.set_block(0, 0);
        } else {
            // Expand to the continuation (left child, which is represented by pushing 0 payload)
            // Wait, if LCA ends at level_start - 1, then pushing 0 means setting the next block
            rhs.offset = level_start;
            rhs.set_block(rhs.offset, 0);
        }
    }

    inline friend std::ostream &operator<<(std::ostream &os, const os_label &l) {
        std::vector<uint8_t> vec = l.to_vector();
        os << "Length: " << vec.size() << ", Label:";
        for (uint64_t v : vec)
            os << " " << v;
        return os;
    }
};

#endif // _OS_LABEL_LEB8_H
