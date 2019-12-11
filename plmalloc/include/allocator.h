#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stdlib.h>

#include <buddy.h>

// if allocation fails, return this
#define ALLOCATOR_FAIL                  ((size_t)(-1))

#define ALLOCATOR_MAX_LEVEL_COUNT       (4 * BUDDY_MAX_ORDER_COUNT)
#define ALLOCATOR_MAX_LEVEL             (ALLOCATOR_MAX_LEVEL_COUNT - 1)

#define ALLOCATOR_BITMAP_BITS           (64 - 1 - 2)

#define ALLOCATOR_MAX_CHUNK_COUNT       BUDDY_MAX_CHUNK_COUNT
#define ALLOCATOR_CHUNK_SIZE            512ULL

// order == group_index == level / 4
#define ALLOCATOR_LEVEL_TO_ORDER(level)         ((level) / 4)

// get the index of starting chunk in a block of given level
#define ALLOCATOR_ALIGN_TO_LEVEL(index, level)  \
    BUDDY_ALIGN_TO_ORDER(index, ALLOCATOR_LEVEL_TO_ORDER(level))

// group_index = 0 => base_level = 0 => base_size = 8
// group_index = 1 => base_level = 4 => base_size = 16
// group_index = 2 => base_level = 8 => base_size = 32
#define ALLOCATOR_GROUP_BASE_SIZE(group_index)  (8ULL << (group_index))

// group_index = 0 => sizes: 8, 10, 12, 14, size_gap = 2
// group_index = 1 => sizes: 16, 20, 24, 28, size_gap = 4
// group_index = 2 => sizes: 32, 40, 48, 56, size_gap = 8
#define ALLOCATOR_GROUP_SIZE_GAP(group_index)   (2ULL << (group_index))

// we use a trick here: 
// because .prev and .next fields of allocated chunks are undefined,
// we reuse them to build a double-linked list of each level,
// and .order tells the group index.
// we additionally use a bitmap to store the state of each chunk.
// level = buddy_chunk_t.order * 4 + allocator_bitmap_t.index_in_group
typedef buddy_chunk_t allocator_chunk_t;

// we have some rules on the use of allocator_bitmap_t.
//
// for a block for tiny allocation (must be in one list),
// the first allocator_bitmap_t is set as:
//      <tiny>, 1
//      <index_in_group> indicates the level (with help of chunk->order),
//      <frees> is the availablity bitmap,
// and the following allocator_bitmap_t(s) are undefined.
//
// for a block for direct allocation, all the allocator_bitmap_t(s) are set as:
//      <tiny>, 0
//      <index_in_group>, <frees> are undefined
// and the following allocator_bitmap_t(s) are undefined.
typedef struct {
    // is a chunk for tiny piece
    uint64_t tiny : 1;
    // if tiny == 1, this is the index in the group, else undefined
    uint64_t index_in_group: 2;
    // if tiny == 1, the state (free = 1, allocated = 0) of each piece
    uint64_t frees : ALLOCATOR_BITMAP_BITS;
}
allocator_bitmap_t;

// each level has a list, linking the blocks which have available pieces
typedef struct {
    // the head of the double-linked list
    allocator_chunk_t* head;
    // count of node in the list 
    size_t length;
}
allocator_list_t;

typedef struct {
    // a buddy allocator
    buddy_t buddy;
    // the bitmap of each chunk
    allocator_bitmap_t* bitmap_array;
    // the double-linked list of each level
    allocator_list_t lists[ALLOCATOR_MAX_LEVEL_COUNT];
}
allocator_t;

// initialize an allocator
//      size: size of a continuous space to be managed
//      func_allocate_meta: user defined function to allocate space for meta data
//          size: allocating size
//          privdata: user defined data for this function
//      func_free_meta: user defined function to free space for meta data
//          ptr: the space to be released
//          privdata: user defined data for this function
//      privdata: user defined data passing to func_allocate_meta
// return 0 if ok, or an errno:
//      EINVAL: size is too large
//      ENOMEM: fail to allocate space for meta data
// if func_allocate_meta is NULL, then it will fall back to use malloc()
// if func_free_meta is NULL, then it will fall back to use free()
int allocator_init(allocator_t* allocator, size_t size,
    void* (*func_allocate_meta)(size_t size, void* privdata),
    void (*func_free_meta)(void* ptr, void* privdata),
    void* privdata);

// destroy an allocator
//      func_free_meta: user defined function to free space for meta data
//          ptr: the space to be released
//          privdata: user defined data for this function
//      privdata: user defined data passing to func_allocate_meta
// if func_free_meta is NULL, then it will fall back to use free()
void allocator_deinit(allocator_t* allocator,
    void (*func_free_meta)(void* ptr, void* privdata), void* privdata);

// allocate a piece of memory
//      size: the min size of the piece of memory
// return the offset in the logic memory space, or ALLOCATOR_FAIL if unavailable
size_t allocator_allocate(allocator_t* allocator, size_t size);

// reserve a piece of memory
//      addr: the address of the piece
//      size: the size of thie piece
// return 0 if ok, or an errno:
//      EINVAL: addr or size is invalid
//      EBUSY: (part of) this piece has been allocated / occupied
int allocator_reserve(allocator_t* allocator, size_t addr, size_t size);

// free a piece of memory
//      addr: the address of the piece
// return 0 if ok, or an errno:
//      EINVAL: wrong address
int allocator_free(allocator_t* allocator, size_t addr);

// get the usable size of the given piece of memory
//      addr: the address of the piece
// return the usable size, or 0 if addr is invalid
// notes: what is usable size?
//          it's the actual usable size you get, for example,
//          when you allocate(100), the allocated piece have a usable size of 112.
size_t allocator_usable_size(allocator_t* allocator, size_t addr);

// transfer size to level
//      size: the min size to allocate
// if size is small enough to use tiny allocation, return a non-negative level,
// otherwise a negative number n,
// whereas -n is the order for direct allocation from buddy system.
int allocator_size_to_level(size_t size);

// get the capacity of a piece of memory allocated from given level
//      level: the level
// return the capacity.
// this function doesn't check whether level is uder ALLOCATOR_MAX_LEVEL_COUNT
size_t allocator_level_size(uint8_t level);

// NOTE:
// reserve(), free() and usable_size() all take an argument 'addr'.
// if compiled with marco ALLOCATOR_STRICT_ADDR defined,
// 'addr' will be checked whether it is the starting address of the piece.
// for example, assuming you call allocate(100), and then get offset = 0x1000,
// in ALLOCATOR_STRICT_ADDR mode, allocator_usable_size(0x1002) returns 0,
// because 0x1002 is not the starting address.
// but without ALLOCATOR_STRICT_ADDR, allocator_usable_size(0x1002) is same as
// allocator_usable_size(0x1000), as long as 'addr' is within the piece.
#endif