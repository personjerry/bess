package main

/*
#include "profiler.h"
*/
import "C"

import (
	"fmt"
	"os"

	"github.com/melvinw/go-dpdk"
)

const mpName = "mp_prof"
const ringName = "ring_prof"

type flow struct {
	src_addr uint
	dst_addr uint
	src_port uint
	dst_port uint
	proto    uint
}

type meta struct {
	probe uint
	ts    float32
}

type report struct {
	f flow
	m meta
}

func analyze(rChan chan report) {
	flows := make(map[flow][]meta)
	for r := range rChan {
		flows[r.f] = append(flows[r.f], r.m)
	}
}

func main() {
	if dpdk.RteEalInit(os.Args) < 0 {
		fmt.Println("Failed to lookup ring")
		return
	}

	ring := dpdk.RteRingLookup(ringName)
	mp := dpdk.RteMemPoolLookup(mpName)

	if ring == nil {
		fmt.Println("Failed to lookup ring")
		return
	}

	if mp == nil {
		fmt.Println("Failed to lookup mempool")
		return
	}

	n := uint(32)
	tbl := util.GetCArray(n)

	rChan := make(chan report)
	go analyze(rChan)

	for {
		nb_rx := uint(ring.DequeueBurst(tbl, 32))
		reports := dpdk.SliceFromCArray(tbl, nb_rx)
		for _, x := range reports {
			r := (*C.struct_report)(x)
			if r != nil {
				f := flow{uint(r.src_addr), uint(r.dst_addr), uint(r.src_port),
					uint(r.dst_port), uint(r.protocol)}
				m := meta{uint(r.probe_id), float32(r.time_stamp)}
				rChan <- report{f, m}
			}
		}
		mp.PutBulk(tbl, nb_rx)
	}

	return
}
