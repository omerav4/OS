
#include "VirtualMemory.h"
#include "PhysicalMemory.h"


///------------------------------------------- macros -------------------------------------------------------
#define SUCCESS 1
#define FAIL 0
#define PHYSICAL_LEVEL 1

///------------------------------------------- structs -------------------------------------------------------
/**
 * A struct which represents a page in the virtual memory.
 * Has an address, a former and next pages, a row and a caller table
 */
struct page{
    word_t caller_table;
    word_t address;
    word_t former_add;
    page* next;
    uint64_t row;
    uint64_t page;
    uint64_t level;
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
 * Returns the address of the given full address without the offset
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

/**
 * returns the physical address of the given frame and row
 */
uint64_t get_physical_address(uint64_t frame, uint64_t row){
    return frame * PAGE_SIZE + row;
}

/**
 * returns the next page to consider
 */
uint64_t get_next_page(uint64_t page, uint64_t row){
    return (page << OFFSET_WIDTH) + row;
}

///---------------------------------------- helper functions ----------------------------------------------------

/**
 * Resets the given frame by changing its rows to 0.
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
    if (origin_address - page_num <= 0){dist = page_num - origin_address;}
    else{dist = origin_address - page_num;}
    if(dist > NUM_PAGES - dist){dist = NUM_PAGES - dist;}
    return dist;
}

/**
 * Transverses the tree
 */
void transverse_tree(page* node, word_t* max_frame_index,
                     page* available_frame, page* frame_to_evict, uint64_t* max_dist, word_t requested_page){

    // base case; if we are in physical memory, calculate cyclic dist
    if(node->level >= TABLES_DEPTH){
        uint64_t cur_dist = cyclic_dist( requested_page, node->page);
        if( cur_dist > *max_dist){  // update max_dist and page_to_evict
            *max_dist = cur_dist;
            page evicted = {node->caller_table, node->address,
                            node->former_add, node->next, node->row, node->page};
            *frame_to_evict = evicted;
        }
        return;
    }

    // otherwise, we will go through the current node's children (means the frame's rows)
    // if a child exists (means there is row != 0), then the current frame is not empty
    // in addition, we will update the max_frame_index to keep the maximum frame index
    bool is_empty = true;
    for (uint64_t row = 0; row < PAGE_SIZE; ++row){

        // initialize next node and find next address
        page next = {node->caller_table, 0,
                     node->address, nullptr, 0, 0, node->level+1};
        node->next = &next;
        PMread(get_physical_address(node->address, row), &(node->next->address));

        if (node->next->address != 0) {  // page is full, continue searching in next level
            is_empty = false;
            // update max_frame_index and root
            if (node->next->address > *max_frame_index){ *max_frame_index = node->next->address;}

            // update next
            node->next->row = row;
            node->next->page = get_next_page(node->page, row);

            // call next level search
            transverse_tree(node->next, max_frame_index, available_frame,
                            frame_to_evict, max_dist, requested_page);
        }
    }

    // check if available and update pointer to available frame
    if (is_empty&& node->address!=node->caller_table){
        page available = {node->caller_table, node->address,
                          node->former_add,node->next, node->row, node->page, node->level};
        *available_frame = available;
    }

}

/**
 * Unlinks the current node from its child by changing its value in the physical memory to 0.
 */
void unlink(page* node){
    uint64_t address = get_physical_address(node->former_add, node->row);
    PMwrite(address, 0);
}

/**
 * Evicts the given page
 */
void evict(page* frame_to_evict, uint_fast64_t page_to_evict){
    unlink(frame_to_evict);
    PMevict(frame_to_evict->address, page_to_evict);
}


/**
 * Finds a frame to put the given page in or evicts another page if necessary
 */
uint64_t find_frame(page* root, word_t requested_page){
    page available_frame = {root->caller_table, 0, 0, nullptr, 0, 0};
    page frame_to_evict = {root->caller_table, 0, 0, nullptr, 0, 0};
    word_t max_frame_index = 0;
    uint64_t max_dist = 0;

    // transverses the tree in order to find the max frame index and if there is an empty frame (frame with rows = 0).
    // also checks which page to evict if it will be necessary
    transverse_tree(root, &max_frame_index,
                    &available_frame, &frame_to_evict,&max_dist, requested_page);

    // option 1: we find an empty frame (frame with rows = 0)
    if (available_frame.address != 0 && available_frame.address != root->caller_table){
        unlink(&available_frame);
        return available_frame.address;
    }

    // option 2: if there is an unused frame
    if (max_frame_index + 1 < NUM_FRAMES){return (max_frame_index + 1);}

    // option 3: otherwise, evict a page and returns its frame
    evict(&frame_to_evict, frame_to_evict.page);
    return frame_to_evict.address;
}

/**
 * Gets the page address in the physical memory (means frame address) of the given full address
 * @param address- the full address
 * @return the page address in the physical memory
 */
word_t get_page_address(uint64_t address){
    word_t current_address = 0;
    auto requested_page = (word_t) get_address_without_offset(address);
    for (uint64_t level = TABLES_DEPTH; level > 0 ; level--){

        // Reads on each iteration the next level of the given address
        word_t caller_address = current_address;
        uint64_t next_address = get_next_address(address, level);
        PMread(get_physical_address(current_address, next_address), &current_address);

        // if we get to undefined table (address = 0), then find a free frame, resets him (create a table) and links it
        // to the current page
        if (current_address == 0){
            page root = {caller_address, 0, 0,nullptr, 0, 0, 0};
            auto frame = (word_t) find_frame(&root, requested_page);  // find a relevant frame

            if (level == PHYSICAL_LEVEL){PMrestore(frame,get_address_without_offset(address));}
            else{reset_frame(frame);}

            PMwrite(get_physical_address(caller_address, next_address), frame); // create the link between the page and the frame
            current_address = frame;
        }
    }

    return (word_t)get_physical_address(current_address, get_offset(address));
}

///------------------------------------------- library -------------------------------------------------------
void VMinitialize(){
    reset_frame(0);
}

int VMread(uint64_t virtualAddress, word_t* value){
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE){return FAIL;}
    uint64_t address = get_page_address(virtualAddress);
    PMread(address, value);
    return SUCCESS;
}

int VMwrite(uint64_t virtualAddress, word_t value){
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE){return FAIL;}
    uint64_t address = get_page_address(virtualAddress);
    PMwrite(address, value);
    return SUCCESS;
}
