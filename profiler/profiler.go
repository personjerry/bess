package main
/*
#cgo CFLAGS: -I/home/melvin/dpdk-2.2.0/x86_64-native-linuxapp-gcc/include -D_GNU_SOURCE
#cgo LDFLAGS: -L/home/melvin/dpdk-2.2.0/x86_64-native-linuxapp-gcc/lib -ldpdk -m64 -pthread -march=native -lm -ldl
#include "profiler.c"
 */
import "C"
import "unsafe"
import "os"
import "fmt"

type report struct {
    src_addr uint
        dst_addr uint
        src_port uint
        dst_port uint
        proto uint
        ts float32
}

func do_init(args [][]byte, ring **C.struct_rte_ring, mp **C.struct_rte_mempool) {
    var b *C.char
    ptrSize := unsafe.Sizeof(b)

    ptr := C.malloc(C.size_t(len(args)) * C.size_t(ptrSize))
    defer C.free(ptr)

    for i := 0; i < len(args); i++ {
        element := (**C.char)(unsafe.Pointer(uintptr(ptr) + uintptr(i)*ptrSize))
        *element = (*C.char)(unsafe.Pointer(&args[i][0]))
    }

    C.init(C.int(len(args)), (**C.char)(ptr), ring, mp)
}

func analyzeReports(c chan report) {
    for report := range c {
        fmt.Println(report)
    }
}

func main() {
    var args [][]byte
    for _, x := range os.Args {
        args = append(args, []byte(x))
    }

    var ring *C.struct_rte_ring
    var mp *C.struct_rte_mempool
    do_init(args, &ring, &mp)

    var p *C.struct_report
    tbl := C.malloc(C.size_t(32) * C.size_t(unsafe.Sizeof(p)))
    defer C.free(tbl)

    c := make(chan report)
    go analyzeReports(c)

    for {
        nb_rx := uint(C.rte_ring_dequeue_burst(ring, (*unsafe.Pointer)(tbl), 32))
        if nb_rx > 0 {
            arr := (*[1 << 30](*C.struct_report))(tbl)[:nb_rx:nb_rx]
            for _, x := range arr {
                r := *x
                c <- report{
                    uint(r.flow.src_addr), uint(r.flow.dst_addr),
                    uint(r.flow.src_port), uint(r.flow.dst_port),
                    uint(r.flow.protocol), float32(r.time_stamp),
                }
            }
        }
        C.rte_mempool_put_bulk(mp, (*unsafe.Pointer)(tbl), C.unsigned(nb_rx))
    }

    close(c)

    return
}
