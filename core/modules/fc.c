#include "../module.h"

#include "../utils/simd.h"
#include "../utils/random.h"
#include "flowtable.h"

#include <rte_byteorder.h>

/******************************************************************************/

struct fc_priv {
	int init;
	struct flow_table flow_table;
    uint64_t seed;
};

static struct snobj *fc_init(struct module *m, struct snobj *arg)
{
	struct fc_priv *priv = get_priv(m);
	int ret = 0;
	int size = snobj_eval_int(arg, "size");
	int bucket = snobj_eval_int(arg, "bucket");

	assert(priv != NULL);

	priv->init = 0;
    priv->seed = 1234;

	if (size == 0)
		size = DEFAULT_TABLE_SIZE;
	if (bucket == 0)
		bucket = MAX_BUCKET_SIZE;

	ret = ftb_init(&priv->flow_table, size, bucket);

	if (ret != 0) {
		return snobj_err(-ret,
				 "initialization failed with argument " \
                                 "size: '%d' bucket: '%d'\n",
				 size, bucket);
	}

	priv->init = 1;

	return NULL;
}

static void fc_deinit(struct module *m)
{
	struct fc_priv *priv = get_priv(m);

	if (priv->init) {
		priv->init = 0;
		ftb_deinit(&priv->flow_table);
	}
}

static inline struct snobj*
flow_from_snobj(struct snobj *entry, struct flow* flow) {
	if (snobj_type(entry) != TYPE_MAP) {
		return snobj_err(EINVAL,
				 "add must be given as a list of map");
	}

	struct snobj *src_addr = snobj_map_get(entry, "src_addr");
	struct snobj *dst_addr = snobj_map_get(entry, "dst_addr");
	struct snobj *src_port = snobj_map_get(entry, "src_port");
	struct snobj *dst_port = snobj_map_get(entry, "dst_port");
	struct snobj *protocol = snobj_map_get(entry, "protocol");

	struct snobj *gate = snobj_map_get(entry, "gate");

	if (!src_addr || snobj_type(src_addr) != TYPE_INT)
		return snobj_err(EINVAL, "Must supply source address");

	if (!dst_addr || snobj_type(dst_addr) != TYPE_INT)
		return snobj_err(EINVAL, 
				"Must supply destination address");

	if (!src_port || snobj_type(src_port) != TYPE_INT ||
			snobj_int_get(src_port) > UINT16_MAX)
		return snobj_err(EINVAL, 
				"Must supply valid source port");

	if (!dst_port || snobj_type(dst_port) != TYPE_INT ||
			snobj_int_get(dst_port) > UINT16_MAX)
		return snobj_err(EINVAL,
				"Must supply valid destination port");

	if (!protocol || snobj_type(protocol) != TYPE_INT ||
			snobj_int_get(protocol) > UINT8_MAX)
		return snobj_err(EINVAL,
				"Must supply valid protocol");

	if (!gate || snobj_type(gate) != TYPE_INT ||
			snobj_int_get(gate) >= INVALID_GATE)
		return snobj_err(EINVAL,
				"Must supply valid gate");


	flow->src_addr = (uint32_t) snobj_int_get(src_addr);
	flow->dst_addr = (uint32_t) snobj_int_get(dst_addr);
	flow->src_port = (uint16_t) snobj_int_get(src_port);
	flow->dst_port = (uint16_t) snobj_int_get(dst_port);
	flow->protocol = (uint8_t) snobj_int_get(protocol);
	return NULL;
}

static inline int 
extract_flow(struct snbuf *snb, struct flow *flow, uint16_t **pkt_id)
{
	struct ether_hdr *eth;
	struct ipv4_hdr *ip;
	struct udp_hdr *udp;

	eth = (struct ether_hdr*)snb_head_data(snb);

	if (eth->ether_type != rte_cpu_to_be_16(ETHER_TYPE_IPv4))
		return -1;

	ip = (struct ipv4_hdr *)(eth + 1);

    *pkt_id = &ip->packet_id;

	int ihl = (ip->version_ihl & IPV4_HDR_IHL_MASK) *
		IPV4_IHL_MULTIPLIER;

	if (ip->next_proto_id != 17 &&
	    ip->next_proto_id != 6)
		return -1;
	
	udp = (struct udp_hdr*)(((char*)ip) + ihl);

	flow->src_addr = ip->src_addr;
	flow->dst_addr = ip->dst_addr;
	flow->src_port = udp->src_port;
	flow->dst_port = udp->dst_port;
	flow->protocol = ip->next_proto_id;
	
	return 0;
}

__attribute__((optimize("unroll-loops")))
static void fc_process_batch(struct module *m, struct pkt_batch *batch)
{
	int r, i;
    uint64_t *n = NULL, j;
    uint16_t *pkt_id = NULL;

	struct fc_priv *priv = get_priv(m);

	for (i = 0; i < batch->cnt; i++) {
		struct snbuf *snb = batch->pkts[i];

		struct flow flow;
		
		if (extract_flow(snb, &flow, &pkt_id) == 0) {
			r = ftb_find(&priv->flow_table, &flow, &n);
			if (r != 0) {
                r = ftb_add_entry(&priv->flow_table, &flow, &n);
                if (r != 0) {
                    continue;
                }
            }
            if ((j = *n) < SAMPLE_SIZE || 
                (j = rand_fast_range(&priv->seed, *n)) < SAMPLE_SIZE) {
                *pkt_id = rte_cpu_to_be_16(0xBEEF);
            }
            *n = *n + 1;
        }
	}

	run_next_module(m, batch);
}

static const struct mclass fc = {
	.name            = "FCR",
	.priv_size       = sizeof(struct fc_priv),
	.init            = fc_init,
	.deinit          = fc_deinit,
	.process_batch   = fc_process_batch,
};

ADD_MCLASS(fc)
