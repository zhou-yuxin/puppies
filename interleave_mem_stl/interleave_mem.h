#ifndef INTERLEAVE_MEM_H
#define INTERLEAVE_MEM_H

#include <memory>
#include <string>
#include <stdexcept>

#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <jemalloc/jemalloc.h>

namespace InterleaveMem {

const char* DEVICE = "/dev/interleave_mem";

class GenericAllocator {
private:
    struct my_extent_hooks_t : extent_hooks_t {
        GenericAllocator* self;
    };

    void* m_base;
    size_t m_capacity;
    size_t m_allocated;
    my_extent_hooks_t m_extent_hooks;
    unsigned m_arena_index;
    int m_flags;

public:
    GenericAllocator(const std::string& pattern, size_t capacity = 1ULL << 40) {
        int err;
        char errmsg[256];
        int fd = open(DEVICE, O_RDWR);
        if (fd < 0) {
            sprintf(errmsg, "failed to open '%s': %s", DEVICE,
                        strerror(errno));
            throw std::runtime_error(errmsg);
        }
        ssize_t len = write(fd, pattern.data(), pattern.length());
        if (len != (ssize_t)pattern.length()) {
            err = errno;
            close(fd);
            sprintf(errmsg, "failed to write pattern: %s", strerror(err));
            throw std::runtime_error(errmsg);
        }
        m_base = mmap(NULL, capacity, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, 0);
        err = errno;
        close(fd);
        if (m_base == MAP_FAILED) {
            sprintf(errmsg, "failed to mmap %lu-size area: %s", capacity,
                        strerror(err));
            throw std::runtime_error(errmsg);
        }
        m_capacity = capacity;
        m_allocated = 0;
        redirect_extent_hooks();
        size_t index_size = sizeof(m_arena_index);
        err = mallctl("arenas.create",
                        (void*)&m_arena_index, &index_size, NULL, 0);
        if (err) {
            munmap(m_base, capacity);
            sprintf(errmsg, "failed to create arena: %s", strerror(err));
            throw std::runtime_error(errmsg);
        }
        char cmd[64];
        sprintf(cmd, "arena.%u.extent_hooks", m_arena_index);
        extent_hooks_t* phooks = &m_extent_hooks;
        err = mallctl(cmd, NULL, NULL, (void*)&phooks, sizeof(phooks));
        if (err) {
            munmap(m_base, capacity);
            sprintf(cmd, "arena.%u.destroy", m_arena_index);
            mallctl(cmd, NULL, NULL, NULL, 0);
            sprintf(errmsg, "failed to bind extent hooks: %s",
                        strerror(err));
            throw std::runtime_error(errmsg);
        }
        m_flags = MALLOCX_ARENA(m_arena_index) | MALLOCX_TCACHE_NONE;
    }

    ~GenericAllocator() {
        char cmd[64];
        sprintf(cmd, "arena.%u.destroy", m_arena_index);
        mallctl(cmd, NULL, NULL, NULL, 0);
        munmap(m_base, m_capacity);
    }

    void* malloc(size_t size) {
        return mallocx(size, m_flags);
    }

    void dalloc(void* ptr) {
        dallocx(ptr, m_flags);
    }

    void sdalloc(void* ptr, size_t size) {
        sdallocx(ptr, size, m_flags);
    }

private:
    void redirect_extent_hooks() {
        auto alloc = [](extent_hooks_t* extent_hooks,
                        void* new_addr, size_t size, size_t alignment,
                        bool* zero, bool* commit,
                        unsigned /*arena_index*/) -> void* {
            auto* self = ((my_extent_hooks_t*)extent_hooks)->self;
            return self->extent_alloc(new_addr, size, alignment,
                                        zero, commit);
        };
        auto purge_lazy = [](extent_hooks_t* extent_hooks,
                                void* addr, size_t size, size_t offset,
                                size_t length,
                                unsigned /*arena_index*/) -> bool {
            auto* self = ((my_extent_hooks_t*)extent_hooks)->self;
            return self->extent_purge_lazy(addr, size, offset, length);
        };
        auto purge_forced = [](extent_hooks_t* extent_hooks,
                                void* addr, size_t size, size_t offset,
                                size_t length,
                                unsigned /*arena_index*/) -> bool {
            auto* self = ((my_extent_hooks_t*)extent_hooks)->self;
            return self->extent_purge_forced(addr, size, offset, length);
        };
        auto split = [](extent_hooks_t *extent_hooks,
                        void *addr, size_t size, size_t size1, size_t size2,
                        bool committed, unsigned /*arena_index*/) -> bool {
            auto* self = ((my_extent_hooks_t*)extent_hooks)->self;
            return self->extent_split(addr, size, size1, size2, committed);
        };
        auto merge = [](extent_hooks_t* extent_hooks,
                        void *addr1, size_t size1, void *addr2, size_t size2,
                        bool committed, unsigned /*arena_index*/) -> bool {
            auto* self = ((my_extent_hooks_t*)extent_hooks)->self;
            return self->extent_merge(addr1, size1, addr2, size2, committed);
        };
        memset(&m_extent_hooks, 0, sizeof(m_extent_hooks));
        m_extent_hooks.alloc = alloc;
        m_extent_hooks.purge_lazy  = purge_lazy;
        m_extent_hooks.purge_forced = purge_forced;
        m_extent_hooks.split = split;
        m_extent_hooks.merge = merge;
        m_extent_hooks.self = this;
    }

    void* extent_alloc(void* new_addr, size_t size, size_t alignment,
                        bool* zero, bool* commit) {
        size_t addr;
        if (new_addr) {
            addr = (size_t)new_addr;            
        }
        else {
            addr = (size_t)m_base + m_allocated;
            size_t mod = addr % alignment;
            if (mod != 0) {
                addr += alignment - mod;
            }
        }
        assert(addr % alignment == 0);
        assert((size_t)m_base + m_allocated <= addr);
        size_t allocated = addr + size - (size_t)m_base;
        if (allocated > m_capacity) {
            return NULL;
        }
        assert(m_allocated < allocated);
        m_allocated = allocated;
        *zero = false;
        *commit = false;
        return (void*)addr;
    }

    bool extent_purge_lazy(void* addr, size_t size, size_t offset,
                            size_t length) {
        int ret = madvise((char*)addr + offset, length, MADV_DONTNEED);
        return ret == 0 ? false : true;
    }

    bool extent_purge_forced(void* addr, size_t size, size_t offset,
                            size_t length) {
        int ret = madvise((char*)addr + offset, length, MADV_DONTNEED);
        return ret == 0 ? false : true;
    }

    bool extent_split(void *addr, size_t size, size_t size1, size_t size2,
                        bool committed) {
        // success to split
        return false;
    }

    bool extent_merge(void *addr1, size_t size1, void *addr2, size_t size2,
                        bool committed) {
        // success to merge
        return false;
    }
};

template <typename T>
class STLAllocator : public std::allocator<T> {
public:
    template <typename U>
    struct rebind {
        typedef STLAllocator<U> other;
    };

    static GenericAllocator* engine;

public:
    T* allocate(size_t n, const void* hint = 0) {
        assert(engine);
        void* ptr = engine->malloc(n * sizeof(T));
        if (!ptr) {
            throw std::bad_alloc();
        }
        return (T*)ptr;
    }

    void deallocate(T* ptr, size_t n) {
        assert(engine);
        engine->sdalloc(ptr, n * sizeof(T));
    }
};

template <typename T>
GenericAllocator* STLAllocator<T>::engine = NULL;

}

#endif