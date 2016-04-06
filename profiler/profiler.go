package main

/*
#include "profiler.h"
*/
import "C"

import (
	"encoding/binary"
	"fmt"
	"os"
	"strings"

	"github.com/melvinw/go-dpdk"
)

const mpName = "mp_prof"
const ringName = "ring_prof"

type flow struct {
	srcAddr uint
	srcMask uint

	dstAddr uint
	dstMask uint

	srcPort uint
	dstPort uint

	proto uint
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

type adj map[uint]map[uint][]report

type policy struct {
	connections map[uint]map[uint]struct{}
	queries     map[flow][][]uint
}

func analyze(rChan chan report, p *policy) {
	A := make(adj)
	for r := range rChan {
		u := r.m.prevProbe
		v := r.m.probe
		if _, ok := A[u]; !ok {
			A[u] = make(map[uint][]report)
		}

		if _, ok := A[u][v]; !ok {
			A[u][v] = make([]report, 0)
		}

		A[u][v] = append(A[u][v], r)

		for f, paths := range p.queries {
			if r.f.srcAddr&f.srcMask == f.srcAddr {
				for _, p := range paths {
					succ := true
					u := p[len(p)-1]
					for i := len(p) - 2; i >= 0; i-- {
						v := p[i]
						if _, ok := A[u][v]; !ok {
							succ = false
							break
						}
						u = v
					}
					if succ {
						fmt.Println("Flow", r.f, "matched query path", p, " @ t =", r.m.timeStamp)
					}
				}
				break
			}
		}
	}
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
	p, err := parsePolicy(profArgs[0])
	if err != nil {
		fmt.Println("Failed to parse policy spec: ", err)
		return
	}
	fmt.Println(p)

	go analyze(rChan, p)

	for {
		nb_rx := uint(ring.DequeueBurst(tbl, 32))
		reports := dpdk.SliceFromCArray(tbl, nb_rx)
		for _, x := range reports {
			r := (*C.struct_report)(x)
			if r != nil {
				sABytes := make([]byte, 4)
				binary.LittleEndian.PutUint32(sABytes, uint32(r.src_addr))
				dABytes := make([]byte, 4)
				binary.LittleEndian.PutUint32(dABytes, uint32(r.dst_addr))
				sPBytes := make([]byte, 2)
				binary.LittleEndian.PutUint16(sPBytes, uint16(r.src_port))
				dPBytes := make([]byte, 2)
				binary.LittleEndian.PutUint16(dPBytes, uint16(r.dst_port))
				f := flow{
					uint(binary.BigEndian.Uint32(sABytes)),
					0xFFFFFFFF,
					uint(binary.BigEndian.Uint32(dABytes)),
					0xFFFFFFFF,
					uint(binary.BigEndian.Uint16(sPBytes)),
					uint(binary.BigEndian.Uint16(dPBytes)),
					uint(r.protocol),
				}
				m := meta{uint(r.prev_probe_id), uint(r.probe_id),
					float32(r.time_stamp)}
				rChan <- report{f, m}
			}
		}
		mp.PutBulk(tbl, nb_rx)
	}

	return
}
