#ifndef _ATOMIC_SEQLOCK_H
#define _ATOMIC_SEQLOCK_H

#pragma GCC visibility push(default)

#include <atomic>
#include <cstdint>
#include <sched.h>

#if (defined(SERIAL_TOOL) && SERIAL_TOOL) || defined(SERIAL) || defined(CILK_SERIAL)

class atomic_seqlock {
  public:
    inline void begin_write() {}
    inline void end_write() {}
    inline uint32_t begin_read() { return 0; }
    inline bool read_was_safe(uint32_t old_seq) const { return true; }
};

#else

class atomic_seqlock {
    // We store a has_writer boolean in the low-order bit
    std::atomic<uint32_t> seq{0};

  public:
    void begin_write() {
        while (true) {
            uint32_t s = seq.load(std::memory_order_relaxed);
            if ((s & 1) == 0) { // no writer
                if (seq.compare_exchange_weak(s, s + 1, std::memory_order_acquire, std::memory_order_relaxed)) {
                    break;
                }
            }
            // Spin on read to avoid thrashing
            while (seq.load(std::memory_order_relaxed) & 1) {
                #if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
                #elif defined(__aarch64__)
                __builtin_arm_yield();
                #endif
            }
        }
    }

    void end_write() {
        // Our write is visibile to us-- we can just load-increment weakly
        seq.store(seq.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    inline uint32_t begin_read() {
        // This is a weak operation; so, we can read with just atomicity
        uint32_t ret = seq.load(std::memory_order_relaxed);
        while (__builtin_expect(ret & 1, 0)) {
            #if defined(__aarch64__)
            __builtin_arm_yield();
            #elif defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
            #endif
            ret = seq.load(std::memory_order_relaxed);
        }
        // and declare that we've acquired a resource (barrier)
        std::atomic_thread_fence(std::memory_order_acquire);
        return ret;
    }

    bool read_was_safe(uint32_t old_seq) const { 
        // Use a fence to ensure we get updated info
        std::atomic_thread_fence(std::memory_order_acquire);
        // Relaxed is fine here-- if it was fine at the fence, then it's fine here.
        return seq.load(std::memory_order_relaxed) == old_seq; 
    }
};

#endif // !SERIAL_TOOL

#pragma GCC visibility pop

#endif // _ATOMIC_SEQLOCK_H
