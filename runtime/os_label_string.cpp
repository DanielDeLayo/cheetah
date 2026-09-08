#include <cilk/os_label.h>

void os_label::restore_on_sync(uint8_t conts) {
    if (conts == 0)
        return;
    // Clear left child
    for (; conts > 0; conts--)
        labels[offset--] = 0;
    // Increment Parent
    check_label_value_overflow(labels[offset], 2, UINT8_MAX);
    labels[offset] += 2;
}

range_check os_label::range_relation(const os_label &rhs,
                                     const bool &is_range) const {
    size_t num_matches = calc_matching_prefix_length(rhs);

    // Case 1: same
    if (num_matches == offset + 1 && offset == rhs.offset)
        return identical;
    // Case 2: descendent
    if (num_matches == offset + 1 || num_matches == rhs.offset + 1)
        return is_range ? within : synced;
    // Case 3: parallel
    if (is_parallel(rhs))
        return parallel;
    return synced;
}
