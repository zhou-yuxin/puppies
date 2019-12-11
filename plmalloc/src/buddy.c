#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <buddy.h>

#define CHECK_BLOCK(buddy, chunk_index, order)                                  \
    (                                                                           \
        ((order) <= (buddy)->max_order) &&                                      \
        (BUDDY_ALIGN_TO_ORDER((chunk_index), (order)) == (chunk_index)) &&      \
        ((chunk_index) + BUDDY_ORDER_TO_COUNT(order) <= (buddy)->chunk_count)   \
    )

#define ASSERT_BLOCK(buddy, chunk_index, order)                                 \
    assert(CHECK_BLOCK(buddy, chunk_index, order))

// call this to set the value of followers same as the first buddy_chunk_t
static void propagate_state(buddy_chunk_t* chunk, uint8_t order) {
    size_t count = BUDDY_ORDER_TO_COUNT(order);
    for(size_t i = 1; i < count; i++) {
        chunk[i] = chunk[0];
    }
}

// add a chunk(the first chunk of a block) into the list of given order
static void push_list(buddy_t* buddy, uint8_t order, size_t chunk_index) {
    ASSERT_BLOCK(buddy, chunk_index, order);
    buddy_chunk_t* array = buddy->chunk_array;
    buddy_chunk_t* chunk = array + chunk_index;
    buddy_list_t* list = buddy->lists + order;
    buddy_chunk_t* head = list->head;
    // if the list is not empty, add the new chunks to the tail
    if(head) {
        assert(list->length > 0);
        chunk->next = head - array;
        chunk->prev = head->prev;
        array[head->prev].next = chunk_index;
        head->prev = chunk_index;
    }
    // else the single node points to itself
    else {
        assert(list->length == 0);
        chunk->prev = chunk_index;
        chunk->next = chunk_index;
        list->head = chunk;
    }
    list->length++;
    chunk->free = 1;
    chunk->order = order;
    propagate_state(chunk, order);
}

// pop a given element from the list of given order
static void pop_list(buddy_t* buddy, uint8_t order, size_t chunk_index) {
    ASSERT_BLOCK(buddy, chunk_index, order);
    buddy_chunk_t* array = buddy->chunk_array;
    buddy_chunk_t* chunk = array + chunk_index;
    assert(chunk->free);
    assert(chunk->order == order);
    assert(chunk->prev < buddy->chunk_count);
    assert(chunk->next < buddy->chunk_count);
    buddy_list_t* list = buddy->lists + order;
    assert(list->length > 0);
    // the list has more than one node
    if(list->length > 1) {
        assert(chunk->prev != chunk_index);
        assert(chunk->next != chunk_index);
        array[chunk->prev].next = chunk->next;
        array[chunk->next].prev = chunk->prev;
        if(list->head == chunk) {
            list->head = array + chunk->next;
        }
    }
    else {
        assert(chunk->prev == chunk_index);
        assert(chunk->next == chunk_index);
        assert(list->head == chunk);
        list->head = NULL;
    }
    list->length--;
}

// pop the first element from the list of given order
static size_t pop_list_head(buddy_t* buddy, uint8_t order) {
    buddy_chunk_t* array = buddy->chunk_array;
    buddy_list_t* list = buddy->lists + order;
    buddy_chunk_t* head = list->head;
    assert(head);
    size_t chunk_index = head - array;
    ASSERT_BLOCK(buddy, chunk_index, order);
    assert(head->free);
    assert(head->order == order);
    assert(list->length > 0);
    // the list has more than one node
    if(list->length > 1) {
        assert(head->prev != chunk_index);
        assert(head->next != chunk_index);
        array[head->prev].next = head->next;
        array[head->next].prev = head->prev;
        list->head = array + head->next;
    }
    else {
        assert(head->prev == chunk_index);
        assert(head->next == chunk_index);
        list->head = NULL;
    }
    list->length--;
    return chunk_index;
}

