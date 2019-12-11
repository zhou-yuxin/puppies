#include <errno.h>
#include <assert.h>
#include <string.h>

#include <allocator.h>

int allocator_init(allocator_t* allocator, size_t size,
    void* (*func_allocate_meta)(size_t size, void* privdata),
    void (*func_free_meta)(void* ptr, void* privdata),
    void* privdata) {
    assert(allocator);
    size_t chunk_count = size / ALLOCATOR_CHUNK_SIZE;
    if(chunk_count == 0 || chunk_count > ALLOCATOR_MAX_CHUNK_COUNT) {
        return EINVAL;
    }
    int ret = buddy_init(&(allocator->buddy), chunk_count,
                            func_allocate_meta, privdata);
    if(ret) {
        return ret;
    }
    // allocate the chunk bitmap array
    size_t alloc_size = chunk_count * sizeof(allocator_bitmap_t);
    allocator_bitmap_t* array = func_allocate_meta ?
                                func_allocate_meta(alloc_size, privdata) :
                                malloc(alloc_size);
    if(!array) {
        buddy_deinit(&(allocator->buddy), func_free_meta, privdata);
        return ENOMEM;
    }
    // we needn't to clear the array, because buddy.chunk_array tells
    // which bitmaps are valid
    allocator->bitmap_array = array;
    // initialize the double-linked list of each order
    memset(&(allocator->lists), 0, sizeof(allocator->lists));
    return 0;
}

void allocator_deinit(allocator_t* allocator,
    void (*func_free_meta)(void* ptr, void* privdata), void* privdata) {
    assert(allocator);
    if(func_free_meta) {
        func_free_meta(allocator->bitmap_array, privdata);
    }
    else {
        free(allocator->bitmap_array);
    }
    allocator->bitmap_array = NULL;
    memset(&(allocator->lists), 0, sizeof(allocator->lists));
    buddy_deinit(&(allocator->buddy), func_free_meta, privdata);
}

