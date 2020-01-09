#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <asm/perf_regs.h>

#include "perf_event_open_dump.h"

#define INIT_BUFFER_CAPACITY        1024
#define UNUSED(x)                   ((void)x)

//==========================struct context===========================

struct context {
    const struct perf_event_attr *attr;
    unsigned long flags;
    char* buffer;
    size_t capacity;
    size_t length;
};

static int append(struct context* context, const char* str) {
    size_t len = strlen(str);
    size_t new_len = context->length + len;
    while (new_len >= context->capacity) {
        size_t new_capacity = context->capacity * 2;
        char* new_buf = realloc(context->buffer, new_capacity);
        if (!new_buf) {
            return errno;
        }
        context->buffer = new_buf;
        context->capacity = new_capacity;
    }
    assert(new_len < context->capacity);
    memcpy(context->buffer + context->length, str, len + 1);
    context->length = new_len;
    return 0;
}

//========================macro to dump==============================

#define DUMP_NUMBERIC_FIELD(context, field, format) ({      \
    int _ret;                                               \
    if (!(_ret = append(context, "." #field " = "))) {      \
        char _buffer[128];                                  \
        sprintf(_buffer, format, context->attr->field);     \
        _ret = append(context, _buffer);                    \
    }                                                       \
    _ret;                                                   \
})

#define ARRAY_COUNT(array)   (sizeof(array) / sizeof(typeof(*array)))

#define CONVERT_SERIAL_FIELD(value, strings, count) ({      \
    const char* _string = NULL;                             \
    assert(ARRAY_COUNT(strings) >= count);                  \
    if (value < ARRAY_COUNT(strings)) {                     \
        _string = strings[value];                           \
    }                                                       \
    _string;                                                \
})

#define DUMP_SERIAL_FIELD(context, field, strings, count) ({        \
    int _ret = -1;                                                  \
    const char* _str = CONVERT_SERIAL_FIELD(context->attr->field,   \
            strings, count);                                        \
    if (_str) {                                                     \
        if (!(_ret = append(context, "." #field "= "))) {           \
            _ret = append(context, _str);                           \
        }                                                           \
    }                                                               \
    _ret;                                                           \
})

#define CONVERT_DISCRETE_FIELD(value, values, strings) ({   \
    const char* _string = NULL;                             \
    size_t _i;                                              \
    assert(ARRAY_COUNT(values) == ARRAY_COUNT(strings));    \
    for (_i = 0; _i < ARRAY_COUNT(values); _i++) {          \
        if (values[_i] == value) {                          \
            _string = strings[_i];                          \
            break;                                          \
        }                                                   \
    }                                                       \
    _string;                                                \
})

#define DUMP_DISCRETE_FIELD(context, field, values, strings) ({     \
    int _ret = -1;                                                  \
    const char* _str = CONVERT_DISCRETE_FIELD(context->attr->field, \
            values, strings);                                       \
    if (_str) {                                                     \
        if (!(_ret = append(context, "." #field "= "))) {           \
            _ret = append(context, _str);                           \
        }                                                           \
    }                                                               \
    _ret;                                                           \
})

static int dump_bitwise(struct context* context, unsigned long long value,
        const char** strings, size_t count) {
    size_t bit_count = 0, i;
    int ret;
    for (i = 0; i < count; i++) {
        unsigned long long mask = 1ULL << i;
        if (value & mask) {
            if (bit_count) {
                if ((ret= append(context, " | "))) {
                    return ret;
                }
            }
            if ((ret= append(context, strings[i]))) {
                return ret;
            }
            bit_count++;
        }
    }
    value &= ~((1ULL << count) - 1);
    if (value) {
        char buffer[128];
        sprintf(buffer, "%s0x%llx", bit_count ? " | " : "", value);
        if ((ret= append(context, buffer))) {
            return ret;
        }
    }
    else if (bit_count == 0) {
        return append(context, "0");
    }
    return 0;
}

