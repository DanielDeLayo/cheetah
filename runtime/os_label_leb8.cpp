#include <cilk/os_label.h>

void os_label::restore_on_sync(uint16_t restore_point) {
    if (offset == restore_point && offset == 0 && get_block(0) == 0)
        return;

    // Reset offset directly to restore_point
    offset = restore_point;

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

bool os_label::is_identical_slow(const os_label &rhs) const {
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

bool os_label::is_prefix_slow(const os_label &full) const {
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

range_check os_label::range_relation(const os_label &rhs, const bool &is_range) const {
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
