#include "../log.h"
#include "../module.h"
#include "flowtable.h"

#include <rte_mempool.h>
#include <rte_ring.h>
#include <rte_errno.h>
#include <rte_byteorder.h>
#include <rte_cycles.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#define MP_NAME "mp_prof"
#define MP_SIZE (1<<16)

#define RING_NAME "ring_prof"

#define BURST_SIZE (32)

struct probe_priv {
    int id;
    unsigned n;
    struct rte_mempool *mp;
    struct rte_ring *ring;
};

struct report {
    uint64_t src_addr;
    uint64_t dst_addr;
    uint64_t src_port;
    uint64_t dst_port;
    uint64_t  protocol;

    uint32_t probe_id;
    double time_stamp;
};

static struct snobj *probe_init(struct module *m, struct snobj *arg) {
	struct probe_priv *priv = get_priv(m);
	int id = snobj_eval_int(arg, "id");
	assert(priv != NULL);
    priv->id = id;
    priv->n = 0;

    priv->mp = rte_mempool_lookup(MP_NAME);
    if (priv->mp == NULL)
		return snobj_err(ENOENT, "Mempool not allocated");

    priv->ring = rte_ring_lookup(RING_NAME);
    if (priv->ring == NULL)
		return snobj_err(ENOENT, "Ring not allocated");

	return NULL;
}

static void probe_deinit(struct module *m) {
    return;
}

static inline double now_sec(void) {
    return rte_get_tsc_cycles() / (double) rte_get_tsc_hz();
}

static void ship_burst(struct report **tbl, struct probe_priv *priv, uint8_t n) {
    if (n == 0) {
        return;
    }
    n -= rte_ring_enqueue_burst(priv->ring, (void**)tbl, n);
}

static void probe_process_batch(struct module *m, struct pkt_batch *batch) {
	struct probe_priv *priv = get_priv(m);
    struct report *tbl[batch->cnt]; 
    struct ether_hdr *eth;
	struct ipv4_hdr *ip;
	struct udp_hdr *udp;
    uint8_t i, n = 0;

    //while (rte_mempool_get_bulk(priv->mp, (void**)tbl, batch->cnt) < 0) {}

    double now = now_sec();
	for (i = 0; i < batch->cnt; i++) {
		struct snbuf *snb = batch->pkts[i];
        eth = (struct ether_hdr*)snb_head_data(snb);

        if (eth->ether_type != rte_cpu_to_be_16(ETHER_TYPE_IPv4))
            continue;

        ip = (struct ipv4_hdr *)(eth + 1);

        if (ip->next_proto_id != 17 &&
            ip->next_proto_id != 6)
            continue;

        if ((ip->packet_id & rte_cpu_to_be_16(0xBEEF)) == 0)
            continue;

        int ihl = (ip->version_ihl & IPV4_HDR_IHL_MASK) *
            IPV4_IHL_MULTIPLIER;

        udp = (struct udp_hdr*)(((char*)ip) + ihl);

        if (rte_mempool_get(priv->mp, (void**)&tbl[n]) < 0)
            continue;
        tbl[n]->src_addr = ip->src_addr;
        tbl[n]->dst_addr = ip->dst_addr;
        tbl[n]->src_port = udp->src_port;
        tbl[n]->dst_port = udp->dst_port;
        tbl[n]->protocol = ip->next_proto_id;
        tbl[n]->probe_id = priv->id;
        tbl[n++]->time_stamp = now;
    }

    ship_burst(tbl, priv, n);

	run_next_module(m, batch);
}

static const struct mclass probe = {
	.name		= "Probe",
	.priv_size 	= sizeof(struct probe_priv),
	.init		= probe_init,
	.deinit		= probe_deinit,
	.process_batch  = probe_process_batch,
};

ADD_MCLASS(probe)
