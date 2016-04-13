#profiler: policy.peg.go profiler.go
#	go build

CC=gcc

DPDK_INC_DIR = ${RTE_SDK}/${RTE_TARGET}/include
DPDK_LIB_DIR = ${RTE_SDK}/${RTE_TARGET}/lib

LDFLAGS += -rdynamic -L${DPDK_LIB_DIR} -Wl,-rpath=${DPDK_LIB_DIR}
LIBS += -Wl,--whole-archive -ldpdk -Wl,--no-whole-archive -lm -lpthread \
		-ldl -lpcap -lsqlite3
CFLAGS += -std=gnu99 -g3 -ggdb3 -Ofast -m64 -march=native \
	  -Wall -Werror -Wno-unused-function -Wno-unused-but-set-variable \
 	  -I${DPDK_INC_DIR} -D_GNU_SOURCE

profiler: profiler.c
	${CC} ${CFLAGS} ${LDFLAGS} -o profiler profiler.c ${LIBS}
