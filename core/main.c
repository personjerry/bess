#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/resource.h>

#include "debug.h"
#include "dpdk.h"
#include "master.h"
#include "worker.h"
#include "driver.h"
#include "log.h"

const struct global_opts global_opts;
static struct global_opts *opts = (struct global_opts *)&global_opts;

static void print_usage(char *exec_name)
{
	log_err("Usage: %s" \
		" [-t] [-c <core list>] [-p <port>] [-f] [-k] [-d]\n\n",
		exec_name);

	log_err("  %-16s Dump the size of internal data structures\n", 
			"-t");
	log_err("  %-16s Core ID for each worker (e.g., -c 0,8)\n",
			"-c <core list>");
	log_err("  %-16s Specifies the TCP port on which SoftNIC" \
			" listens for controller connections\n",
			"-p <port>");
	log_err("  %-16s Run BESS in foreground mode (for developers)\n",
			"-f");
	log_err("  %-16s Kill existing BESS instance, if any\n",
			"-k");
	log_err("  %-16s Show TC statistics every second\n",
			"-s");
	log_err("  %-16s Run BESS in debug mode (with debug log messages)\n",
			"-d");

	exit(2);
}

/* NOTE: At this point DPDK has not been initilaized, 
 *       so it cannot invoke rte_* functions yet. */
static void parse_args(int argc, char **argv)
{
	char c;

	num_workers = 0;

	while ((c = getopt(argc, argv, ":tc:p:fksd")) != -1) {
		switch (c) {
		case 't':
			dump_types();
			exit(EXIT_SUCCESS);
			break;

		case 'p':
			sscanf(optarg, "%hu", &opts->port);
			break;

		case 'f':
			opts->foreground = 1;
			break;

		case 'k':
			opts->kill_existing = 1;
			break;

		case 's':
			opts->print_tc_stats = 1;
			break;

		case 'd':
			opts->debug_mode = 1;
			break;

		case ':':
			log_err("argument is required for -%c\n", optopt);
			print_usage(argv[0]);
			break;

		case '?':
			log_err("Invalid command line option -%c\n", optopt);
			print_usage(argv[0]);
			break;

		default:
			assert(0);
		}
	}

	if (opts->foreground && !opts->print_tc_stats)
		log_info("TC statistics output is disabled (add -s option?)\n");
}

/* todo: chdir */
void check_user()
{
	uid_t euid;
	
	euid = geteuid();
	if (euid != 0) {
		log_err("ERROR: You need root privilege to run BESS daemon\n");
		exit(EXIT_FAILURE);
	}

	/* Great power comes with great responsibility */
	umask(S_IWGRP | S_IWOTH);
}

/* ensure unique instance */
void check_pidfile()
{
	char buf[1024];

	int fd;
	int ret;

	int trials = 0;

	pid_t pid;

	fd = open("/var/run/bessd.pid", O_RDWR | O_CREAT, 0644);
	if (fd == -1) {
		log_perr("open(/var/run/bessd.pid)");
		exit(EXIT_FAILURE);
	}

again:
	ret = flock(fd, LOCK_EX | LOCK_NB);
	if (ret) {
		if (errno != EWOULDBLOCK) {
			log_perr("flock(pidfile)");
			exit(EXIT_FAILURE);
		}

		/* lock is already acquired */
		ret = read(fd, buf, sizeof(buf) - 1);
		if (ret <= 0) {
			log_perr("read(pidfile)");
			exit(EXIT_FAILURE);
		}

		buf[ret] = '\0';

		sscanf(buf, "%d", &pid);

		if (trials == 0)
			log_notice("  There is another BESS daemon " \
				"running (PID=%d).\n", pid);

		if (!opts->kill_existing) {
			log_err("ERROR: You cannot run more than" \
				" one BESS instance at a time. " \
				"(add -k option?)\n");
			exit(EXIT_FAILURE);
		}

		trials++;

		if (trials <= 3) {
			log_info("  Sending SIGTERM signal...\n");

			ret = kill(pid, SIGTERM);
			if (ret < 0) {
				log_perr("kill(pid, SIGTERM)");
				exit(EXIT_FAILURE);
			}

			usleep(trials * 100000);
			goto again;

		} else if (trials <= 5) {
			log_info("  Sending SIGKILL signal...\n");

			ret = kill(pid, SIGKILL);
			if (ret < 0) {
				log_perr("kill(pid, SIGKILL)");
				exit(EXIT_FAILURE);
			}

			usleep(trials * 100000);
			goto again;
		}

		log_err("ERROR: Cannot kill the process\n");
		exit(EXIT_FAILURE);
	}

	if (trials > 0)
		log_info("  Old instance has been successfully terminated.\n");

	ret = ftruncate(fd, 0);
	if (ret) {
		log_perr("ftruncate(pidfile, 0)");
		exit(EXIT_FAILURE);
	}

	ret = lseek(fd, 0, SEEK_SET);
	if (ret) {
		log_perr("lseek(pidfile, 0, SEEK_SET)");
		exit(EXIT_FAILURE);
	}

	pid = getpid();
	ret = sprintf(buf, "%d\n", pid);
	
	ret = write(fd, buf, ret);
	if (ret < 0) {
		log_perr("write(pidfile, pid)");
		exit(EXIT_FAILURE);
	}

	/* keep the file descriptor open, to maintain the lock */
}