#define BITWISE_ENUM_COUNT(max)     (__builtin_ctzll(max))

#define DUMP_BITWISE_FIELD(context, field, strings, max) ({     \
    int _ret;                                                   \
    assert(ARRAY_COUNT(strings) >= BITWISE_ENUM_COUNT(max));    \
    if (!(_ret = append(context, "." #field "= "))) {           \
        _ret = dump_bitwise(context, context->attr->field,      \
                strings, BITWISE_ENUM_COUNT(max));              \
    }                                                           \
    _ret;                                                       \
})

//========================dump field type============================

static int dump_type(struct context* context) {
    static const char* strings[] = {
        "PERF_TYPE_HARDWARE",
        "PERF_TYPE_SOFTWARE",
        "PERF_TYPE_TRACEPOINT",
        "PERF_TYPE_HW_CACHE",
        "PERF_TYPE_RAW",
        "PERF_TYPE_BREAKPOINT",
    };
    int ret = DUMP_SERIAL_FIELD(context, type, strings, PERF_TYPE_MAX);
    return ret >= 0 ? ret :
            DUMP_NUMBERIC_FIELD(context, type, "%u /* unknown */");
}

//============================field size=============================

static int dump_size(struct context* context) {
    static const unsigned int values[] = {
#ifdef PERF_ATTR_SIZE_VER0
        PERF_ATTR_SIZE_VER0,
#endif
#ifdef PERF_ATTR_SIZE_VER1
        PERF_ATTR_SIZE_VER1,
#endif
#ifdef PERF_ATTR_SIZE_VER2
        PERF_ATTR_SIZE_VER2,
#endif
#ifdef PERF_ATTR_SIZE_VER3
        PERF_ATTR_SIZE_VER3,
#endif
#ifdef PERF_ATTR_SIZE_VER4
        PERF_ATTR_SIZE_VER4,
#endif
#ifdef PERF_ATTR_SIZE_VER5
        PERF_ATTR_SIZE_VER5,
#endif
    };
    static const char* strings[] = {
#ifdef PERF_ATTR_SIZE_VER0
        "PERF_ATTR_SIZE_VER0",
#endif
#ifdef PERF_ATTR_SIZE_VER1
        "PERF_ATTR_SIZE_VER1",
#endif
#ifdef PERF_ATTR_SIZE_VER2
        "PERF_ATTR_SIZE_VER2",
#endif
#ifdef PERF_ATTR_SIZE_VER3
        "PERF_ATTR_SIZE_VER3",
#endif
#ifdef PERF_ATTR_SIZE_VER4
        "PERF_ATTR_SIZE_VER4",
#endif
#ifdef PERF_ATTR_SIZE_VER5
        "PERF_ATTR_SIZE_VER5",
#endif
    };
    int ret = DUMP_DISCRETE_FIELD(context, size, values, strings);
    return ret >= 0 ? ret :
            DUMP_NUMBERIC_FIELD(context, size, "%u /* unknown */");
}

//===========================field config============================

static int dump_config_hardware(struct context* context) {
    static const char* strings[] = {
        "PERF_COUNT_HW_CPU_CYCLES",
        "PERF_COUNT_HW_INSTRUCTIONS",
        "PERF_COUNT_HW_CACHE_REFERENCES",
        "PERF_COUNT_HW_CACHE_MISSES",
        "PERF_COUNT_HW_BRANCH_INSTRUCTIONS",
        "PERF_COUNT_HW_BRANCH_MISSES",
	    "PERF_COUNT_HW_BUS_CYCLES",
        "PERF_COUNT_HW_STALLED_CYCLES_FRONTEND",
        "PERF_COUNT_HW_STALLED_CYCLES_BACKEND",
        "PERF_COUNT_HW_REF_CPU_CYCLES",
    };
    return DUMP_SERIAL_FIELD(context, config, strings, PERF_COUNT_HW_MAX);
}

