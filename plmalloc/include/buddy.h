#ifndef BUDDY_H
#define BUDDY_H

#include <stdint.h>
#include <stdlib.h>

// if allocation fails, return this
#define BUDDY_FAIL                  ((size_t)(-1))

#define BUDDY_ORDER_BITS            4
#define BUDDY_MAX_ORDER_COUNT       (1ULL << BUDDY_ORDER_BITS)
#define BUDDY_MAX_ORDER             (BUDDY_MAX_ORDER_COUNT - 1)

#define BUDDY_LINK_BITS             ((64 - BUDDY_ORDER_BITS - 1) / 2)
#define BUDDY_MAX_CHUNK_COUNT       ((1ULL << BUDDY_LINK_BITS))

// get the 2^o
#define BUDDY_ORDER_TO_COUNT(o)     (1ULL << (o))

// get the index of starting chunk in a block of given order
#define BUDDY_ALIGN_TO_ORDER(index, order)  \
    ((index) & (~(BUDDY_ORDER_TO_COUNT(order) - 1)))

// we have some rules on the use of buddy_chunk_t.
//
// for a free block (a serial of chunks) (must be in one list),
// the first buddy_chunk_t is set as:
//      <free>, 1
//      <order> indicates the size of the block,
//      <prev>, <next> are set to link a double-linked list,
//      even in a list with a single node, prev and next point to itself
// and the following buddy_chunk_t(s) are same as the first chunk.
//
// for an allocated block, all the buddy_chunk_t(s) are set as:
//      <free>, 0
//      <order> indicates the size of the block
//      <prev>, <next> are undefined
typedef struct {
    // is free (unallocated)
    uint64_t free : 1;
    // order
    uint64_t order : BUDDY_ORDER_BITS;
    // index of previous chunk in double-linked list
    uint64_t prev : BUDDY_LINK_BITS;
    // index of next chunk in double-linked list
    uint64_t next : BUDDY_LINK_BITS;
}
buddy_chunk_t;

typedef struct {
    // the head of the double-linked list
    buddy_chunk_t* head;
    // the count of node in the list
    size_t length;
}
buddy_list_t;

typedef struct {
    // count of chunks
    size_t chunk_count;
    // the state of each chunk
    buddy_chunk_t* chunk_array;
    // the max order to allocate
    uint8_t max_order;
    // the double-linked list of each order
    buddy_list_t lists[BUDDY_MAX_ORDER_COUNT];
}
buddy_t;

// initialize a buddy allocator
//      chunk_count: count of chunk to be managed
//      func_allocate_meta: user defined function to allocate space for meta data
//          size: allocating size
//          privdata: user defined data for this function
//      privdata: user defined data passing to func_allocate_meta
// return 0 if ok, or an errno:
//      EINVAL: chunk_count is invalid (0 or too large)
//      ENOMEM: fail to allocate space for meta data
// if func_allocate_meta is NULL, then it will fall back to use malloc()
int buddy_init(buddy_t* buddy, size_t chunk_count,
    void* (*func_allocate_meta)(size_t size, void* privdata), void* privdata);

// destroy a buddy allocator
//      func_free_meta: user defined function to free space for meta data
//          ptr: the space to be released
//          privdata: user defined data for this function
//      privdata: user defined data passing to func_allocate_meta
// if func_free_meta is NULL, then it will fall back to use free()
void buddy_deinit(buddy_t* buddy,
    void (*func_free_meta)(void* ptr, void* privdata), void* privdata);

// allocate a block (a serial of continuous chunks)
//      order: count of chunks = 2 ^ order
// return the starting chunk index if ok, or BUDDY_FAIL if unavailable
size_t buddy_allocate(buddy_t* buddy, uint8_t order);

// reserve a block
//      chunk_index: the index of the starting chunk
//      order: count of chunks = 2 ^ order
// return 0 if ok, or an errno:
//      EINVAL: invalid arguments,
//                  e.g. chunk_index is not the index of the starting chunk,
//                  or out of range
//      EBUSY: (part of) this block has been allocated / occupied
int buddy_reserve(buddy_t* buddy, size_t chunk_index, uint8_t order);

// free a block
//      chunk_index: the index of the first chunk of the block
// return 0 if ok, or an errno:
//      EINVAL: wrong address
int buddy_free(buddy_t* buddy, size_t chunk_index);

#endif