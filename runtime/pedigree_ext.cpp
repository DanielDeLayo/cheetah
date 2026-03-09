#include "pedigree-internal.h"

// Pedigree-extension code, included in the runtime as part of the bitcode file.

void __cilkrts_extend_spawn(__cilkrts_worker *w, void **parent_extension,
                            void **child_extension) {

    //__pedigree_frame *parent_frame2 = (__pedigree_frame *)(*parent_extension);
    //tstd::cout << "OVERWRITE: " << parent_frame2->label << std::endl;

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

    //std::cout << "PARENT: " << parent_frame->label << std::endl;
    //std::cout << "CHILD: " << frame->label << std::endl;

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
    //parent_frame->label.join_left_child();

    //std::cout << "PARENT2: " << parent_frame->label << std::endl;
    //std::cout << "CHILD2: " << frame->label << std::endl;
}

// TODO: Remove extension parameter?
void __cilkrts_extend_return_from_spawn(__cilkrts_worker *w,
                                        [[maybe_unused]] void **extension) {
    
    //std::cout << "POP: " << ((__pedigree_frame*)(*extension))->label << std::endl;
    // Free the pedigree frame.    
    pop_pedigree_frame(w);
                                           
}

void __cilkrts_extend_leave_frame(__cilkrts_worker *w,
                                        [[maybe_unused]] void **extension) {
    // Free the pedigree frame.    
    __pedigree_frame *frame = (__pedigree_frame *)(*extension);
    //frame->label.join_left_child();
    //std::cout << "LEAVE: " << ((__pedigree_frame*)(*extension))->label << std::endl;
    //std::cout << frame->label << std::endl;

    //__cilkrts_extend_label_sync(extension);

}

void __cilkrts_extend_enter_frame(__cilkrts_worker *w,
                                        [[maybe_unused]] void **extension) {
    // Free the pedigree frame.    
    __pedigree_frame *frame = (__pedigree_frame *)(*extension);
    //frame->label.join_left_child();
    //std::cout << "ENTER: " << ((__pedigree_frame*)(*extension))->label << std::endl;
    //std::cout << frame->label << std::endl;
}

void __cilkrts_extend_landingpad(__cilkrts_worker *w,
                                        [[maybe_unused]] void **extension) {
    // Free the pedigree frame.    
    __pedigree_frame *frame = (__pedigree_frame *)(*extension);
    //frame->label.join_left_child();
    //std::cout << "LAND: " << ((__pedigree_frame*)(*extension))->label << std::endl;
    //std::cout << frame->label << std::endl;
}

void __cilkrts_extend_enter_frame_helper(__cilkrts_worker *w,
                                        [[maybe_unused]] void **extension) {
    // Free the pedigree frame.    
    __pedigree_frame *frame = (__pedigree_frame *)(*extension);
    //frame->label.join_left_child();
    //std::cout << "ENTER HELPER: " << ((__pedigree_frame*)(*extension))->label << std::endl;
    //std::cout << frame->label << std::endl;
}



void __cilkrts_extend_sync(void **extension) {
    // Update the rank and dprng_dotproduct.
    __pedigree_frame *frame = (__pedigree_frame *)(*extension);
    frame->rank++;
    frame->dprng_dotproduct = __cilkrts_dprng_sum_mod_p(
        frame->dprng_dotproduct, __pedigree_dprng_m_array[frame->dprng_depth]);
    frame->label.restore_on_sync();
    //std::cout << "SYNC1: " << ((__pedigree_frame*)(*extension))->label << std::endl;
}

void __cilkrts_extend_label_sync(void **extension) {
    // Update the rank and dprng_dotproduct.
    __pedigree_frame *frame = (__pedigree_frame *)(*extension);
    //frame->label.restore_on_sync();
    //std::cout << "SYNC2: " << ((__pedigree_frame*)(*extension))->label << std::endl;
}