static int dump_config_software(struct context* context) {
    static const char* strings[] = {
        "PERF_COUNT_SW_CPU_CLOCK",
        "PERF_COUNT_SW_TASK_CLOCK",
        "PERF_COUNT_SW_PAGE_FAULTS",
        "PERF_COUNT_SW_CONTEXT_SWITCHES",
        "PERF_COUNT_SW_CPU_MIGRATIONS",
        "PERF_COUNT_SW_PAGE_FAULTS_MIN",
        "PERF_COUNT_SW_PAGE_FAULTS_MAJ",
        "PERF_COUNT_SW_ALIGNMENT_FAULTS",
        "PERF_COUNT_SW_EMULATION_FAULTS",
        "PERF_COUNT_SW_DUMMY",
        "PERF_COUNT_SW_BPF_OUTPUT",
    };
    return DUMP_SERIAL_FIELD(context, config, strings, PERF_COUNT_SW_MAX);
}

static int dump_config_tracepoint(struct context* context) {
    UNUSED(context);
    // PERF_TYPE_TRACEPOINT should refer to
    // debugfs tracing/events/*/*/id if ftrace is enabled in the kernel
    return -1;
}

static int dump_config_cache(struct context* context) {
    static const char* id_strings[] = {
        "PERF_COUNT_HW_CACHE_L1D",
        "PERF_COUNT_HW_CACHE_L1I",
        "PERF_COUNT_HW_CACHE_LL",
        "PERF_COUNT_HW_CACHE_DTLB",
        "PERF_COUNT_HW_CACHE_ITLB",
        "PERF_COUNT_HW_CACHE_BPU",
        "PERF_COUNT_HW_CACHE_NODE",
    };
    static const char* op_id_strings[] = {
        "PERF_COUNT_HW_CACHE_OP_READ",
        "PERF_COUNT_HW_CACHE_OP_WRITE",
        "PERF_COUNT_HW_CACHE_OP_PREFETCH",
    };
    static const char* result_id_strings[] = {
        "PERF_COUNT_HW_CACHE_RESULT_ACCESS",
        "PERF_COUNT_HW_CACHE_RESULT_MISS",
    };
    unsigned long long config = context->attr->config;
    unsigned int id_value = config & 0xff;
    unsigned int op_id_value = (config >> 8) & 0xff;
    unsigned int result_id_value = (config >> 16) & 0xff;
    const char* id = CONVERT_SERIAL_FIELD(id_value,
            id_strings, PERF_COUNT_HW_CACHE_MAX);
    const char* op_id = CONVERT_SERIAL_FIELD(op_id_value,
            op_id_strings, PERF_COUNT_HW_CACHE_OP_MAX);
    const char* result_id = CONVERT_SERIAL_FIELD(result_id_value,
            result_id_strings, PERF_COUNT_HW_CACHE_RESULT_MAX);
    char buffer[128];
    if (id && op_id && result_id) {
        sprintf(buffer, ".config = (%s) | (%s << 8) | (%s << 16)",
                id, op_id, result_id);
        return append(context, buffer);
    }
    return -1;
}

static int dump_config_raw(struct context* context) {
    return DUMP_NUMBERIC_FIELD(context, config, "0x%llx");
}

static int dump_config_breakpoint(struct context* context) {
    UNUSED(context);
    // PERF_TYPE_BREAKPOINT should leave config set to zero.
    // Its parameters are set in other places.
    return -1;
}

static int dump_config(struct context* context) {
    unsigned int type = context->attr->type;
    typedef int (*func_t)(struct context*);
    static const func_t funcs[] = {
        dump_config_hardware,
        dump_config_software,
        dump_config_tracepoint,
        dump_config_cache,
        dump_config_raw,
        dump_config_breakpoint,
    };
    int ret = -1;
    assert(ARRAY_COUNT(funcs) >= PERF_TYPE_MAX);
    if (type < ARRAY_COUNT(funcs)) {
        ret = funcs[type](context);
    }
    return ret >= 0 ? ret :
            DUMP_NUMBERIC_FIELD(context, config, "%llu /* unknown */");
}

