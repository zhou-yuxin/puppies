#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <asm/perf_regs.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>

#include "perf_event_open_dump.h"

int main() {
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_HW_CACHE;
    attr.size = PERF_ATTR_SIZE_VER5;
    attr.config = PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
    attr.sample_freq = 100;
    attr.freq = 1;
    attr.task = 1;
    attr.sample_type = PERF_SAMPLE_TID | PERF_SAMPLE_ADDR | PERF_SAMPLE_PHYS_ADDR | (1<<20);
    attr.read_format = PERF_FORMAT_ID;
    attr.wakeup_events = 100;
    attr.watermark = 1;
    attr.bp_type = HW_BREAKPOINT_RW;
    attr.branch_sample_type = PERF_SAMPLE_BRANCH_ANY_CALL | PERF_SAMPLE_BRANCH_USER;
    attr.sample_regs_user = (1ULL << PERF_REG_X86_BP) | (1ULL << PERF_REG_X86_R9);
    attr.sample_stack_user = 120;
    attr.clockid = CLOCK_BOOTTIME;
    attr.sample_regs_intr = (1ULL << PERF_REG_X86_BP) | (1ULL << PERF_REG_X86_R10);
    attr.aux_watermark = 11110;
    char* str = perf_event_open_dump(&attr, 0, 1, -1,
            PERF_FLAG_PID_CGROUP, 4);
    printf("%s\n", str);
    free(str);
    return 0;
}