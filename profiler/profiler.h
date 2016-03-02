#ifndef _PROFILER_H_
#define _PROFILER_H_ 1

#include <rte_config.h>
#include <rte_common.h>
#include <rte_eal.h>
#include <rte_mempool.h>
#include <rte_ring.h>

#define MP_NAME "mp_prof"
#define MP_SIZE (1<<16)
#define MP_CACHE_SIZE (512)

#define RING_NAME "ring_prof"
#define RING_SIZE (512)
#define BURST_SIZE (32)

struct flow {
	uint32_t src_addr; //4
	uint32_t dst_addr; //4
	uint16_t src_port; //2
	uint16_t dst_port; //2
	uint8_t  protocol; //1
	uint8_t dummy[3];    //?3
}__attribute__ ((aligned (16)));

struct report {
    struct flow flow;
    uint32_t probe_id;
    double time_stamp;
};

#endif
