# 基于交织内存的STL内存分配器

有了[interleave_mem](../interleave_mem)内核模块之后，如何让应用程序方便地使用起来呢？考虑到很多基础应用系统都是用C++实现的，而C++中的STL容器通常会占用很多内存，怎样才能自定义STL容器的内存分配行为？

好在STL的设计者们早就意识到了这个问题。细看每个STL容器的详细定义，会发现它们都是模板（不然怎么会叫做“标准模板库”），而最后一个模板参数都是Alloc。比如说std::vector的定义如下：
```
template <class T, class Alloc = allocator<T>>
class vector;
```
而std::list的定义如下：
```
template <class T, class Alloc = allocator<T>>
class list;
```
再复杂些，std::map的定义如下：
```
template <class Key, class T, class Compare = less<Key>,
          class Alloc = allocator<pair<const Key,T>>>
class map;
```
可见，我们只要实现一个allocator模板，就可以定义自己的容器。
一个通用的STL allocator定义如下：
```
#include <memory>

template <typename T>
class MyAllocator : public std::allocator<T> {
public:
    template <typename U>
    struct rebind {
        typedef MyAllocator<U> other;
    };

public:
    T* allocate(size_t n, const void* hint = 0) {
        printf("allocate(%lu, %p)\n", n, hint);
        return (T*)malloc(n * sizeof(T));
    }

    void deallocate(T* ptr, size_t n) {
        printf("deallocate(%p, %lu)\n", ptr, n);
        free(ptr);
    }
};
```
这里，我们使用malloc()和free()来管理内存，为了证明我确实redirect了内存分配行为，我在allocate()和deallocate()中加了printf()。当然，除了allocate()和deallocate()用来控制内存空间，std::allocator还有construct()和destroy()来控制构造和析构，具体用法可以参考C++手册。

注意一点，自定义的内存分配器不一定是继承自std::allocator，因为是模板编程，只要拥有相同“样子”的接口，在编译时能够找到正确的定义即可。我之所以选择继承自std::allocator，是因为这样我就能只关注我关心的部分（比如只管allocate()和deallocate()，而construct()和destroy()以及很多别的各种定义都fall back给原有的逻辑），让代码最简洁。还有，struct rebind是一定需要的（虽然我目前也清楚具体原理）。

这个自定义分配器用法如下：
```
//此处贴上面的代码

#include <vector>

int main() {
    std::vector<int, MyAllocator<int>> v;
    for (int i = 0; i < 1000; i++) {
        v.push_back(i);
    }
    return 0;
}
```
可以看到如下输出：
```
allocate(1, (nil))
allocate(2, (nil))
deallocate(0x1761280, 1)
allocate(4, (nil))
deallocate(0x17612a0, 2)
allocate(8, (nil))
deallocate(0x1761280, 4)
allocate(16, (nil))
deallocate(0x17612c0, 8)
allocate(32, (nil))
deallocate(0x17612f0, 16)
allocate(64, (nil))
deallocate(0x1761340, 32)
allocate(128, (nil))
deallocate(0x17613d0, 64)
allocate(256, (nil))
deallocate(0x17614e0, 128)
allocate(512, (nil))
deallocate(0x17616f0, 256)
allocate(1024, (nil))
deallocate(0x1761b00, 512)
deallocate(0x1762310, 1024)
```
每次空间不够了，就分配两倍的空间，释放原有空间。与vector的理论行为一致。

[interleave_mem.h](interleave_mem.h)提供了基于/dev/interleave_mem的STL allocator。只要include该头文件，就能自定义各种STL容器，让它们使用“交织内存”。示例如下：
```
#include <vector>
#include <interleave_mem.h>

int main() {
    InterleaveMem::GenericAllocator a(std::string("\x00", 1));
    InterleaveMem::STLAllocator<int>::engine = &a;

    std::vector<int, InterleaveMem::STLAllocator<int>> v;
    for (int i = 0; i < 1000; i++) {
        v.push_back(i);
    }
    return 0;
}
```
需要注意：
1) 进程中需要用到的所有STLAllocator&lt;T&gt;都是需要绑定其engine；
2) 传入GenericAllocator的pattern中的字符串必须是其数值等于节点编号（比如是"\x00\x01"，而不是"01"）；