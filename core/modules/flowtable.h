/* A generic table for learning where flows (as defined by the 5 tuple) are
 * sent. Used by the DHT and ...
 */
#ifndef __FLOWTABLE_H__
#define __FLOWTABLE_H__
#include "../utils/simd.h"

#include <rte_hash_crc.h>
#include <rte_prefetch.h>

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#define MAX_TABLE_SIZE (1048576 * 64)
#define DEFAULT_TABLE_SIZE (1048576)
#define MAX_BUCKET_SIZE (4)

#define RESERVED_OCCUPIED_BIT (0x1ul)

#define MAX_INSERTION_SEARCH_DEPTH (2)

#define USE_RTEMALLOC (1)

#define SAMPLE_SIZE (2)

struct flow
{
	uint32_t src_addr; //4
	uint32_t dst_addr; //4
	uint16_t src_port; //2
	uint16_t dst_port; //2
	uint8_t  protocol; //1
	uint8_t dummy[3];    //?3
}__attribute__ ((aligned (16)));

struct flow_entry
{
	union {
		struct flow flow;
		uint64_t data[2];
		struct {
			uint16_t dummy[7];
			uint16_t gate:15;
			uint16_t occupied:1;
		};
	};
    uint64_t n;
} __attribute__ ((aligned (16)));

struct flow_table
{
	struct flow_entry *table;
	uint64_t size;
	uint64_t size_power;
	uint64_t bucket;
	uint64_t count;
	int32_t ref_count;
};

static inline int is_power_of_2(uint64_t n)
{
	return (n != 0 && ((n & (n - 1)) == 0));
}

/*
 * ftb_init:
 *  Initilizes the flow_table.
 *  It creates the slots of MAX_TABLE_SIZE multiplied by MAX_BUCKET_SIZE.
 *
 * @l2tbl: pointer to
 * @size: number of hash value entries. must be power of 2, greater than 0, and
 *        less than equal to MAX_TABLE_SIZE (2^30)
 * @bucket: number of slots per hash value. must be power of 2, greater than 0,
 *        and less than equal to MAX_BUCKET_SIZE (4)
 */
static int ftb_init(struct flow_table *tbl, int size, int bucket)
{
	if (size <= 0 || size > MAX_TABLE_SIZE ||
	    !is_power_of_2(size))
		return -EINVAL;

	if (bucket <= 0 || bucket > MAX_BUCKET_SIZE ||
	    !is_power_of_2(bucket))
		return -EINVAL;

	if (tbl == NULL)
		return -EINVAL;

#if USE_RTEMALLOC
	tbl->table =
		rte_zmalloc("tbl",
			    sizeof(struct flow_entry) * size * bucket, 0);
#else
	tbl->table =
		malloc(sizeof(struct flow_entry) * size * bucket);
#endif

	if (tbl->table == NULL)
		return -ENOMEM;

	tbl->size = size;
	tbl->bucket = bucket;

	/* calculates the log_2 (size) */
	tbl->size_power = 0;
	while (size > 1) {
		size = size >> 1;
		tbl->size_power += 1;
	}

	tbl->ref_count = 1;

	return 0;
}

static int ftb_deinit(struct flow_table *tbl)
{
	if (tbl == NULL)
		return -EINVAL;

	if (tbl->table == NULL)
		return -EINVAL;

	if (tbl->size == 0)
		return -EINVAL;

	if (tbl->bucket == 0)
		return -EINVAL;
	if (tbl->ref_count == 0)
		return -EINVAL;

	if (tbl->ref_count > 1) {
		tbl->ref_count--;
		return 0;
	}

#if USE_RTEMALLOC
	rte_free(tbl->table);
#else
	free(tbl->table);
#endif

	memset(tbl, 0, sizeof(struct flow_table));

	return 0;
}

/* Rudimentary refcounting to allow the table to be shared across BESS
 * components */
static int ftb_ref(struct flow_table *tbl) {
	if (tbl == NULL || tbl->table == NULL || tbl->size == 0
			|| tbl->bucket == 0 || tbl->ref_count <= 0) {
		return -EINVAL;
	}

	tbl->ref_count++;
	return 0;
}

static inline uint32_t 
ftb_ib_to_offset(struct flow_table *tbl, int index, int bucket)
{
	return index * tbl->bucket + bucket;
}

static inline uint32_t ftb_hash(const struct flow *flow)
{
	return rte_hash_crc(flow, sizeof(struct flow), 0);
}

static inline uint32_t ftb_hash_to_index(uint32_t hash, uint32_t size)
{
	return hash & (size - 1);
}

static inline uint32_t 
ftb_alt_index(uint32_t hash, uint32_t size_power, uint32_t index)
{
	uint64_t tag =  (hash >> size_power) + 1;
	tag = tag * 0x5bd1e995;
	return (index ^ tag) & ((0x1lu << (size_power - 1)) - 1);
}


static inline int 
ftb_find_index(struct flow *flow, struct flow_entry *table, 
		uint64_t bucket_size)
{
	struct flow_entry *fe = (struct flow_entry *) flow;

	for (uint32_t i = 0; i < (int)bucket_size; i++) {
		if (((fe->data[1] | (1ul<<63)) ==
		     (table[i].data[1] & 0x8000ffffFFFFffffUL)) &&
		    fe->data[0] == table[i].data[0]) {
			return i + 1;
		}
	}

	return 0;
}

