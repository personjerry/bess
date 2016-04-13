#ifndef _PROFILER_H_
#define _PROFILER_H_ 1

#include <unistd.h>
#include <stdint.h>

#include <rte_config.h>
#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_byteorder.h>
#include <rte_cycles.h>

#define BURST_SIZE 32

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

static double
get_time_msec(void)
{
    return 1000 * (rte_get_tsc_cycles() / (double) rte_get_tsc_hz());
}
#endif
