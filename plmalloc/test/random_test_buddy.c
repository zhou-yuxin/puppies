#include "buddy.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#define CHUNK_COUNT         (1ULL<<20)
// #define CHUNK_COUNT         1234567
#define ALLOCATE_COUNT      (1ULL << 20)
#define MIN_ORDER           0
#define MAX_ORDER           4
#define FREE_RATIO          30

#define UNUSED(x)           ((void)(x))

#define DO_WITH_ASSERT(statement, expection)    \
    do {                                        \
        typeof(statement) _ret_ = (statement);  \
        UNUSED(_ret_);                          \
        if(!(expection))                        \
        {                                       \
            abort();                            \
        }                                       \
    } while(0)

typedef struct
{
    size_t chunk_index;
    size_t order;
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

char buffer[CHUNK_COUNT * sizeof(buddy_chunk_t)];

void* allocate(size_t size, void* privdata)
{
    assert(size == sizeof(buffer));
    return buffer;
}

void release(void* ptr, void* privdata)
{
    assert(ptr == buffer);
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

    buddy_t buddy;
    DO_WITH_ASSERT(buddy_init(&buddy, CHUNK_COUNT, allocate, NULL), _ret_ == 0);

    stat_buddy(&buddy);

    DO_WITH_ASSERT(buddy_reserve(&buddy, 0, 0), _ret_ == 0);
    stat_buddy(&buddy);

    buddy_free(&buddy, 0);
    stat_buddy(&buddy);

    for(size_t i = 0; i < ALLOCATE_COUNT; i++)
    {
        size_t order = (rand() % (MAX_ORDER - MIN_ORDER + 1)) + MIN_ORDER;
        assert(MIN_ORDER <= order && order <= MAX_ORDER);
        size_t index = buddy_allocate(&buddy, order);
        int to_free;
        if(index != BUDDY_FAIL)
        {
            allocation_t allocation;
            allocation.chunk_index = index;
            allocation.order = order;
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
                DO_WITH_ASSERT(buddy_free(&buddy, allocation.chunk_index),
                        _ret_ == 0);
            }
        }
    }

    stat_buddy(&buddy);
    buddy_deinit(&buddy, release, NULL);

    DO_WITH_ASSERT(buddy_init(&buddy, CHUNK_COUNT, NULL, NULL), _ret_ == 0);

    allocation_t allocation;
    while(allocation_set_pop(&set, &allocation))
    {
        DO_WITH_ASSERT(buddy_reserve(&buddy, allocation.chunk_index, allocation.order),
            _ret_ == 0);
    }

    stat_buddy(&buddy);
    
    buddy_deinit(&buddy, NULL, NULL);

    return 0;
}