int daemon_start()
{
	int pipe_fds[2];
	const int read_end = 0;
	const int write_end = 1;

	pid_t pid;
	pid_t sid;

	int ret;

	log_info("Launching BESS daemon in background...\n");

	ret = pipe(pipe_fds);
	if (ret < 0) {
		log_perr("pipe()");
		exit(EXIT_FAILURE);
	}

	pid = fork();
	if (pid < 0) {
		log_perr("fork()");
		exit(EXIT_FAILURE);
	} else if (pid > 0) {
		/* parent */
		uint64_t tmp;

		close(pipe_fds[write_end]);

		ret = read(pipe_fds[read_end], &tmp, sizeof(tmp));
		if (ret == sizeof(uint64_t)) {
			log_info("Done (PID=%d).\n", pid);
			exit(EXIT_SUCCESS);
		} else {
			log_err("Failed. (syslog may have details)\n");
			exit(EXIT_FAILURE);
		}
	} else {
		/* child */
		close(pipe_fds[read_end]);
		/* fall through */
	}

	/* Start a new session */
	sid = setsid();
	if (sid < 0) {
		log_perr("setsid()");
		exit(EXIT_FAILURE);
	}

	return pipe_fds[write_end];
}

static void set_resource_limit()
{
	struct rlimit limit = {.rlim_cur = 65536, .rlim_max = 262144};

	for (;;) {
		int ret = setrlimit(RLIMIT_NOFILE, &limit);
		if (ret == 0)
			return;

		if (errno == EPERM && limit.rlim_cur >= 1024) {
			limit.rlim_max /= 2;
			limit.rlim_cur = MIN(limit.rlim_cur, limit.rlim_max);
			continue;
		}

		log_err("WARNING: setrlimit() failed\n");
		return;
	}
}

int main(int argc, char **argv)
{
	int signal_fd = -1;

	parse_args(argc, argv);

	check_user();
	
	if (opts->foreground)
		log_info("Launching BESS daemon in process mode...\n");
	else
		signal_fd = daemon_start();

	check_pidfile();
	set_resource_limit();

	start_logger();

	init_dpdk(argv[0]);
	init_mempool();
#if 1
#define PROF_MP_NAME "mp_prof"
#define PROF_MP_SIZE (1<<16)
#define PROF_MP_CACHE_SIZE (512)

#define PROF_RING_NAME "ring_prof"
#define PROF_RING_SIZE (1<<12)
    /* Create mempool */
    struct rte_mempool *mp = rte_mempool_create(PROF_MP_NAME, PROF_MP_SIZE, 192,
                           PROF_MP_CACHE_SIZE,
                           0,
                           NULL, NULL,
                           NULL, NULL,
                           rte_socket_id(),
                           0);
    if (mp == NULL) {
        rte_exit(EXIT_FAILURE, "Failed to allocate profiler mempool\n");
    }

    /* Create ring */
    struct rte_ring *ring = rte_ring_create(PROF_RING_NAME, PROF_RING_SIZE, rte_socket_id(), RING_F_SC_DEQ);
    if (ring == NULL) {
        rte_exit(EXIT_FAILURE, "Failed to allocate profiler ring\n");
    }
#endif
	init_drivers();

	setup_master(opts->port);

	/* signal the parent that all initialization has been finished */
	if (!opts->foreground) {
		int ret = write(signal_fd, &(uint64_t){1}, sizeof(uint64_t));
		if (ret < 0) {
			log_perr("write(signal_fd)");
			exit(EXIT_FAILURE);
		}
		close(signal_fd);
	}

	run_master();

	rte_eal_mp_wait_lcore();
	close_mempool();

	end_logger();

	return 0;
}
