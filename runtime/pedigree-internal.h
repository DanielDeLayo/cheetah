#ifndef _PEDIGREE_INTERNAL_H
#define _PEDIGREE_INTERNAL_H

#include "cilk-internal.h"
#include <cilk/cilk_api.h>
#include <cilk/os_label.h>
#include <cstdlib>
#include <cstring>

static const uint64_t DPRNG_PRIME = (uint64_t)(-59);
extern uint64_t *__pedigree_dprng_m_array;
extern uint64_t __pedigree_dprng_seed;

class __cilkrts_os_label_internal : public os_label {
public:
  uint16_t restore_idx;
  static constexpr int scan_max_low_offset_bytes = sizeof(data) - sizeof(uint64_t);

  inline void append_left_child() {
    int scan_low_offset_bytes = end_idx / 8;

    if (scan_low_offset_bytes > scan_max_low_offset_bytes) {
      scan_low_offset_bytes = scan_max_low_offset_bytes;
    }

    uint64_t* scan_low_addr = (uint64_t*)((uint8_t*)data + scan_low_offset_bytes);

    uint64_t scan_val = *scan_low_addr;
    uint64_t scan_low_idx = end_idx - scan_low_offset_bytes * 8;
    assert(scan_low_idx < 64);

    uint64_t scan_val_capped = scan_val | 1ull << scan_low_idx;
    int high_set_idx = 63 - __builtin_clzll(scan_val_capped);
    // We need to round up from high_set_idx to the start of the next S group.
    // -----..------..------.
    //      |v      |v      |
    //     .-----. .'----. .'.
    // P C S S P C S S P C S S
    // ^       ^       ^
    // '-------'-------'---- P bits should be 0, so they don't matter
    end_idx = ((high_set_idx + 2) | 3) + 1 + scan_low_offset_bytes * 8;

    assert(end_idx <= sizeof(data) * 8);
  }

  inline void start_new_frame() {
    restore_idx = end_idx;
  }

  inline void set_right_child() {
    assert(end_idx % 4 == 0);
    assert(end_idx > 0);
    uint64_t p_idx = end_idx - 1;
    data[p_idx / 64ull] |= 1ull << (p_idx % 64ull);
  }

  inline void restore_on_sync() {
    int scan_low_offset_bytes = restore_idx / 8;

    if (scan_low_offset_bytes > scan_max_low_offset_bytes) {
      scan_low_offset_bytes = scan_max_low_offset_bytes;
    }

    uint64_t* scan_low_addr = (uint64_t*)((uint8_t*)data + scan_low_offset_bytes);
    uint64_t scan_low_idx = restore_idx - scan_low_offset_bytes * 8;
    assert(scan_low_idx < 64);
    assert(scan_low_idx % 4 == 0);

    uint64_t scan_val = *scan_low_addr;
    uint64_t p_mask = 0x8888888888888888ull << scan_low_idx;

    // Clear continuations
    uint64_t conts = scan_val & p_mask;
    scan_val &= ~conts & (conts - 1);

    // Increment s value.
    // more significant p bits are set to propagate carry, then cleared
    // c bits are saved so they can be restored if they
    // participated in carry propagation.
    uint64_t carries = scan_val & (p_mask >> 1ull);
    scan_val |= p_mask;
    scan_val += 1 << scan_low_idx;
    scan_val &= ~p_mask;
    scan_val |= carries;

    *scan_low_addr = scan_val;

    uint8_t* clear_low_addr = (uint8_t*)(scan_low_addr + 1);

    unsigned clear_high_offset_bytes = end_idx / 8 + sizeof(uint64_t);
    if (clear_high_offset_bytes > sizeof(data)) {
      clear_high_offset_bytes = sizeof(data);
    }
    uint8_t* clear_high_addr = (uint8_t*)data + clear_high_offset_bytes;
    assert(clear_low_addr <= clear_high_addr);
    // TODO: can we tune this better?
    memset(clear_low_addr, 0, clear_high_addr - clear_low_addr);

    end_idx = restore_idx;
  }
};

typedef struct __pedigree_frame {
    __cilkrts_pedigree pedigree; // Fields for pedigrees.
    int64_t rank;
    uint64_t dprng_dotproduct;
    int64_t dprng_depth;
    __cilkrts_os_label_internal label;
} __pedigree_frame;

///////////////////////////////////////////////////////////////////////////
// Helper methods

static inline __attribute__((malloc)) __pedigree_frame *
push_pedigree_frame(__cilkrts_worker *w) {
#if ENABLE_EXTENSION
    return static_cast<__pedigree_frame*>
      (__cilkrts_push_ext_stack(w, sizeof(__pedigree_frame)));
#else
    return nullptr;
#endif
}

static inline void pop_pedigree_frame(__cilkrts_worker *w) {
#if ENABLE_EXTENSION
    __cilkrts_pop_ext_stack(w, sizeof(__pedigree_frame));
#endif
}

static inline uint64_t __cilkrts_dprng_swap_halves(uint64_t x) {
  return (x >> (4 * sizeof(uint64_t))) | (x << (4 * sizeof(uint64_t)));
}

static inline uint64_t __cilkrts_dprng_mix(uint64_t x) {
  for (int i = 0; i < 4; i++) {
      x = x * (2*x+1);
      x = __cilkrts_dprng_swap_halves(x);
  }
  return x;
}

static inline uint64_t __cilkrts_dprng_mix_mod_p(uint64_t x) {
  x = __cilkrts_dprng_mix(x);
  return x - (DPRNG_PRIME & -(x >= DPRNG_PRIME));
}

static inline uint64_t __cilkrts_dprng_sum_mod_p(uint64_t a, uint64_t b) {
    uint64_t z = a + b;
    if ((z < a) || (z >= DPRNG_PRIME)) {
        z -= DPRNG_PRIME;
    }
    return z;
}

// Helper method to advance the pedigree and dprng states.
static inline __attribute__((always_inline)) __pedigree_frame *
bump_worker_rank(void) {
#if ENABLE_EXTENSION
    __pedigree_frame *frame = (__pedigree_frame *)(__cilkrts_get_extension());
    frame->rank++;
    frame->dprng_dotproduct = __cilkrts_dprng_sum_mod_p(
        frame->dprng_dotproduct, __pedigree_dprng_m_array[frame->dprng_depth]);
    return frame;
#else
    return nullptr;
#endif
}

#endif // _PEDIGREE_INTERNAL_H
