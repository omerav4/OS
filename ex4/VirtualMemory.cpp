#include <cmath>
#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include <iostream>


///------------------------------------------- macros -------------------------------------------------------
#define SUCCESS 1
#define FAIL 0
#define LAST_PAGE 1
#define PHYSICAL_LEVEL 1

///------------------------------------------- structs -------------------------------------------------------
/**
 * A struct which represents a page in the virtual memory.
 * Has an address, a former and next pages, a row and a caller table
 */
struct page{
    uint64_t caller_table;
    word_t address;
    page* former;
    page* next;
    uint64_t row;
};

///---------------------------------------- address manipulations ----------------------------------------------------
/**
 * Returns the offset of the given address
 */
uint64_t get_offset(uint64_t address){
    uint64_t offset_mask = (1ULL<< OFFSET_WIDTH) - 1;
    return address & offset_mask;
}

/**
 * Returns the address of the given full address without the offeset
 */
uint64_t get_address_without_offset(uint64_t address){
    return address >> (OFFSET_WIDTH);
}

/**
 * Returns the next address of the given full address, according to the given level
 */
uint64_t get_next_address(uint64_t address, uint64_t level){
    address = address >> (level*OFFSET_WIDTH);
    return get_offset(address);
}

///---------------------------------------- helper functions ----------------------------------------------------

/**
 * Resets the given frame by changing its rows to 0.
 * @param frame_index
 */
void reset_frame(uint64_t frame_index){
    for (uint64_t i = 0;  i < PAGE_SIZE; i++){
        PMwrite(frame_index * PAGE_SIZE + i, 0);
    }
}

/**
 * Calculates the cyclic distance of the given page number
 */
uint64_t cyclic_dist(uint64_t origin_address, uint64_t page_num){
    uint64_t dist;
    if ((origin_address >> OFFSET_WIDTH) - page_num <= 0){dist = page_num - (origin_address >> OFFSET_WIDTH);}
    else{dist = (origin_address >> OFFSET_WIDTH) - page_num;}
    if(dist > NUM_PAGES - dist){dist = NUM_PAGES - dist;}
    return dist;
}

void initialize_next_node(page* node){
    page next = {node->caller_table, 0, nullptr, nullptr, 0};
    node->next = &next;
}

/**
 * Transverse the tree
 * For each
 * @param node
 * @param cur_level
 * @param max_frame_index
 * @param original_address
 * @param available_frame
 * @param frame_to_evict
 * @param max_dist
 */
void transverse_tree(page* node, uint64_t cur_level, uint64_t* max_frame_index, uint64_t original_address,
                     page* available_frame, page* frame_to_evict, uint64_t* max_dist){
    // base case; if we are in physical memory, calculate cyclic dist
    if(cur_level > TABLES_DEPTH){
        uint64_t cur_dist = cyclic_dist( original_address,node->address);
        if( cur_dist > *max_dist){  // update max_dist and page_to_evict
            *max_dist = cur_dist;
            frame_to_evict = node;
        }
        return;
    }

    // otherwise, we will go through the current node's children (means the frame's rows)
    // if a child exists (means there is row != 0), then the current frame is not empty
    // in addition, we will update the max_frame_index to keep the maximum frame index
    bool is_empty = true;
    for (uint64_t row = 0; row < PAGE_SIZE; ++row){   // recursive call
        initialize_next_node(node);
        std::cout << "before PMread 2\n ";
        PMread(node->address * PAGE_SIZE + row, &(node->next->address));

        if (node->next->address != 0) {  // page is full, continue searching in next level
            is_empty = false;
            // update max_frame_index and root
            if (node->next->address > *max_frame_index){ *max_frame_index = node->next->address;}
            node->next->former = node;
            // node->former->next->address = node->address;
            node->row = row;
            // call next level search
            std::cout << "before another transverse tree\n";
            transverse_tree(node->next, cur_level++, max_frame_index,
                            original_address, available_frame, frame_to_evict, max_dist);
        }
    }
    if (is_empty){
            available_frame = node;
        }
}

/**
 * Unlinks the current node from its child by changing its value in the physical memory to 0.
 */
void unlink(page* node){
    uint64_t address = node->former->address * PAGE_SIZE + node->row;
    PMwrite(address, 0);
}

/**
 * Evicts the given page
 * @return
 */
word_t evict(page* frame_to_evict){
    word_t frame;
    PMread(frame_to_evict->address, &frame);
    PMevict(frame, get_address_without_offset(frame_to_evict->address));
    unlink(frame_to_evict);
}


/**
 * Finds a frame to put the given page in or evicts another page if necessary
 */
uint64_t find_frame(page* root){
    page available_frame = {root->caller_table, 0, nullptr, nullptr, 0};
    page frame_to_evict = {root->caller_table, 0, nullptr, nullptr, 0};
    uint64_t max_frame_index = 0;
    uint64_t max_dist = 0;

    // transverses the tree in order to find the max frame index and if there is an empty frame (frame with rows = 0).
    // we also checks which page to evict if it will be necessary
    transverse_tree(root, 0, &max_frame_index, root->address,
                    &available_frame, &frame_to_evict,&max_dist);

    // option 1: we find an empty frame (frame with rows = 0)
    if (available_frame.address != 0 && available_frame.address != root->caller_table){
        unlink(&available_frame);
        return available_frame.address;
    }

    // option 2: if there is an unused frame
    if (max_frame_index + 1 < NUM_FRAMES){return (max_frame_index + 1);}

    // option 3: otherwise, evict a page and returns its frame
    evict(&frame_to_evict);     // TODO verify already evicted frames
    return frame_to_evict.address;
}

/**
 * Gets the page address in the physical memory (means frame address) of the given full address
 * @param address- the full address
 * @return the page address in the physical memory
 */
word_t get_page_address(uint64_t address){
    std::cout << "get page address start\n ";
    word_t current_address = 0;
    for (uint64_t level = TABLES_DEPTH; level > 0 ; level--){
        // Reads on each iteration the next level of the given address
        word_t caller_address = current_address;
        uint64_t next_address = get_next_address(address, level);
        std::cout << "before PMread 1\n ";
        PMread(current_address * PAGE_SIZE + next_address, &current_address);

        // if we get to undefined table (address = 0), then find a free frame, resets him (create a table) and links it
        // to the current page
        if (current_address == 0){
            page root = {caller_address, 0, nullptr, nullptr, 0}; // TODO change values?
            word_t frame = find_frame(&root);  // find a relevant frame
            std::cout << "frame " << frame << "\n ";

            if (level == PHYSICAL_LEVEL){ PMrestore(frame,get_address_without_offset(address));}
            reset_frame(frame);
            PMwrite(current_address * PAGE_SIZE + next_address, frame); // create the link between the page and the frame
            current_address = frame;
        }
    }
    current_address = current_address * PAGE_SIZE + get_offset(address);
    return current_address;
}

///------------------------------------------- library -------------------------------------------------------
void VMinitialize(){
    reset_frame(0);
}

int VMread(uint64_t virtualAddress, word_t* value){
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE){return FAIL;}
    PMread(get_page_address(virtualAddress), value);
    return SUCCESS;
}

int VMwrite(uint64_t virtualAddress, word_t value){
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE){return FAIL;}
    PMwrite(get_page_address(virtualAddress), value);
    return SUCCESS;
}
