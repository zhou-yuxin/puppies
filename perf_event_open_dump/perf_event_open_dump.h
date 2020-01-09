#ifndef PERF_EVENT_ATTR_DUMP_H
#define PERF_EVENT_ATTR_DUMP_H

#include <stdlib.h>
#include <sys/types.h>
#include <linux/perf_event.h>

// dump the arguments and result of perf_event_open(),
// return a dynamic allocated string, which should be
// deleted by free().
char* perf_event_open_dump(const struct perf_event_attr *attr,
        pid_t pid, int cpu, int group_fd, unsigned long flags,
        int fd);

#endif