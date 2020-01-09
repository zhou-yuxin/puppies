# perf_event_open()调用查看器

Linux自带的perf工具确实功能强大，但是很多时候我希望自己基于perf_event_open()接口来实现某个特定的功能。比如我想要实现类似于`perf mem -D -p record`这样的功能，即采样系统中所有进程的内存访问，以做内存冷热分析等等。之所以不能直接使用perf命令，一方面是因为perf只能把结果采集到文件中离线分析，另一方面是perf还会做很多解析工作而引入额外开销。而我需要实时地得到结果，而且不需要解析模块名称等信息。

那就自己基于perf_event_open()实现呗！可是，此时遇到了一个问题：perf_event_open()是一个非常复杂的系统调用，参数繁多，如何才能构造正确的参数？如何才能遵循正确的调用顺序？往往自己啃了手册还是“百试不得其解”。那么最好的办法就是观察perf工具是如何调用perf_event_open()的。该项目的目的就在于实现一个函数，把perf_event_open()的参数和返回值都以人类可阅读的方式打印出来，从而方便学习perf是如何工作的。

具体使用如下：

### STEP 1
下载perf的源码（perf源码集成在kernel源码中）。以4.19.11为例，下载[linux-4.19.11.tar.xz](https://cdn.kernel.org/pub/linux/kernel/v4.x/linux-4.19.11.tar.xz)并解压，然后进入tools/perf目录（以下称为当前目录）：
```
wget https://cdn.kernel.org/pub/linux/kernel/v4.x/linux-4.19.11.tar.xz
tar xf linux-4.19.11.tar.xz
cd linux-4.19.11/tools/perf
```

### STEP 2
把项目中的[perf_event_open_dump.h](./perf_event_open_dump.h)和[perf_event_open_dump.c](./perf_event_open_dump.c)复制到当前目录下：
```
cp path/to/perf_event_open_dump.h .
cp path/to/perf_event_open_dump.c .
```

### STEP 3
在当前目录下的Build文件的开头增加一行
```
perf-y += perf_event_open_dump.o
```

### STEP 4
在当前目录下的perf-sys.h文件的开头适当处增加
```
#include <stdio.h>
#include "perf_event_open_dump.h"
```
并在`sys_perf_event_open()`内部、`fd = syscall(__NR_perf_event_open, ...`下方加入
```
    {
        char* dump = perf_event_open_dump(attr, pid, cpu, group_fd, flags, fd);
        fprintf(stderr, "%s\n", dump);
        free(dump);
    }
```

### STEP 5
随后就可以`make`，得到./perf可执行文件。该perf在调用perf_event_open()时会打印出所有参数与返回值。

比如执行`./perf mem -D -p record`，可以得到很多诸如
```
================perf_event_open() DUMP===================
arg[0]:
    struct perf_event_attr attr = {
        .type= PERF_TYPE_RAW,
        .size= PERF_ATTR_SIZE_VER5,
        .config = 0x82d0,
        .sample_freq = 4000 /* Hz */,
        .sample_type= PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_ADDR | PERF_SAMPLE_ID | PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD | PERF_SAMPLE_WEIGHT | PERF_SAMPLE_DATA_SRC | PERF_SAMPLE_PHYS_ADDR,
        .read_format= PERF_FORMAT_ID,
        .disabled = 1,
        .inherit = 1,
        .pinned = 0,
        .exclusive = 0,
        .exclude_user = 0,
        .exclude_kernel = 0,
        .exclude_hv = 0,
        .exclude_idle = 0,
        .mmap = 0,
        .comm = 0,
        .freq = 1,
        .inherit_stat = 0,
        .enable_on_exec = 0,
        .task = 0,
        .watermark = 0,
        .precise_ip = 3 /* SAMPLE_IP must have 0 skid */,
        .mmap_data = 0,
        .sample_id_all = 1,
        .exclude_host = 0,
        .exclude_guest = 0,
        .exclude_callchain_kernel = 0,
        .exclude_callchain_user = 0,
        .mmap2 = 0,
        .comm_exec = 0,
        .use_clockid = 0,
        .context_switch = 0,
        .write_backward = 0,
        .namespaces = 0,
        .wakeup_events = 0 /* events to wakeup */,
        .bp_type= HW_BREAKPOINT_EMPTY,
        .config1 = 0x0,
        .config2 = 0x0,
        .branch_sample_type= 0,
        .sample_regs_user= 0,
        .sample_stack_user = 0,
        .clockid= CLOCK_REALTIME,
        .sample_regs_intr= 0,
        .aux_watermark = 0,
        .sample_max_stack = 0,
    };
arg[1]:
    pid_t pid = -1;
arg[2]:
    int cpu = 3;
arg[3]:
    int group_fd = -1;
arg[4]:
    unsigned long flags = PERF_FLAG_FD_CLOEXEC;
return:
    int fd = 23;
```
这样的输出。

## NOTES
由于`struct perf_event_attr`中的字段在不断增加，因此现在的代码用在过去的kernel上，可能会报错说xxx字段未定义，那么找到相应的代码注释掉即可。相反，如果用在更新的kernel上，那么可能就会遗漏某些字段。好在新增的字段通常都是被清零的。