//=================field sample_period/sample_freq===================

static int dump_sample_period(struct context* context) {
    if (context->attr->freq) {
        return DUMP_NUMBERIC_FIELD(context,
                sample_freq, "%llu /* Hz */");
    }
    else {
        return DUMP_NUMBERIC_FIELD(context,
                sample_period, "%llu /* events per sample */");
    }
}

//========================field sample_type==========================

static int dump_sample_type(struct context* context) {
    static const char* strings[] = {
        "PERF_SAMPLE_IP",
        "PERF_SAMPLE_TID",
        "PERF_SAMPLE_TIME",
        "PERF_SAMPLE_ADDR",
        "PERF_SAMPLE_READ",
        "PERF_SAMPLE_CALLCHAIN",
        "PERF_SAMPLE_ID",
        "PERF_SAMPLE_CPU",
        "PERF_SAMPLE_PERIOD",
        "PERF_SAMPLE_STREAM_ID",
        "PERF_SAMPLE_RAW",
        "PERF_SAMPLE_BRANCH_STACK",
        "PERF_SAMPLE_REGS_USER",
        "PERF_SAMPLE_STACK_USER",
        "PERF_SAMPLE_WEIGHT",
        "PERF_SAMPLE_DATA_SRC",
        "PERF_SAMPLE_IDENTIFIER",
        "PERF_SAMPLE_TRANSACTION",
        "PERF_SAMPLE_REGS_INTR",
        "PERF_SAMPLE_PHYS_ADDR",
    };
    return DUMP_BITWISE_FIELD(context, sample_type, strings, PERF_SAMPLE_MAX);
}

//========================field precise_ip===========================

static int dump_precise_ip(struct context* context) {
    static const char* formats[] = {
        "%u /* SAMPLE_IP can have arbitrary skid */",
        "%u /* SAMPLE_IP must have constant skid */",
		"%u /* SAMPLE_IP requested to have 0 skid */",
		"%u /* SAMPLE_IP must have 0 skid */",
    };
    return DUMP_NUMBERIC_FIELD(context, precise_ip,
            formats[context->attr->precise_ip]);
}

//========================field read_format==========================

static int dump_read_format(struct context* context) {
    static const char* strings[] = {
        "PERF_FORMAT_TOTAL_TIME_ENABLED",
        "PERF_FORMAT_TOTAL_TIME_RUNNING",
        "PERF_FORMAT_ID",
        "PERF_FORMAT_GROUP",
    };
    return DUMP_BITWISE_FIELD(context, read_format, strings, PERF_FORMAT_MAX);
}

//================field wakeup_events/wakeup_watermark===============

static int dump_wakeup_events(struct context* context) {
    if (context->attr->watermark) {
        return DUMP_NUMBERIC_FIELD(context,
                wakeup_watermark, "%u /* bytes to wakeup */");
    }
    else {
        return DUMP_NUMBERIC_FIELD(context,
                wakeup_events, "%u /* events to wakeup */");
    }
}

//============================field bp_type==========================

static int dump_bp_type(struct context* context) {
    static const char* strings[] = {
        "HW_BREAKPOINT_EMPTY",
        "HW_BREAKPOINT_R",
        "HW_BREAKPOINT_W",
        "HW_BREAKPOINT_RW",
        "HW_BREAKPOINT_X",
    };
    int ret = DUMP_SERIAL_FIELD(context, bp_type, strings,
            ARRAY_COUNT(strings));
    return ret >= 0 ? ret :
            DUMP_NUMBERIC_FIELD(context, bp_type, "%u /* unknown */");
}

//============field bp_addr/kprobe_func/uprobe_path/config1==========

