#include "profiler.h"

#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int
init_db(sqlite3 **db)
{
    int ret = sqlite3_open(":memory:", db);
    if (ret) {
        sqlite3_close(*db);
        return 0 - ret;
    }

    char *query = "create table reports ( \
u integer, \
v integer, \
src_ip text, \
dst_ip text, \
src_port integer, \
dst_port integer, \
proto integer, \
ts real); \
delete from reports; \
create index idx_sip on reports (src_ip); \
create index idx_dip on reports (dst_ip); \
create index idx_sport on reports (src_port); \
create index idx_dport on reports (dst_port); \
create index idx_proto on reports (proto); \
create index idx_5tup on reports (src_ip, dst_ip, src_port, dst_port, proto); \
create index idx_u on reports (u); \
create index idx_v on reports (v); \
create index idx_edge on reports (u, v); \
create index idx_ts on reports (ts);";
    ret = sqlite3_exec(*db, query, NULL, NULL, NULL);
    if (ret) {
        sqlite3_close(*db);
        return 0 - ret;
    }

    sqlite3_exec(*db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);
    sqlite3_exec(*db, "PRAGMA count_changes=OFF", NULL, NULL, NULL);
    sqlite3_exec(*db, "PRAGMA journal_mode=MEMORY", NULL, NULL, NULL);
    sqlite3_exec(*db, "PRAGMA temp_store=MEMORY", NULL, NULL, NULL);

    return 0;
}

int
main(int argc, char**argv)
{
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_exit(ret, "failed to init EAL\n");
    }

    argc -= ret;
    argv += ret;

    if (argc < 2) {
        rte_exit(-1, "usage: %s DURATION_SECS\n", argv[0]);
    }

    double now,
           dur_msec = 1000 * strtod(argv[1], NULL),
           start = get_time_msec();

    printf("Recoding for %f us\n", dur_msec);

    struct rte_ring *ring = rte_ring_lookup("ring_prof");
    if (ring == NULL) {
        rte_exit(-1, "ring not setup\n");
    }

    struct rte_mempool *mp = rte_mempool_lookup("mp_prof");
    if (mp == NULL) {
        rte_exit(-1, "mempool not setup\n");
    }

    sqlite3 *db;
    ret = init_db(&db);
    if (ret < 0) {
        rte_exit(ret, "failed to init db\n");
    }

            
    char query[2048], src_ip[16], dst_ip[16],
         *query_template = "insert into \
reports(u, v, src_ip, dst_ip, src_port, dst_port, proto, ts) \
values(%u, %u, '%s', '%s', %u, %u, %u, %f)";

    struct report *tbl[BURST_SIZE], *r;
    unsigned i, nb_rx, n = 0;
    sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
    printf("===Begin TX===\n");
    while (((now = get_time_msec()) - start) < dur_msec) {
        nb_rx = rte_ring_dequeue_burst(ring, (void**)tbl, BURST_SIZE);
        for (i = 0; i < nb_rx; i++) {
            r = tbl[i];
            
            strcpy(src_ip, inet_ntoa(*(struct in_addr*)&(r->src_addr)));
            strcpy(dst_ip, inet_ntoa(*(struct in_addr*)(&r->dst_addr)));


            sprintf(query, query_template, r->prev_probe_id, r->probe_id,
                    src_ip, dst_ip, r->protocol, r->time_stamp);
            
            sqlite3_exec(db, query, NULL, NULL, NULL);
            /*
            sqlite3_stmt *stmt;
            sqlite3_prepare_v2(db, query, -1, &stmt, NULL);

            if ((ret = sqlite3_step(stmt)) != SQLITE_DONE)
                continue;
            sqlite3_finalize(stmt);
            */
            n++;
        }
        if (n >= 250000) {
            ret = sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
            if (ret) {
                printf("OH LORD!\n");
            }
            printf("===COMMIT===\n");
            sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
            printf("===Begin TX===\n");
            n = 0;
        }
        rte_mempool_put_bulk(mp, (void**)tbl, nb_rx);
    }

    sqlite3_close(db);

    return 0;
}
