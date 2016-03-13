package main

import (
	"encoding/binary"
	"io/ioutil"
	"net"
	"strconv"
	"strings"
)

func parseConnection(p *node32, pol *policy, buffer string) {
	first := false
	var u, v uint
	for p != nil {
		if p.pegRule == ruleprobe {
			if !first {
				i, _ := strconv.Atoi(buffer[p.begin:p.end])
				u = uint(i)
				if pol.connections[u] == nil {
					pol.connections[u] = make(map[uint]struct{})
				}
				first = true
			} else {
				i, _ := strconv.Atoi(buffer[p.begin:p.end])
				v = uint(i)
				pol.connections[u][v] = struct{}{}
			}
		}
		p = p.next
	}
}

func parseChain(p *node32, pol *policy, buffer string) []uint {
	var u uint
	for p != nil {
		if p.pegRule == ruleprobe {
			i, _ := strconv.Atoi(buffer[p.begin:p.end])
			u = uint(i)
		}
		if p.pegRule == ruleChain {
			return append(parseChain(p.up, pol, buffer), u)
		}
		p = p.next
	}
	return []uint{u}
}

func parseIP(s string) (uint, uint) {
	var retIP, retMask uint
	if strings.Contains(s, "/") {
		_, snet, _ := net.ParseCIDR(s)
		n := len(snet.IP)
		retIP = uint(binary.BigEndian.Uint32(snet.IP[n-4 : n]))
		n = len(snet.Mask)
		retMask = uint(binary.BigEndian.Uint32(snet.Mask[n-4 : n]))
	} else {
		sip := net.ParseIP(s)
		n := len(sip)
		retIP = uint(binary.BigEndian.Uint32(sip[n-4 : n]))
		retMask = 0xFFFFFFFF
	}
	return retIP, retMask
}

func parseFlow(p *node32, pol *policy, buffer string) flow {
	f := flow{}
	prefix1 := false
	proto := false
	sport := false
	for p != nil {
		if p.pegRule == ruleprefix && !prefix1 {
			f.srcAddr, f.srcMask = parseIP(buffer[p.begin:p.end])
			prefix1 = true
		} else if p.pegRule == ruleprefix {
			f.dstAddr, f.dstMask = parseIP(buffer[p.begin:p.end])
		}

		if p.pegRule == ruleprobe && !proto {
			i, _ := strconv.Atoi(buffer[p.begin:p.end])
			f.proto = uint(i)
			proto = true
		} else if p.pegRule == ruleprobe && !sport {
			i, _ := strconv.Atoi(buffer[p.begin:p.end])
			f.srcPort = uint(i)
			sport = true
		} else if p.pegRule == ruleprobe {
			i, _ := strconv.Atoi(buffer[p.begin:p.end])
			f.dstPort = uint(i)
		}

		p = p.next
	}
	return f
}

func parseCheck(p *node32, pol *policy, buffer string) {
	var f flow
	for p != nil {
		if p.pegRule == ruleFlow {
			f = parseFlow(p.up, pol, buffer)
			if _, ok := pol.queries[f]; !ok {
				pol.queries[f] = [][]uint{}
			}
		}
		if p.pegRule == ruleChain {
			probes := parseChain(p.up, pol, buffer)
			pol.queries[f] = append(pol.queries[f], probes)
		}
		p = p.next
	}
}

func parsePolicy(policyFile string) (*policy, error) {
	buffer, err := ioutil.ReadFile(policyFile)
	if err != nil {
		return nil, err
	}

	pol := &Policy{Buffer: string(buffer)}
	pol.Init()

	if err := pol.Parse(); err != nil {
		return nil, err
	}

	p := pol.AST()

	thePolicy := &policy{
		make(map[uint]map[uint]struct{}),
		make(map[flow][][]uint),
	}

	p = p.up
	for p != nil {
		if p.up == nil {
			continue
		}

		if p.pegRule == ruleConnection {
			parseConnection(p.up, thePolicy, pol.Buffer)
		}

		if p.pegRule == ruleCheck {
			parseCheck(p.up, thePolicy, pol.Buffer)
		}
		p = p.next
	}
	return thePolicy, nil
}