static int dump_bp_addr(struct context* context) {
    unsigned int type = context->attr->type;
    const char* key;
    char buffer[128];
    if (type == PERF_TYPE_BREAKPOINT) {
        key = "bp_addr";
    }
    else if (type == PERF_TYPE_RAW) {
        key = "config1";
    }
    else if (type == PERF_TYPE_TRACEPOINT) {
        key = "kprobe_func /* maybe uprobe_path */";
    }
    else {
        key = "bp_addr /* ignored */";
    }
    sprintf(buffer, ".%s = 0x%llx", key, context->attr->bp_addr);
    return append(context, buffer);
}

//============field bp_len/kprobe_addr/probe_offset/config2==========

static int dump_bp_len(struct context* context) {
    unsigned int type = context->attr->type;
    const char* key;
    char buffer[128];
    if (type == PERF_TYPE_BREAKPOINT) {
        key = "bp_len";
    }
    else if (type == PERF_TYPE_RAW) {
        key = "config2";
    }
    else if (type == PERF_TYPE_TRACEPOINT) {
        key = "kprobe_addr /* maybe probe_offset */";
    }
    else {
        key = "bp_len /* ignored */";
    }
    sprintf(buffer, ".%s = 0x%llx", key, context->attr->bp_len);
    return append(context, buffer);
}

//=====================field branch_sample_type======================

static int dump_branch_sample_type(struct context* context) {
    static const char* strings[] = {
        "PERF_SAMPLE_BRANCH_USER",
        "PERF_SAMPLE_BRANCH_KERNEL",
        "PERF_SAMPLE_BRANCH_HV",
        "PERF_SAMPLE_BRANCH_ANY",
        "PERF_SAMPLE_BRANCH_ANY_CALL",
        "PERF_SAMPLE_BRANCH_ANY_RETURN",
        "PERF_SAMPLE_BRANCH_IND_CALL",
        "PERF_SAMPLE_BRANCH_ABORT_TX",
        "PERF_SAMPLE_BRANCH_IN_TX",
        "PERF_SAMPLE_BRANCH_NO_TX",
        "PERF_SAMPLE_BRANCH_COND",
        "PERF_SAMPLE_BRANCH_CALL_STACK",
        "PERF_SAMPLE_BRANCH_IND_JUMP",
        "PERF_SAMPLE_BRANCH_CALL",
        "PERF_SAMPLE_BRANCH_NO_FLAGS",
        "PERF_SAMPLE_BRANCH_NO_CYCLES",
        "PERF_SAMPLE_BRANCH_TYPE_SAVE",
    };
    return DUMP_BITWISE_FIELD(context, branch_sample_type,
            strings, PERF_SAMPLE_BRANCH_MAX);
}

//===========field sample_regs_user and sample_regs_intr=============

#define SAMPLE_REGS_USER    0
#define SAMPLE_REGS_INTR    1

static int dump_sample_regs(struct context* context, int which) {
    static const char* strings[] = {
        "PERF_REG_X86_AX",
        "PERF_REG_X86_BX",
        "PERF_REG_X86_CX",
        "PERF_REG_X86_DX",
        "PERF_REG_X86_SI",
        "PERF_REG_X86_DI",
        "PERF_REG_X86_BP",
        "PERF_REG_X86_SP",
        "PERF_REG_X86_IP",
        "PERF_REG_X86_FLAGS",
        "PERF_REG_X86_CS",
        "PERF_REG_X86_SS",
        "PERF_REG_X86_DS",
        "PERF_REG_X86_ES",
        "PERF_REG_X86_FS",
        "PERF_REG_X86_GS",
        "PERF_REG_X86_R8",
        "PERF_REG_X86_R9",
        "PERF_REG_X86_R10",
        "PERF_REG_X86_R11",
        "PERF_REG_X86_R12",
        "PERF_REG_X86_R13",
        "PERF_REG_X86_R14",
        "PERF_REG_X86_R15",
    };
    if (which == SAMPLE_REGS_USER) {
        return DUMP_BITWISE_FIELD(context, sample_regs_user,
                strings, 1ULL << PERF_REG_X86_64_MAX);
    }
    else if (which == SAMPLE_REGS_INTR) {
        return DUMP_BITWISE_FIELD(context, sample_regs_intr,
                strings, 1ULL << PERF_REG_X86_64_MAX);
    }
    else {
        return -1;
    }
}