int buddy_init(buddy_t* buddy, size_t chunk_count,
    void* (*func_allocate_meta)(size_t size, void* privdata), void* privdata) {
    assert(buddy);
    if(chunk_count == 0 || chunk_count > BUDDY_MAX_CHUNK_COUNT) {
        return EINVAL;
    }
    buddy->chunk_count = chunk_count;
    // allocate the chunk infomation array
    size_t alloc_size = chunk_count * sizeof(buddy_chunk_t);
    buddy_chunk_t* array = func_allocate_meta ?
                            func_allocate_meta(alloc_size, privdata) :
                            malloc(alloc_size);
    if(!array) {
        return ENOMEM;
    }
    buddy->chunk_array = array;
    // the index of the highest bit of 1
    int highest_bit = sizeof(unsigned long long) * 8 -
                            __builtin_clzll(chunk_count) - 1;
    assert(highest_bit >= 0);
    buddy->max_order = highest_bit < (int)BUDDY_MAX_ORDER ?
                            highest_bit : (int)BUDDY_MAX_ORDER;
    // initialize the double-linked list of each order
    memset(&(buddy->lists), 0, sizeof(buddy->lists));
    // add all the chunks to the list of the current highest order
    size_t offset = 0;
    // divide into many trees, from big tree to small tree
    for(int order = buddy->max_order; order >= 0; order--) {
        // capacity of current tree
        size_t skip = BUDDY_ORDER_TO_COUNT(order);
        while(1) {
            size_t next_offset = offset + skip;
            if(next_offset > chunk_count) {
                break;
            }
            // all the buddy_chunk_t will be inited in push_list()
            push_list(buddy, order, offset);
            offset = next_offset;
        }
    }
    assert(offset == chunk_count);
    return 0;
}

void buddy_deinit(buddy_t* buddy,
    void (*func_free_meta)(void* ptr, void* privdata), void* privdata) {
    assert(buddy);
    if(func_free_meta) {
        func_free_meta(buddy->chunk_array, privdata);
    }
    else {
        free(buddy->chunk_array);
    }
    buddy->chunk_array = NULL;
    buddy->chunk_count = 0;
    buddy->max_order = 0;
    memset(&(buddy->lists), 0, sizeof(buddy->lists));
}

static void mark_allocated(buddy_t* buddy, size_t chunk_index, uint8_t order) {
    ASSERT_BLOCK(buddy, chunk_index, order);
    buddy_chunk_t* chunk = buddy->chunk_array + chunk_index;
    chunk->free = 0;
    chunk->order = order;
    propagate_state(chunk, order);
}

// this is the recursive algorithm of buddy allocation,
// but before export to user, we need additionally to mark it.
static size_t allocate_block(buddy_t* buddy, uint8_t order) {
    if(order > buddy->max_order) {
        return BUDDY_FAIL;
    }
    // if the list is not empty
    if(buddy->lists[order].length) {
        // get the first chunk in the list
        return pop_list_head(buddy, order);
    }
    // we have to allocate from higher order, and split it
    // then add the right buddy to this order, and return the left buddy
    size_t chunk_index = allocate_block(buddy, order + 1);
    if(chunk_index == BUDDY_FAIL) {
        return BUDDY_FAIL;
    }
    size_t buddy_chunk_index = chunk_index + BUDDY_ORDER_TO_COUNT(order);
    push_list(buddy, order, buddy_chunk_index);
    return chunk_index;
}

size_t buddy_allocate(buddy_t* buddy, uint8_t order) {
    assert(buddy);
    size_t chunk_index = allocate_block(buddy, order);
    if(chunk_index == BUDDY_FAIL) {
        return BUDDY_FAIL;
    }
    mark_allocated(buddy, chunk_index, order);
    return chunk_index;
}

