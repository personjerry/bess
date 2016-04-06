#ifndef _PROFILER_H_
#define _PROFILER_H_ 1

#include <unistd.h>
#include <stdint.h>

struct report {
    uint32_t src_addr;
    uint32_t dst_addr;
    uint64_t src_port;
    uint64_t dst_port;
    uint64_t  protocol;

    uint32_t prev_probe_id;
    uint32_t probe_id;
    double time_stamp;
};

#endif