// add a chunk(the first chunk of a block) into the list of given level
static void push_list(allocator_t* allocator, uint8_t level, size_t chunk_index) {
    assert(ALLOCATOR_ALIGN_TO_LEVEL(chunk_index, level) == chunk_index);
    allocator_chunk_t* chunk_array = allocator->buddy.chunk_array;
    allocator_chunk_t* chunk = chunk_array + chunk_index;
    assert(!chunk->free);
    assert(chunk->order == ALLOCATOR_LEVEL_TO_ORDER(level));
    assert(level < ALLOCATOR_MAX_LEVEL_COUNT);
    allocator_list_t* list = allocator->lists + level;
    allocator_chunk_t* head = list->head;
    // if the list is not empty, add the new chunks to the tail
    if(head) {
        assert(list->length > 0);
        chunk->next = head - chunk_array;
        chunk->prev = head->prev;
        chunk_array[head->prev].next = chunk_index;
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
}

// pop a given chunk from the list of given level
static void pop_list(allocator_t* allocator, uint8_t level, size_t chunk_index) {
    assert(ALLOCATOR_ALIGN_TO_LEVEL(chunk_index, level) == chunk_index);
    allocator_chunk_t* chunk_array = allocator->buddy.chunk_array;
    allocator_chunk_t* chunk = chunk_array + chunk_index;
    assert(!chunk->free);
    assert(chunk->order == ALLOCATOR_LEVEL_TO_ORDER(level));
    assert(chunk->prev < allocator->buddy.chunk_count);
    assert(chunk->next < allocator->buddy.chunk_count);
    assert(level < ALLOCATOR_MAX_LEVEL_COUNT);
    allocator_list_t* list = allocator->lists + level;
    assert(list->length > 0);
    // the list has more than one node
    if(list->length > 1) {
        assert(chunk->prev != chunk_index);
        assert(chunk->next != chunk_index);
        chunk_array[chunk->prev].next = chunk->next;
        chunk_array[chunk->next].prev = chunk->prev;
        if(list->head == chunk) {
            list->head = chunk_array + chunk->next;
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

#define MIN(a, b)   ((a) < (b) ? (a) : (b))

// get the count of valid bits in bitmap of given level
static uint8_t get_bitmap_bits(uint8_t level) {
    assert(allocator_level_size(0) == 8);
    assert(allocator_level_size(1) == 10);
    assert(allocator_level_size(2) == 12);
    assert(allocator_level_size(3) == 14);
    static const uint8_t bits_array[4] = {
        MIN(ALLOCATOR_CHUNK_SIZE / 8, ALLOCATOR_BITMAP_BITS),
        MIN(ALLOCATOR_CHUNK_SIZE / 10, ALLOCATOR_BITMAP_BITS),
        MIN(ALLOCATOR_CHUNK_SIZE / 12, ALLOCATOR_BITMAP_BITS),
        MIN(ALLOCATOR_CHUNK_SIZE / 14, ALLOCATOR_BITMAP_BITS),
    };
    uint8_t index_in_group = level % 4;
    uint8_t bits = bits_array[index_in_group];
    assert(bits * allocator_level_size(index_in_group) <= ALLOCATOR_CHUNK_SIZE);
    return bits;
}

// get bitmap of all-1 of given level
#define FULL_BITMAP(level)      ((1ULL << get_bitmap_bits(level)) - 1)

static void init_bitmap(allocator_t* allocator, size_t chunk_index, uint8_t level) {
    allocator_bitmap_t* bitmap = allocator->bitmap_array + chunk_index;
    bitmap->tiny = 1;
    bitmap->index_in_group = level % 4;
    // all bits are 1
    bitmap->frees = FULL_BITMAP(level);
}

size_t allocator_allocate(allocator_t* allocator, size_t size) {
    assert(allocator);
    int level = allocator_size_to_level(size);
    size_t chunk_index;
    // if too large to use tiny allocation, fail back to direct buddy allocation
    if(level < 0) {
        // order is the opposite number of level
        chunk_index = buddy_allocate(&(allocator->buddy), -level);
        if(chunk_index == BUDDY_FAIL) {
            return ALLOCATOR_FAIL;
        }
        allocator->bitmap_array[chunk_index].tiny = 0;
        return chunk_index * ALLOCATOR_CHUNK_SIZE;
    }
    assert((size_t)level < ALLOCATOR_MAX_LEVEL_COUNT);
    allocator_list_t* list = allocator->lists + level;
    // if this level has no available block, allocate one from buddy
    if(list->length == 0) {
        chunk_index = buddy_allocate(&(allocator->buddy),
                                        ALLOCATOR_LEVEL_TO_ORDER(level));
        if(chunk_index == BUDDY_FAIL) {
            return ALLOCATOR_FAIL;
        }
        // add this new block into level
        push_list(allocator, level, chunk_index);
        init_bitmap(allocator, chunk_index, level);
    }
    else {
        assert(list->head);
        chunk_index = list->head - allocator->buddy.chunk_array;
    }
    assert(chunk_index < allocator->buddy.chunk_count);
    // bitmap is valid only if chunk.free == 0 (in use)
    assert(!allocator->buddy.chunk_array[chunk_index].free);
    allocator_bitmap_t* bitmap = allocator->bitmap_array + chunk_index;
    assert(bitmap->tiny);
    assert(bitmap->frees);
    // find the lowest bit 1 in the bitmap
    int bit_index = __builtin_ctzll(bitmap->frees);
    assert(0 <= bit_index && bit_index < 64);
    // set the bit to 0
    bitmap->frees &= ~(1ULL << bit_index);
    // if all bits are 0, pop this block from the level
    if(!bitmap->frees) {
        pop_list(allocator, level, chunk_index);
    }
    return chunk_index * ALLOCATOR_CHUNK_SIZE +
            bit_index * allocator_level_size(level);
}

static int get_bit_index(size_t addr, size_t chunk_index, uint8_t level) {
    assert((size_t)level < ALLOCATOR_MAX_LEVEL_COUNT);
    assert(addr >= chunk_index * ALLOCATOR_CHUNK_SIZE);
    size_t offset_in_block = addr - chunk_index * ALLOCATOR_CHUNK_SIZE;
    size_t level_size = allocator_level_size(level);
    // the index of the piece in this block
    size_t bit_index = offset_in_block / level_size;
    // check whether addr is aligned to piece size
    if(bit_index >= get_bitmap_bits(level)
#ifdef ALLOCATOR_STRICT_ADDR
        || bit_index * level_size != offset_in_block
#endif
        ) {
        return -EINVAL;
    }
    assert(0 <= bit_index && bit_index < 64);
    return bit_index;
}

int allocator_reserve(allocator_t* allocator, size_t addr, size_t size) {
    assert(allocator);
    // the chunk where this piece starts
    size_t chunk_index = addr / ALLOCATOR_CHUNK_SIZE;
    if(chunk_index >= allocator->buddy.chunk_count) {
        return EINVAL;
    }
    int level = allocator_size_to_level(size);
    // if too large to use tiny allocation
    if(level < 0) {
#ifdef ALLOCATOR_STRICT_ADDR
        // addr should be the start of its chunk
        if(addr != chunk_index * ALLOCATOR_CHUNK_SIZE) {
            return EINVAL;
        }
#endif
        // order is the opposite number of level
        // chunk_index may be invalid, or may be allocated,
        // return the result upwards
        return buddy_reserve(&(allocator->buddy), chunk_index, -level);
    }
    // else tiny allocation
    assert((size_t)level < ALLOCATOR_MAX_LEVEL_COUNT);
    uint8_t order = ALLOCATOR_LEVEL_TO_ORDER(level);
    // align the chunk index, get the first chunk index
    chunk_index = BUDDY_ALIGN_TO_ORDER(chunk_index, order);
    // now we determine the index of this piece in this block
    int bit_index = get_bit_index(addr, chunk_index, level);
    if(bit_index < 0) {
        return -bit_index;
    }
    uint64_t bitmap_mask = 1ULL << bit_index;
    allocator_chunk_t* chunk = allocator->buddy.chunk_array + chunk_index;
    allocator_bitmap_t* bitmap = allocator->bitmap_array + chunk_index;
    // if this chunk is not allocated, we should allocate it first
    if(chunk->free) {
        int ret = buddy_reserve(&(allocator->buddy), chunk_index, order);
        if(ret != 0) {
            return ret;
        }
        push_list(allocator, level, chunk_index);
        init_bitmap(allocator, chunk_index, level);
        assert(!chunk->free);
        assert(chunk->order == order);
        assert(bitmap->tiny);
        assert(chunk->order * 4 + bitmap->index_in_group == level);
        assert(bitmap->frees & bitmap_mask);
    }
    // else chunk is allocated already,
    else {
        // but either for another order or for a direct allocation
        if(!(chunk->order == order && bitmap->tiny) ||
            // or not for this level
            chunk->order * 4 + bitmap->index_in_group != level ||
            // or is allocated
            (bitmap->frees & bitmap_mask) == 0) {
            return EBUSY;
        }
    }
    // set the bit to 0
    bitmap->frees &= ~bitmap_mask;
    // if all bits are 0, pop this block from the level
    if(!bitmap->frees) {
        pop_list(allocator, level, chunk_index);
    }
    return 0;
}

int allocator_free(allocator_t* allocator, size_t addr) {
    assert(allocator);
    // the chunk where this piece starts
    size_t chunk_index = addr / ALLOCATOR_CHUNK_SIZE;
    if(chunk_index >= allocator->buddy.chunk_count) {
        return EINVAL;
    }
    // the chunk where this piece is
    allocator_chunk_t* chunk = allocator->buddy.chunk_array + chunk_index;
    // the whole chunk is not allocated
    if(chunk->free) {
        return EINVAL;
    }
    uint8_t order = chunk->order;
    // align the chunk index, get the first chunk index
    chunk_index = BUDDY_ALIGN_TO_ORDER(chunk_index, order);
    chunk = allocator->buddy.chunk_array + chunk_index;
    assert(!chunk->free);
    assert(chunk->order == order);
    allocator_bitmap_t* bitmap = allocator->bitmap_array + chunk_index;
    // if this block is for direct allocation
    if(!bitmap->tiny) {
#ifdef ALLOCATOR_STRICT_ADDR
        // addr should be the start of its chunk
        if(addr != chunk_index * ALLOCATOR_CHUNK_SIZE) {
            return EINVAL;
        }
#endif
        return buddy_free(&(allocator->buddy), chunk_index);
    }
    // else for tiny allocation
    uint8_t level = order * 4 + bitmap->index_in_group;
    assert((size_t)level < ALLOCATOR_MAX_LEVEL_COUNT);
    // now we determine the index of this piece in this block
    int bit_index = get_bit_index(addr, chunk_index, level);
    if(bit_index < 0) {
        return -bit_index;
    }
    uint64_t bitmap_mask = 1ULL << bit_index;
    // if this piece is free
    if(bitmap->frees & bitmap_mask) {
        return EINVAL;
    }
    // if all the pieces were allocated, we can add it back to the level list
    if(bitmap->frees == 0) {
        push_list(allocator, level, chunk_index);
    }
    // set the bit to 1
    bitmap->frees |= bitmap_mask;
    // if all the pieces are free, return this block to buddy
    if(bitmap->frees == FULL_BITMAP(level)) {
        pop_list(allocator, level, chunk_index);
#ifndef NDEBUG
        int ret = buddy_free(&(allocator->buddy), chunk_index);
        assert(ret == 0);
#else
        buddy_free(&(allocator->buddy), chunk_index);
#endif
        assert(chunk->free);
    }
    return 0;
}

size_t allocator_usable_size(allocator_t* allocator, size_t addr) {
    assert(allocator);
    // the chunk where this piece starts
    size_t chunk_index = addr / ALLOCATOR_CHUNK_SIZE;
    if(chunk_index >= allocator->buddy.chunk_count) {
        return 0;
    }
    // the chunk where this piece is
    allocator_chunk_t* chunk = allocator->buddy.chunk_array + chunk_index;
    // the whole chunk is not allocated
    if(chunk->free) {
        return 0;
    }
    uint8_t order = chunk->order;
    // align the chunk index, get the first chunk index
    chunk_index = BUDDY_ALIGN_TO_ORDER(chunk_index, order);
    chunk = allocator->buddy.chunk_array + chunk_index;
    assert(!chunk->free);
    assert(chunk->order == order);
    allocator_bitmap_t* bitmap = allocator->bitmap_array + chunk_index;
    // if this block is for direct allocation
    if(!bitmap->tiny) {
#ifdef ALLOCATOR_STRICT_ADDR
        // addr should be the start of its chunk
        if(addr != chunk_index * ALLOCATOR_CHUNK_SIZE) {
            return 0;
        }
#endif
        return BUDDY_ORDER_TO_COUNT(order) * ALLOCATOR_CHUNK_SIZE;
    }
    // else for tiny allocation
    uint8_t level = order * 4 + bitmap->index_in_group;
    assert((size_t)level < ALLOCATOR_MAX_LEVEL_COUNT);
    return allocator_level_size(level);
}

// get ceil(n / p), assuming n >= 0 and p >= 0
#define DIV_CEIL(n, p)                          \
({                                              \
    typeof(n) _n_ = (n);                        \
    _n_ == 0 ? 0 : (_n_ - 1) / (p) + 1;         \
})

int allocator_size_to_level(size_t size) {
    // for any size < 8, level is 0
    if(size < 8) {
        return 0;
    }
    // the highest bit index
    int highest_bit = sizeof(unsigned long long) * 8 - __builtin_clzll(size) - 1;
    assert(highest_bit >= 3);
    // 4 level per group, level 0 = size 8
    int group_index = highest_bit - 3;
    assert(size >= ALLOCATOR_GROUP_BASE_SIZE(group_index));
    int level_addition = DIV_CEIL(size - ALLOCATOR_GROUP_BASE_SIZE(group_index),
                                    ALLOCATOR_GROUP_SIZE_GAP(group_index));
    assert(level_addition >= 0);
    assert(level_addition <= 4);
    int level = group_index * 4 + level_addition;
    // if tiny enough to use tiny allocation
    if((size_t)level <= ALLOCATOR_MAX_LEVEL) {
        return level;
    }
    // else use direct allocation from buddy for too large size
    // the count of chunk to use
    size_t chunk_count = DIV_CEIL(size, ALLOCATOR_CHUNK_SIZE);
    assert(chunk_count > 0);
    // find the min 2^n >= chunk_count
    highest_bit = sizeof(unsigned long long) * 8 - __builtin_clzll(chunk_count) - 1;
    // if chunk_count is a power of 2, return the highest_bit
    // else we need the higher order
    int order = (1ULL << highest_bit) == chunk_count ?
                highest_bit : highest_bit + 1;
    assert(order > 0);
    assert(BUDDY_ORDER_TO_COUNT(order) * ALLOCATOR_CHUNK_SIZE >= size);
    return -order;
}

size_t allocator_level_size(uint8_t level) {
    uint8_t group_index = level / 4;
    return ALLOCATOR_GROUP_BASE_SIZE(group_index) +
            (level % 4) * ALLOCATOR_GROUP_SIZE_GAP(group_index);
}