// this is the recursive algorithm of buddy reservation,
// but before export to user, we need additionally to mark it.
static void reserve_block(buddy_t* buddy, size_t chunk_index, uint8_t order,
    size_t root_chunk_index, uint8_t root_order) {
    ASSERT_BLOCK(buddy, chunk_index, order);
    ASSERT_BLOCK(buddy, root_chunk_index, root_order);
    assert(root_chunk_index <= chunk_index);
    assert(root_chunk_index + BUDDY_ORDER_TO_COUNT(root_order) >=
            chunk_index + BUDDY_ORDER_TO_COUNT(order));
    assert(root_order >= order);
    // if we get what exactly we want
    if(root_order == order) {
        return;
    }
    // else the whole block this chunk is in
    // the middle position of this whole block
    uint8_t sub_order = root_order - 1;
    size_t boundary = root_chunk_index + BUDDY_ORDER_TO_COUNT(sub_order);
    // if what we want is in the left buddy
    if(chunk_index < boundary) {
        // add the right buddy to lower order
        push_list(buddy, sub_order, boundary);
        // dive into left buddy
        reserve_block(buddy, chunk_index, order, root_chunk_index, sub_order);
    }
    // else what we want is in the right buddy
    else {
        // and the left buddy to lower order
        push_list(buddy, sub_order, root_chunk_index);
        // dive into right buddy
        reserve_block(buddy, chunk_index, order, boundary, sub_order);
    }
}

int buddy_reserve(buddy_t* buddy, size_t chunk_index, uint8_t order) {
    assert(buddy);
    if(!CHECK_BLOCK(buddy, chunk_index, order)) {
        return EINVAL;
    }
    buddy_chunk_t* chunk = buddy->chunk_array + chunk_index;
    if(!(chunk->free && chunk->order >= order)) {
        return EBUSY;
    }
    // if chunk->free and chunk->order >= order,
    // it must be able to reserve
    // the root tree where this block is
    uint8_t root_order = chunk->order;
    size_t root_chunk_index = BUDDY_ALIGN_TO_ORDER(chunk_index, root_order);
    // pop out the root
    pop_list(buddy, root_order, root_chunk_index);
    // dive out want we want from the root
    reserve_block(buddy, chunk_index, order, root_chunk_index, root_order);
    mark_allocated(buddy, chunk_index, order);
    return 0;
}

// this is the recursive algorithm of buddy free
static void free_block(buddy_t* buddy, uint8_t order, size_t chunk_index) {
    ASSERT_BLOCK(buddy, chunk_index, order);
    if(order < buddy->max_order) {
        // get the parent chunk index (if any)
        size_t higher_chunk_index = BUDDY_ALIGN_TO_ORDER(chunk_index, order + 1);
        size_t buddy_chunk_index;
        // if parent chunk index == chunk index, this is the left buddy,
        // buddy is the right one,
        if(chunk_index == higher_chunk_index) {
            buddy_chunk_index = higher_chunk_index + BUDDY_ORDER_TO_COUNT(order);
        }
        // else buddy is the left one
        else {
            assert(chunk_index == higher_chunk_index + BUDDY_ORDER_TO_COUNT(order));
            buddy_chunk_index = higher_chunk_index;
        }
        // if chunk_count is not a power of 2, a block may have no buddy
        if(buddy_chunk_index < buddy->chunk_count) {
            // points to buddy
            buddy_chunk_t* buddy_chunk = buddy->chunk_array + buddy_chunk_index;
            assert(buddy_chunk->order <= order);
            // if buddy is not split and is free, then merge to higher order
            if(buddy_chunk->order == order && buddy_chunk->free) {
                // remove buddy from this order, and together free to uppder order
                pop_list(buddy, order, buddy_chunk_index);
                free_block(buddy, order + 1, higher_chunk_index);
                return;
            }
        }
    }
    push_list(buddy, order, chunk_index);
}

int buddy_free(buddy_t* buddy, size_t chunk_index) {
    assert(buddy);
    if(chunk_index >= buddy->chunk_count) {
        return EINVAL;
    }
    buddy_chunk_t* chunk = buddy->chunk_array + chunk_index;
    if(chunk->free) {
        return EINVAL;
    }
    uint8_t order = chunk->order;
    if(!CHECK_BLOCK(buddy, chunk_index, order)) {
        return EINVAL;
    }
    // if all the conditions above are ok, it must be able to free
    free_block(buddy, order, chunk_index);
    return 0;
}