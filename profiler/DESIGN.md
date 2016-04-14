# The Architecture
```
                              +------------+
                              | E2 Manager +-------------+
                              +------------+             |
                                                         |
                                                         |
                                                         |
+--------------------------------SERVER------------------+--------------------+
|+---------------------BESS/NFs------------------------+ | +-----------------+|
||             +------+    +------+    +------+        | +-|                 ||
||             | NF_u |    | NF_v |    | NF_w |        | | |                 ||
||---------------+--+--------+--+-_------+--+----------+-+ |                 ||
||  +-----+      |  |        |  |        |  |          |   |    Analyzer     ||
||i>| FC  |_[]___|  |___[]___|  |___[]___|  |___[]-->e>|   |                 ||
||  +-----+  |           |           |           |     |   |                 ||
|+-----------+-----------+-----------+-----------+-----+   +-----------------+|
|            |           |           |           |                  |         |
|            +===========+===========+===========+==================+         |
+-----------------------------------------------------------------------------+
FC: flow classifier/packet sampler
[]: instrumentation
= : rte_ring
```

Overview
--------
Each E2 server runs an extended version of BESS that classifies flows at ingress
and samples packets from every flow uniformly. These packets are then marked
by writing 0xE2 to the lower byte of their IP ID header. Further, each server is
instrumented with a small bit of code that runs between each pair of NFs that
sends information about packets that have been sampled to some external process
to be aggregated and processed.

Each NF x running on the server is assigned an identifier, id_x, which can be
locally or globabbly scoped depending on how the analyzer is configured (see
below).

Datapath
--------
The datapath uses the instrumentation ([] in the figure above) to perform the
following on each sampled packet p leaving an NF u toward an NF v:

+ Read id_u from the upper byte of the IP ID header of p
+ Write id_v into the upper byte of the IP ID header of p
+ Send a message of the form (5Tuple(p), id_u, id_v, t) to the analyzer via
  an rte_ring (FIFO queue) where t is the current time in seconds encoded as
  a double.

Analyzer
--------
The analyzer running locally can operator in one of two ways. The first of which
makes sense when you replicate at pipeline granularity, and the second when you
replicate at the granularity of NFs.

### First Approach
The analzyer maintains the following state:

+ An adjacency matrix M_f where M_f[u][v] is a array of at most k timestamps
  received through messages from the datapath for each flow f
+ A represntation of the graph of NFs running on the server,
  obtained from the E2 manager
+ A set of queries in the form of 5-tuples

When a message (f, u, v, t) from the dataplane arrives at the analyzer, it does
the following:

+ Lookup M_f, or initialize it if it doesn't exist
+ Append t to M_f[u][v]
+ If f satisfies a query (f, P) where P is some path through a running
  pipeline, send a message (f, P', t) to the E2 Manager where P' is the Path
  P annotated with the timestamps stored in M_f 

This state is all archived/cleared whenever a policy change occours.

###Second Approach
The analyzer acts as a buffer to an application running on top of the E2
manager i.e. when it receives a message (f, u, v, t) from the datapath, it
stores it and sends batches of messages to the application which would then
aggregate the messages from each server implementing the datapath and run
queries against the resulting globabl view.

Queries
-------
A query q is defined as a 2-tuple: (f, P), where P is a sub-path of the NFs that
make up any pipeline. An application running on top of the E2 manager maintains
a set of queries Q, populated through an interactive interface or some config
file.