static inline int ftb_find(struct flow_table *ftb,
			  struct flow *flow, uint64_t **n)
{
	int ret = -ENOENT;
	uint32_t hash, idx1, offset;
	struct flow_entry *tbl = ftb->table;

	hash = ftb_hash(flow);
	idx1 = ftb_hash_to_index(hash, ftb->size);

	offset = ftb_ib_to_offset(ftb, idx1, 0);

	int tmp1 = ftb_find_index(flow, &tbl[offset], ftb->bucket);
	if (tmp1) {
		*n  = &tbl[offset + tmp1 - 1].n;
		return 0;
	}

	idx1 = ftb_alt_index(hash, ftb->size_power, idx1);
	offset = ftb_ib_to_offset(ftb, idx1, 0);

	int tmp2 = ftb_find_index(flow, &tbl[offset], ftb->bucket);

	if (tmp2) {
		*n  = &tbl[offset + tmp2 - 1].n;
		return 0;
	}
	
	return ret;
}

static int ftb_find_offset(struct flow_table *ftb,
			   struct flow *flow, uint32_t *offset_out)
{
	uint32_t hash, idx1, offset;
	struct flow_entry *tbl = ftb->table;

	hash = ftb_hash(flow);
	idx1 = ftb_hash_to_index(hash, ftb->size);

	offset = ftb_ib_to_offset(ftb, idx1, 0);

	int tmp1 = ftb_find_index(flow, &tbl[offset], ftb->bucket);
	if (tmp1) {
		*offset_out = offset + tmp1 - 1;
		return 0;
	}

	idx1 = ftb_alt_index(hash, ftb->size_power, idx1);
	offset = ftb_ib_to_offset(ftb, idx1, 0);

	int tmp2 = ftb_find_index(flow, &tbl[offset], ftb->bucket);

	if (tmp2) {
		*offset_out = offset + tmp2 - 1;
		return 0;
	}

	return -ENOENT;
}

static int ftb_find_slot(struct flow_table *ftb, struct flow *flow,
			uint32_t *idx, uint32_t *bucket)
{
	int i, j;
	uint32_t hash;
	uint32_t idx1, idx_v1, idx_v2;
	uint32_t offset1, offset2;
	struct flow_entry *tbl = ftb->table;

	hash = ftb_hash(flow);
	idx1 = ftb_hash_to_index(hash, ftb->size);

	/* if there is available slot */
	for (i = 0; i < ftb->bucket; i++) {
		offset1 = ftb_ib_to_offset(ftb, idx1, i);
		if (!tbl[offset1].occupied) {
			*idx = idx1;
			*bucket = i;
			return 0;
		}
	}

	offset1 = ftb_ib_to_offset(ftb, idx1, 0);

	/* try moving */
	for (i = 0; i < ftb->bucket; i++) {
		offset1 = ftb_ib_to_offset(ftb, idx1, i);
		hash = ftb_hash(&tbl[offset1].flow);
		idx_v1 = ftb_hash_to_index(hash, ftb->size);
		idx_v2 = ftb_alt_index(hash, ftb->size_power, idx_v1);

		/* if the alternate bucket is same as original skip it */
		if (idx_v1 == idx_v2 || idx1 == idx_v2)
			break;

		for (j = 0; j < ftb->bucket; j++) {
			offset2 = ftb_ib_to_offset(ftb, idx_v2, j);
			if (!tbl[offset2].occupied) {
				/* move offset1 to offset2 */
				tbl[offset2] = tbl[offset1];
				/* clear offset1 */
				tbl[offset1].occupied = 0;

				*idx = idx1;
				*bucket = 0;
				return 0;
			}
		}
	}

	/* TODO:if alternate index is also full then start move */
	return -ENOMEM;
}

static int ftb_add_entry(struct flow_table *ftb, struct flow *flow, uint64_t **n)
{
	uint32_t offset;
	uint32_t index;
	uint32_t bucket;

	/* if addr already exist then fail */
	if (ftb_find(ftb, flow, n) == 0) {
		return -EEXIST;
	}

	/* find slots to put entry */
	if (ftb_find_slot(ftb, flow, &index, &bucket) != 0) {
		return -ENOMEM;
	}

	/* insert entry into empty slot */
	offset = ftb_ib_to_offset(ftb, index, bucket);

	ftb->table[offset].flow = *flow;
    ftb->table[offset].n = 0;
    ftb->table[offset].gate = 0;
	ftb->table[offset].occupied = 1;
	ftb->count++;
	*n = &ftb->table[offset].n;
	return 0;
}

static int ftb_del_entry(struct flow_table *ftb, struct flow *flow)
{
	uint32_t offset = 0xFFFFFFFF;

	if (ftb_find_offset(ftb, flow, &offset)) {
		return -ENOENT;
	}

	ftb->table[offset].data[0] = 0;
	ftb->table[offset].data[1] = 0;
	ftb->count--;
	return 0;
}

static int ftb_flush(struct flow_table *tbl)
{
	if (NULL == tbl)
		return -EINVAL;
	if (NULL == tbl->table)
		return -EINVAL;

	memset(tbl->table,
	       0,
	       sizeof(struct flow_entry) * tbl->size * tbl->bucket);

	return 0;
}


static uint64_t ftb_addr_to_u64(char* addr)
{
	uint64_t *addrp = (uint64_t*)addr;

	return  (*addrp & 0x0000FFffFFffFFfflu);
}

#endif
