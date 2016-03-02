#include "profiler.h"

static void process_batch(struct report **tbl, unsigned count) {
    unsigned i;
    for (i = 0; i < count; i++) {
        if (tbl[i] == NULL)
            continue;
        printf("Got report from %u: (%u,%u,%u,%u,%u) t=%9.6f\n",
              tbl[i]->probe_id,
              tbl[i]->flow.src_addr, tbl[i]->flow.dst_addr, 
              tbl[i]->flow.src_port, tbl[i]->flow.dst_port, 
              tbl[i]->flow.protocol, tbl[i]->time_stamp);
    }
}

static void main_loop(struct rte_ring *ring, struct rte_mempool *mp) {
    struct report *tbl[BURST_SIZE];
    unsigned nb_rx;
    for (;;) {
        nb_rx = rte_ring_dequeue_burst(ring, (void**)tbl, BURST_SIZE);
        if (nb_rx > 0) {
            process_batch(tbl, nb_rx);
            rte_mempool_put_bulk(mp, (void* const*)tbl, nb_rx);
        }
    }
}

static void init(int argc, char **argv, struct rte_ring **ring, struct rte_mempool **mp) {
    int ret;

    /* Initialize EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
    }

    /* Create mempool */
#if 0
    mp = rte_mempool_create(MP_NAME, MP_SIZE,
                           sizeof(struct report),
                           MP_CACHE_SIZE,
                           0,
                           NULL, NULL,
                           NULL, NULL,
                           rte_socket_id(),
                           0);
#else
    *mp = rte_mempool_lookup(MP_NAME);
#endif
    if (*mp == NULL) {
        rte_exit(EXIT_FAILURE, "Failed to allocate mempool\n");
    }

    /* Create ring */
#if 0
    ring = rte_ring_create(RING_NAME, RING_SIZE, rte_socket_id(), RING_F_SC_DEQ);
#else
    *ring = rte_ring_lookup(RING_NAME);
#endif
    if (*ring == NULL) {
        rte_exit(EXIT_FAILURE, "Failed to allocate ring\n");
    }
}