#define dump_sample_regs_user(context)      \
        dump_sample_regs(context, SAMPLE_REGS_USER)
#define dump_sample_regs_intr(context)      \
        dump_sample_regs(context, SAMPLE_REGS_INTR)

//=========================field clockid=============================

static int dump_clockid(struct context* context) {
    static const signed int values[] = {
#ifdef CLOCK_REALTIME
        CLOCK_REALTIME,
#endif
#ifdef CLOCK_MONOTONIC
        CLOCK_MONOTONIC,
#endif
#ifdef CLOCK_PROCESS_CPUTIME_ID
        CLOCK_PROCESS_CPUTIME_ID,
#endif
#ifdef CLOCK_THREAD_CPUTIME_ID
        CLOCK_THREAD_CPUTIME_ID,
#endif
#ifdef CLOCK_MONOTONIC_RAW
        CLOCK_MONOTONIC_RAW,
#endif
#ifdef CLOCK_REALTIME_COARSE
        CLOCK_REALTIME_COARSE,
#endif
#ifdef CLOCK_MONOTONIC_COARSE
        CLOCK_MONOTONIC_COARSE,
#endif
#ifdef CLOCK_BOOTTIME
        CLOCK_BOOTTIME,
#endif
#ifdef CLOCK_REALTIME_ALARM
        CLOCK_REALTIME_ALARM,
#endif
#ifdef CLOCK_BOOTTIME_ALARM
        CLOCK_BOOTTIME_ALARM,
#endif
#ifdef CLOCK_SGI_CYCLE
        CLOCK_SGI_CYCLE,
#endif
#ifdef CLOCK_TAI
        CLOCK_TAI,
#endif
    };
    static const char* strings[] = {
#ifdef CLOCK_REALTIME
        "CLOCK_REALTIME",
#endif
#ifdef CLOCK_MONOTONIC
        "CLOCK_MONOTONIC",
#endif
#ifdef CLOCK_PROCESS_CPUTIME_ID
        "CLOCK_PROCESS_CPUTIME_ID",
#endif
#ifdef CLOCK_THREAD_CPUTIME_ID
        "CLOCK_THREAD_CPUTIME_ID",
#endif
#ifdef CLOCK_MONOTONIC_RAW
        "CLOCK_MONOTONIC_RAW",
#endif
#ifdef CLOCK_REALTIME_COARSE
        "CLOCK_REALTIME_COARSE",
#endif
#ifdef CLOCK_MONOTONIC_COARSE
        "CLOCK_MONOTONIC_COARSE",
#endif
#ifdef CLOCK_BOOTTIME
        "CLOCK_BOOTTIME",
#endif
#ifdef CLOCK_REALTIME_ALARM
        "CLOCK_REALTIME_ALARM",
#endif
#ifdef CLOCK_BOOTTIME_ALARM
        "CLOCK_BOOTTIME_ALARM",
#endif
#ifdef CLOCK_SGI_CYCLE
        "CLOCK_SGI_CYCLE",
#endif
#ifdef CLOCK_TAI
        "CLOCK_TAI",
#endif
    };
    int ret = DUMP_DISCRETE_FIELD(context, clockid, values, strings);
    return ret >= 0 ? ret:
            DUMP_NUMBERIC_FIELD(context, clockid, "%d /* unknown */");
}

//=====================dump argument attr============================

