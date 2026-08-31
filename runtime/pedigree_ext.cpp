#include "pedigree-internal.h"
#include "frame.h"
#include "worker.h"

// Pedigree-extension code, included in the runtime as part of the bitcode file.

void __cilkrts_extend_spawn(__cilkrts_worker *w, void **parent_extension,
                            void **child_extension,
                            __cilkrts_stack_frame *parent_sf) {
    // Copy the child extension into the parent, and create a new
    // __pedigree_frame for the child.
    *parent_extension = *child_extension;

    // Get a new pedigree frame for the child extension.
    __pedigree_frame *frame = push_pedigree_frame(w);
    *child_extension = frame;

    // Initialize the new frame.
    __pedigree_frame *parent_frame = (__pedigree_frame *)(*parent_extension);
    // Copy the parent's rank into the child frame's pedigree.rank.
    frame->pedigree.rank = parent_frame->rank;
    // Append the child frame's pedigree onto the linked list.
    frame->pedigree.parent = &(parent_frame->pedigree);
    // Initialize the child frame's rank to 0.
    frame->rank = 0;

    // Increment the dprng_depth in the child frame.
    frame->dprng_depth = parent_frame->dprng_depth + 1;
    // Update the child frame's dprng_dotproduct.
    uint64_t parent_dprng_dotproduct = parent_frame->dprng_dotproduct;
    frame->dprng_dotproduct = __cilkrts_dprng_sum_mod_p(
        parent_dprng_dotproduct, __pedigree_dprng_m_array[frame->dprng_depth]);

    // Update the rank and dprng_dotproduct in the parent frame.
    parent_frame->rank++;
    parent_frame->dprng_dotproduct = __cilkrts_dprng_sum_mod_p(
        parent_dprng_dotproduct,
        __pedigree_dprng_m_array[parent_frame->dprng_depth]);

    // Update the labels in the parent and child frames.
    frame->label = parent_frame->label;
    frame->label.append_right_child();
    parent_frame->label.append_left_child();
    
    // Increment the conts counter in the parent's stack frame!
    if (parent_sf) {
        uint8_t conts = __cilkrts_get_conts(parent_sf);
        __cilkrts_set_conts(parent_sf, conts + 1);
    } else {
    }
}

// TODO: Remove extension parameter?
void __cilkrts_extend_return_from_spawn(__cilkrts_worker *w,
                                        [[maybe_unused]] void **extension) {
    
    // Free the pedigree frame.    
    pop_pedigree_frame(w);
                                           
}

void __cilkrts_restore_os_label_on_sync(void) noexcept {
    // The current stack frame is the one performing the sync
    cilk_fiber *fh = __cilkrts_tls.fh;
    __cilkrts_stack_frame *sync_sf = fh->current_stack_frame;
    uint8_t conts = __cilkrts_get_conts(sync_sf);
    
    __pedigree_frame *frame = (__pedigree_frame *)(__cilkrts_get_extension());
    if (!frame) return;
    
    // Restore using only the spawns issued by this exact function
    frame->label.restore_on_sync(conts);
    
    // Reset conts to 0 so subsequent syncs in the same function don't underflow the offset!
    __cilkrts_set_conts(sync_sf, 0);
}


void __cilkrts_extend_sync(void **extension) {
    // Restore the OS label for this sync
    __cilkrts_restore_os_label_on_sync();
    
    // Update the rank and dprng_dotproduct.
    __pedigree_frame *frame = (__pedigree_frame *)(*extension);
    frame->rank++;
    frame->dprng_dotproduct = __cilkrts_dprng_sum_mod_p(
        frame->dprng_dotproduct, __pedigree_dprng_m_array[frame->dprng_depth]);
}

extern "C"
__CILKRTS_STRAND_PURE
const os_label *__cilkrts_get_current_os_label(void) noexcept __CILKRTS_PRESERVE_MOST {
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    if (__builtin_expect(!w || !w->extension, 0))
        return nullptr;
    return &((__pedigree_frame *)w->extension)->label;
}

