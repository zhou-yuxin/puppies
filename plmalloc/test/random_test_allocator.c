#include "allocator.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#define TOTAL_SIZE          (10ULL << 30)
#define ALLOCATE_COUNT      (1000000)
#define MIN_SIZE            0
#define MAX_SIZE            (2ULL << 20)
#define FREE_RATIO          50

#define UNUSED(x)           ((void)(x))

#define DO_WITH_ASSERT(statement, expection)    \
    do {                                        \
        typeof(statement) _ret_ = (statement);  \
        UNUSED(_ret_);                          \
        if(!(expection))                        \
        {                                       \
            printf("%d", __LINE__);             \
            abort();                            \
        }                                       \
    } while(0)

typedef struct
{
    size_t addr;
    size_t size;
}
allocation_t;

typedef struct
{
    allocation_t* array;
    size_t capacity;
    size_t count;
}
allocation_set_t;

void allocation_set_init(allocation_set_t* set, size_t init_capacity)
{
    assert(set);
    if(init_capacity == 0)
    {
        init_capacity = 1;
    }
    DO_WITH_ASSERT(set->array = malloc(sizeof(allocation_t) * init_capacity),
                    set->array);
    set->capacity = init_capacity;
    set->count = 0;
}

void allocation_set_add(allocation_set_t* set, allocation_t* allocation)
{
    assert(set);
    if(set->count == set->capacity)
    {
        size_t new_capacity = set->capacity * 2;
        DO_WITH_ASSERT(set->array = realloc(set->array, new_capacity),
                        set->array);
        set->capacity = new_capacity;
    }
    assert(set->count < set->capacity);
    set->array[set->count++] = *allocation;
}

int allocation_set_pop(allocation_set_t* set, allocation_t* dst)
{
    assert(set);
    if(set->count == 0)
    {
        return 0;
    }
    size_t index = rand() % set->count;
    *dst = set->array[index];
    set->array[index] = set->array[--set->count];
    return 1;
}

void stat_buddy(buddy_t* buddy)
{
    printf("====================\n");
    for(size_t i = 0; i < BUDDY_MAX_ORDER_COUNT; i++)
    {
        buddy_list_t* list = buddy->lists + i;
        printf("order[%lu] length: %lu, list: ", i, list->length);
        buddy_chunk_t* chunk = list->head;
        for(size_t i = 0; i < list->length; i++)
        {
            printf("%lu ", chunk - buddy->chunk_array);
            chunk = buddy->chunk_array + chunk->next;
        }
        printf("\n");
    }
    printf("\n");
}

int main()
{
    srand(time(0));
    allocation_set_t set;
    allocation_set_init(&set, ALLOCATE_COUNT);

    allocator_t allocator;
    DO_WITH_ASSERT(allocator_init(&allocator, TOTAL_SIZE, NULL, NULL, NULL), _ret_ == 0);

    stat_buddy(&(allocator.buddy));

    for(size_t i = 0; i < ALLOCATE_COUNT; i++)
    {
        size_t size = (rand() % (MAX_SIZE - MIN_SIZE + 1)) + MIN_SIZE;
        assert(MIN_SIZE <= size && size <= MAX_SIZE);
        size_t addr = allocator_allocate(&allocator, size);
        int to_free;
        if(addr != ALLOCATOR_FAIL)
        {
            // printf("allocate, addr = %lu, size = %lu\n", addr, size);
            DO_WITH_ASSERT(allocator_usable_size(&allocator, addr), _ret_ >= size);
            size_t usable_size = allocator_usable_size(&allocator, addr);
            // printf("usable = %lu\n", usable_size);
            assert(usable_size >= size);
            allocation_t allocation;
            allocation.addr = addr;
            allocation.size = size;
            allocation_set_add(&set, &allocation);
            to_free = (rand() % 100) <= FREE_RATIO;
        }
        else
        {
            to_free = 1;
        }
        if(to_free)
        {
            allocation_t allocation;
            if(allocation_set_pop(&set, &allocation))
            {
                DO_WITH_ASSERT(allocator_free(&allocator, allocation.addr),
                        _ret_ == 0);
                // printf("free, addr = %lu, size = %lu\n",
                //         allocation.addr, allocation.size);
            }
        }
    }

    stat_buddy(&(allocator.buddy));

#ifndef TEST_RESERVE
    allocation_t allocation;
    while(allocation_set_pop(&set, &allocation))
    {
        // printf("free, addr = %lu, size = %lu\n",
        //                 allocation.addr, allocation.size);
        DO_WITH_ASSERT(allocator_usable_size(&allocator, allocation.addr),
                        _ret_ >= allocation.size);
        DO_WITH_ASSERT(allocator_free(&allocator, allocation.addr),
                        _ret_ == 0);
    }
    stat_buddy(&(allocator.buddy));

    allocator_deinit(&allocator, NULL, NULL);
#else
    allocator_deinit(&allocator, NULL, NULL);
    DO_WITH_ASSERT(allocator_init(&allocator, TOTAL_SIZE, NULL, NULL, NULL), _ret_ == 0);

    allocation_t allocation;
    while(allocation_set_pop(&set, &allocation))
    {
        DO_WITH_ASSERT(allocator_reserve(&allocator, allocation.addr, allocation.size),
                        _ret_ == 0);
        DO_WITH_ASSERT(allocator_usable_size(&allocator, allocation.addr),
                        _ret_ >= allocation.size);
    }

    stat_buddy(&(allocator.buddy));
    allocator_deinit(&allocator, NULL, NULL);
#endif

    return 0;
}