static int dump_attr(struct context* context) {
    int ret;

#define COMPLEX(field)                              \
    if ((ret = append(context, "        ")) ||      \
            (ret = dump_##field(context)) ||        \
            (ret = append(context, ",\n"))) {       \
        return ret;                                 \
    }

#define NUMBERIC(field, format)                                     \
    if ((ret = append(context, "        ")) ||                      \
            (ret = DUMP_NUMBERIC_FIELD(context, field, format)) ||  \
            (ret = append(context, ",\n"))) {                       \
        return ret;                                                 \
    }

#define BIT(field)  NUMBERIC(field, "%u")

    COMPLEX(type);
    COMPLEX(size);
    COMPLEX(config);
    COMPLEX(sample_period);
    COMPLEX(sample_type);
    COMPLEX(read_format);
    BIT(disabled);
    BIT(inherit);
    BIT(pinned);
	BIT(exclusive);
	BIT(exclude_user);
	BIT(exclude_kernel);
	BIT(exclude_hv);
	BIT(exclude_idle);
    BIT(mmap);
    BIT(comm);
    BIT(freq);
    BIT(inherit_stat);
    BIT(enable_on_exec);
    BIT(task);
    BIT(watermark);
    COMPLEX(precise_ip);
    BIT(mmap_data);
    BIT(sample_id_all);
    BIT(exclude_host);
    BIT(exclude_guest);
    BIT(exclude_callchain_kernel);
    BIT(exclude_callchain_user);
    BIT(mmap2);
    BIT(comm_exec);
    BIT(use_clockid);
    BIT(context_switch);
    BIT(write_backward);
    BIT(namespaces);
    COMPLEX(wakeup_events);
    COMPLEX(bp_type);
    COMPLEX(bp_addr);
    COMPLEX(bp_len);
    COMPLEX(branch_sample_type);
    COMPLEX(sample_regs_user);
    NUMBERIC(sample_stack_user, "%u");
    COMPLEX(clockid);
    COMPLEX(sample_regs_intr);
    NUMBERIC(aux_watermark, "%u");
    NUMBERIC(sample_max_stack, "%u");

    return 0;
}

//=====================dump argument flags===========================

static int dump_flags(struct context* context) {
    static const char* strings[] = {
        "PERF_FLAG_FD_NO_GROUP",
        "PERF_FLAG_FD_OUTPUT",
        "PERF_FLAG_PID_CGROUP",
        "PERF_FLAG_FD_CLOEXEC",
    };
    return dump_bitwise(context, context->flags, strings, 4);
}

//=========================exported API==============================

char* perf_event_open_dump(const struct perf_event_attr *attr,
        pid_t pid, int cpu, int group_fd, unsigned long flags,
        int fd) {
    int ret;
    char buffer[128];
    struct context context;
    context.attr = attr;
    context.flags = flags;
    context.buffer = malloc(INIT_BUFFER_CAPACITY);
    if (!context.buffer) {
        return NULL;
    }
    context.capacity = INIT_BUFFER_CAPACITY;
    context.length = 0;
    if((ret = append(&context,
            "================perf_event_open() DUMP===================\n"
            "arg[0]:\n"
            "    struct perf_event_attr attr = {\n")) ||
            (ret = dump_attr(&context))) {
        goto error;
    }
    sprintf(buffer,
            "    };\n"
            "arg[1]:\n"
            "    pid_t pid = %d;\n"
            "arg[2]:\n"
            "    int cpu = %d;\n"
            "arg[3]:\n"
            "    int group_fd = %d;\n",
            pid, cpu, group_fd);
    if ((ret = append(&context, buffer))) {
        goto error;
    }
    if ((ret = append(&context,
            "arg[4]:\n"
            "    unsigned long flags = ")) ||
            (ret = dump_flags(&context)) ||
            (ret = append(&context, ";\n"))) {
        goto error;
    }
    sprintf(buffer,
            "return:\n"
            "    int fd = %d;\n",
            fd);
    if ((ret = append(&context, buffer))) {
        goto error;
    }
    return context.buffer;

error:
    free(context.buffer);
    return NULL;
}
