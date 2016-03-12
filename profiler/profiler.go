package main

/*
#include "profiler.h"
*/
import "C"

import (
	"fmt"
	"os"
	"reflect"
	"strings"

	"github.com/melvinw/go-dpdk"
)

const mpName = "mp_prof"
const ringName = "ring_prof"

type flow struct {
	srcAddr uint
	dstAddr uint
	srcPort uint
	dstPort uint
	proto   uint
}

type meta struct {
	prevProbe uint
	probe     uint
	timeStamp float32
}

type report struct {
	f flow
	m meta
}

type edge struct {
	start      uint
	end        uint
	timeStamps []float32
}

type adj map[uint]map[uint]*edge

type policy struct {
	paths map[flow][]uint
}

func dfs(A adj, u uint) []*edge {
	if len(A[u]) == 0 {
		return []*edge{}
	}

	var v uint
	for k := range A[u] {
		v = k
	}
	return append(dfs(A, v), A[u][v])
}

func analyze(rChan chan report, p *policy) {
	flows := make(map[flow]adj)
	for r := range rChan {
		if _, ok := flows[r.f]; !ok {
			flows[r.f] = make(adj)
		}
		A := flows[r.f]

		if A[r.m.prevProbe] == nil {
			A[r.m.prevProbe] = make(map[uint]*edge)
		}

		e := A[r.m.prevProbe][r.m.probe]
		if e == nil {
			e = &edge{r.m.prevProbe, r.m.probe, []float32{}}
			A[r.m.prevProbe][r.m.probe] = e
		}
		e.timeStamps = append(e.timeStamps, r.m.timeStamp)

		path := dfs(A, 0)
		fmt.Println("Flow: ", r.f, " path:")
        for _, e := range path {
            fmt.Println("\t", *e)
        }
		if reflect.DeepEqual(path, p.paths[r.f]) {
			fmt.Println("Flow : ", r.f, " survived. Path: ", path)
			break
		}
	}
}

func parsePolicy(policyFile string) *policy {
	p := &policy{}
	return p
}

func main() {
	args := strings.Join(os.Args, " ")
	fmt.Println(args)
	splitArgs := strings.Split(args, " -- ")

	if len(splitArgs) < 2 {
		fmt.Println("Usage: [EAL options] -- PolicyFile.spec\n")
		return
	}

	ealArgs := strings.Split(splitArgs[0], " ")
	profArgs := strings.Split(splitArgs[1], " ")

	if dpdk.RteEalInit(ealArgs) < 0 {
		fmt.Println("Failed to init dpdk")
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
	tbl := dpdk.GetCArray(n)

	rChan := make(chan report)
	p := parsePolicy(profArgs[0])
	go analyze(rChan, p)

	for {
		nb_rx := uint(ring.DequeueBurst(tbl, 32))
		reports := dpdk.SliceFromCArray(tbl, nb_rx)
		for _, x := range reports {
			r := (*C.struct_report)(x)
			if r != nil {
				f := flow{uint(r.src_addr), uint(r.dst_addr), uint(r.src_port),
					uint(r.dst_port), uint(r.protocol)}
				m := meta{uint(r.prev_probe_id), uint(r.probe_id),
					float32(r.time_stamp)}
				rChan <- report{f, m}
			}
		}
		mp.PutBulk(tbl, nb_rx)
	}

	return
}
