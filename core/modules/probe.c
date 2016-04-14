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

#include "../../profiler/profiler.h"

#define MP_NAME "mp_prof"
#define RING_NAME "ring_prof"

struct probe_priv {
    int id, enabled;
    unsigned n;
    double start;
    uint64_t n_pkts, n_drops;
    char probe_name[32];
    struct rte_mempool *mp;
    struct rte_ring *ring;
};

static inline double now_sec(void) {
    return rte_get_tsc_cycles() / (double) rte_get_tsc_hz();
}

static struct snobj *
probe_init(struct module *m, struct snobj *arg)
{
	struct probe_priv *priv = get_priv(m);
	int id = snobj_eval_int(arg, "id");
	int enabled = snobj_eval_int(arg, "enabled");
	assert(priv != NULL);
    priv->id = id;
    priv->n = 0;
    priv->n_pkts = 0;
    priv->n_drops = 0;
    priv->start = now_sec();
    priv->enabled = enabled;

    priv->mp = rte_mempool_lookup(MP_NAME);
    if (priv->mp == NULL)
		return snobj_err(ENOENT, "Mempool not allocated");

    priv->ring = rte_ring_lookup(RING_NAME);
    if (priv->ring == NULL)
		return snobj_err(ENOENT, "Ring not allocated");

	return NULL;
}

static void
probe_deinit(struct module *m)
{
    return;
}

struct snobj*
probe_query(struct module *m, struct snobj *q)
{
	struct probe_priv *priv = get_priv(m);
	assert(priv != NULL);

	int enabled = snobj_eval_int(q, "enabled");
    priv->enabled = enabled;
	struct snobj *r = snobj_map();
    snobj_map_set(r, "success", snobj_int(1));
    return r;
}

static void
probe_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct probe_priv *priv = get_priv(m);
    struct report *tbl[batch->cnt]; 
    struct ether_hdr *eth;
	struct ipv4_hdr *ip;
	struct udp_hdr *udp;
    uint8_t i, n = 0;

    double now = now_sec();

    if (priv->enabled == 0) {
        goto done;
    }

    if (rte_mempool_get_bulk(priv->mp, (void**)tbl, batch->cnt) != 0) {
        if (rte_ring_dequeue_bulk(priv->ring, (void**)tbl, batch->cnt) != 0) {
            goto done;
        }
        priv->n_drops += batch->cnt;
    }

	for (i = 0; i < batch->cnt; i++) {
		struct snbuf *snb = batch->pkts[i];
        eth = (struct ether_hdr*)snb_head_data(snb);

        if (eth->ether_type != rte_cpu_to_be_16(ETHER_TYPE_IPv4))
            continue;

        ip = (struct ipv4_hdr *)(eth + 1);

        if (ip->next_proto_id != 17 &&
            ip->next_proto_id != 6)
            continue;

        if ((ip->packet_id & rte_cpu_to_be_16(0x00FF)) != rte_cpu_to_be_16(0x00E2))
            continue;

        int ihl = (ip->version_ihl & IPV4_HDR_IHL_MASK) *
            IPV4_IHL_MULTIPLIER;

        udp = (struct udp_hdr*)(((char*)ip) + ihl);

        tbl[n]->src_addr = ip->src_addr;
        tbl[n]->dst_addr = ip->dst_addr;
        tbl[n]->src_port = udp->src_port;
        tbl[n]->dst_port = udp->dst_port;
        tbl[n]->protocol = ip->next_proto_id;
        tbl[n]->probe_id = priv->id;
        tbl[n]->prev_probe_id = rte_be_to_cpu_16(ip->packet_id) >> 8;
        ip->packet_id = (rte_cpu_to_be_16(0x00FF) & ip->packet_id) | rte_cpu_to_be_16(priv->id << 8);
        tbl[n++]->time_stamp = now;
    }

    rte_ring_enqueue_burst(priv->ring, (void**)tbl, n);
    rte_mempool_put_bulk(priv->mp, (void**)tbl + n, batch->cnt - n);
    priv->n_pkts += n;

done:
    if ((now - priv->start) >= 1.0f) {
        printf("%lu %lu %.9f\n", priv->n_drops, priv->n_pkts,
               priv->n_drops/(double)priv->n_pkts);
        priv->start = now;
        priv->n_pkts = 0;
        priv->n_drops = 0;
    }
	run_next_module(m, batch);
}

static const struct mclass probe = {
	.name		= "Probe",
	.priv_size 	= sizeof(struct probe_priv),
	.init		= probe_init,
	.deinit		= probe_deinit,
	.process_batch  = probe_process_batch,
	.query  = probe_query,
};

ADD_MCLASS(probe)
