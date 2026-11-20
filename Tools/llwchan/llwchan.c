/*
 * llwchan: a UDP flow over llw0 the way a low latency app opens one, to check
 * llw0's datapath without Continuity Camera, and to measure its loss and delay.
 *
 * llw0 is a low latency interface: dlil attaches no flowswitch to it, so its
 * queues only come on when a process gets a flow from the netif nexus agent
 * ("Skywalk NetIf"); then IOSkywalkNetworkBSDClient connects and calls
 * enable(). Network.framework asks that agent for a flow on its own when a UDP
 * connection is pinned to llw0 ("channel-flow", flow:start_nexus). The agent
 * takes IPv6 ULA flows only (nx_netif_netagent_check_flags, flag 0x10) and
 * wants a local ULA address on the interface ("local addr missing" without one).
 *
 * The agent hands back only llw0's own MAC; the destination MAC is the app's
 * to give. Network.framework takes it from a remote endpoint made with
 * nw_endpoint_create_address_with_ethernet; a plain host endpoint leaves the
 * frames addressed to 00:00:00:00:00:00. Continuity Camera trades the MACs
 * over its control connection for the same reason.
 *
 * The addresses are fd00:: plus the EUI-64 of each MAC. The peer is awdlsim
 * --plain --mac <peer>, which sends UDP to port 9999 straight back.
 *
 * Every datagram carries a sequence number and its send time. One line per
 * second gives what went out and came back with the round trip; the report at
 * the end gives the losses grouped in runs of consecutive datagrams, the round
 * trip percentiles and the stalls (no datagram back for longer than 20 ms or
 * five send intervals, whichever is longer). A stall over 100 ms also gets a
 * line of its own with its length and when it ended, the time of day and the
 * second of the run, and the uptime of its start and end (the clock of test_uc
 * scanlog), to set beside the system log's scans: the round trip of
 * the datagrams after a scan also holds the queue built up during it.
 *
 * Usage: llwchan [-i llw0] [-s seconds] [-n count] [-r per-second] [-l bytes]
 *                [-p port] peer-mac
 */
#include <Network/Network.h>
#include <dispatch/dispatch.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <ifaddrs.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

typedef nw_interface_t (*create_with_name_fn)(const char *);
typedef nw_endpoint_t (*address_with_ethernet_fn)(const struct sockaddr *, const unsigned char *);

/* Datagram head: magic, sequence, send time in ns (little endian). */
#define HEAD_LEN 16
static const unsigned char MAGIC[4] = { 'L', 'L', 'W', 'C' };
/* llw0's MTU 1428 less the IPv6 and UDP headers. */
#define MAX_LEN 1380

static int parse_mac(const char *s, unsigned char m[6])
{
	unsigned int v[6];
	if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
		return -1;
	for (int i = 0; i < 6; i++) {
		if (v[i] > 0xff)
			return -1;
		m[i] = (unsigned char)v[i];
	}
	return 0;
}

/* fd00:: with the modified EUI-64 interface identifier of the MAC. */
static void ula_for(const unsigned char m[6], struct in6_addr *a, char *txt, size_t len)
{
	memset(a, 0, sizeof(*a));
	a->s6_addr[0] = 0xfd;
	a->s6_addr[8] = m[0] ^ 0x02;
	a->s6_addr[9] = m[1];
	a->s6_addr[10] = m[2];
	a->s6_addr[11] = 0xff;
	a->s6_addr[12] = 0xfe;
	a->s6_addr[13] = m[3];
	a->s6_addr[14] = m[4];
	a->s6_addr[15] = m[5];
	inet_ntop(AF_INET6, a, txt, (socklen_t)len);
}

/* The interface's MAC and whether it carries the given IPv6 address. */
static int if_state(const char *ifname, unsigned char mac[6], const struct in6_addr *want, int *has)
{
	struct ifaddrs *ifa, *i;
	int found = 0;
	*has = 0;
	if (getifaddrs(&ifa) != 0)
		return -1;
	for (i = ifa; i; i = i->ifa_next) {
		if (!i->ifa_addr || strcmp(i->ifa_name, ifname) != 0)
			continue;
		if (i->ifa_addr->sa_family == AF_LINK) {
			struct sockaddr_dl *sdl = (struct sockaddr_dl *)i->ifa_addr;
			if (sdl->sdl_alen == 6) {
				memcpy(mac, LLADDR(sdl), 6);
				found = 1;
			}
		} else if (want && i->ifa_addr->sa_family == AF_INET6) {
			struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)i->ifa_addr;
			if (memcmp(&s6->sin6_addr, want, sizeof(*want)) == 0)
				*has = 1;
		}
	}
	freeifaddrs(ifa);
	return found ? 0 : -1;
}

static void usage(void)
{
	fprintf(stderr, "usage: llwchan [-i llw0] [-s seconds] [-n count] [-r per-second] [-l bytes]\n"
	                "               [-p port] peer-mac\n"
	                "  -r datagrams per second (default 10), -l datagram size, %d to %d (default %d)\n",
	        HEAD_LEN, MAX_LEN, HEAD_LEN);
	exit(2);
}

static const char *conn_state(nw_connection_state_t s)
{
	switch (s) {
	case nw_connection_state_invalid: return "invalid";
	case nw_connection_state_waiting: return "waiting";
	case nw_connection_state_preparing: return "preparing";
	case nw_connection_state_ready: return "ready";
	case nw_connection_state_failed: return "failed";
	case nw_connection_state_cancelled: return "cancelled";
	}
	return "?";
}

static uint64_t now_ns(void)
{
	return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

static void put_le(unsigned char *p, uint64_t v, int n)
{
	for (int i = 0; i < n; i++)
		p[i] = (unsigned char)(v >> (8 * i));
}

static uint64_t get_le(const unsigned char *p, int n)
{
	uint64_t v = 0;
	for (int i = 0; i < n; i++)
		v |= (uint64_t)p[i] << (8 * i);
	return v;
}

static int cmp_u32(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	return x < y ? -1 : x > y;
}

/* Per datagram: round trip in us + 1, 0 while it has not come back. */
static uint32_t *rtt_of;
static unsigned long rtt_cap;

static int track(unsigned long seq)
{
	if (seq < rtt_cap)
		return 0;
	unsigned long cap = rtt_cap ? rtt_cap : 4096;
	while (cap <= seq)
		cap *= 2;
	uint32_t *n = realloc(rtt_of, cap * sizeof(*n));
	if (!n)
		return -1;
	memset(n + rtt_cap, 0, (cap - rtt_cap) * sizeof(*n));
	rtt_of = n;
	rtt_cap = cap;
	return 0;
}

static void report(unsigned long sent, unsigned long sent_ok, unsigned long sent_err, unsigned long rx,
                   unsigned long dup, unsigned long reorder, unsigned long bad, unsigned long stalls,
                   uint64_t stall_max_ns, unsigned long rate)
{
	printf("llwchan: sent %lu (ok %lu, error %lu), received %lu", sent, sent_ok, sent_err, rx);
	if (dup || reorder || bad)
		printf(", duplicates %lu, out of order %lu, unknown %lu", dup, reorder, bad);
	printf("\n");
	if (!sent)
		return;

	/* Losses, grouped in runs of consecutive sequence numbers. */
	unsigned long lost = 0, runs = 0, longest = 0, run = 0;
	unsigned long hist[5] = { 0 }; /* 1, 2-3, 4-7, 8-15, 16+ */
	for (unsigned long s = 0; s <= sent; s++) {
		if (s < sent && (s >= rtt_cap || rtt_of[s] == 0)) {
			lost++;
			run++;
			continue;
		}
		if (run) {
			runs++;
			if (run > longest)
				longest = run;
			hist[run >= 16 ? 4 : run >= 8 ? 3 : run >= 4 ? 2 : run >= 2 ? 1 : 0]++;
			run = 0;
		}
	}
	printf("lost %lu of %lu (%.2f%%)", lost, sent, 100.0 * (double)lost / (double)sent);
	if (runs) {
		printf(" in %lu runs, longest %lu", runs, longest);
		if (rate)
			printf(" (%.0f ms)", 1000.0 * (double)longest / (double)rate);
		printf("; runs of 1: %lu, 2-3: %lu, 4-7: %lu, 8-15: %lu, 16+: %lu",
		       hist[0], hist[1], hist[2], hist[3], hist[4]);
	}
	printf("\n");

	/* Round trip percentiles over everything that came back. */
	uint32_t *r = malloc((sent ? sent : 1) * sizeof(*r));
	unsigned long n = 0;
	for (unsigned long s = 0; r && s < sent && s < rtt_cap; s++)
		if (rtt_of[s])
			r[n++] = rtt_of[s] - 1;
	if (n) {
		qsort(r, n, sizeof(*r), cmp_u32);
		uint64_t sum = 0;
		for (unsigned long i = 0; i < n; i++)
			sum += r[i];
		printf("round trip ms: min %.2f, median %.2f, avg %.2f, p95 %.2f, p99 %.2f, max %.2f\n",
		       r[0] / 1000.0, r[n / 2] / 1000.0, (double)sum / (double)n / 1000.0,
		       r[(n * 95) / 100] / 1000.0, r[(n * 99) / 100] / 1000.0, r[n - 1] / 1000.0);
	}
	free(r);
	printf("stalls: %lu, longest %.1f ms\n", stalls, stall_max_ns / 1e6);
}

int main(int argc, char **argv)
{
	const char *ifname = "llw0";
	unsigned long seconds = 30, count = 0, port = 9999, rate = 10, size = HEAD_LEN;
	int ch;
	while ((ch = getopt(argc, argv, "i:s:n:p:r:l:")) != -1) {
		switch (ch) {
		case 'i': ifname = optarg; break;
		case 's': seconds = strtoul(optarg, NULL, 0); break;
		case 'n': count = strtoul(optarg, NULL, 0); break;
		case 'p': port = strtoul(optarg, NULL, 0); break;
		case 'r': rate = strtoul(optarg, NULL, 0); break;
		case 'l': size = strtoul(optarg, NULL, 0); break;
		default: usage();
		}
	}
	argc -= optind;
	argv += optind;
	unsigned char peer[6], mac[6];
	if (argc != 1 || parse_mac(argv[0], peer) != 0 || port == 0 || port > 65535 || rate == 0 ||
	    rate > 100000 || size < HEAD_LEN || size > MAX_LEN || seconds == 0)
		usage();

	struct in6_addr local6, peer6;
	char local_txt[64], peer_txt[64];
	int has;
	ula_for(peer, &peer6, peer_txt, sizeof(peer_txt));
	if (if_state(ifname, mac, NULL, &has) != 0) {
		fprintf(stderr, "no interface %s\n", ifname);
		return 1;
	}
	ula_for(mac, &local6, local_txt, sizeof(local_txt));
	if_state(ifname, mac, &local6, &has);
	if (!has) {
		printf("%s has no ULA yet. Once per boot, as root:\n", ifname);
		printf("  ifconfig %s inet6 %s prefixlen 64 alias\n", ifname, local_txt);
		printf("  ndp -s %s %02x:%02x:%02x:%02x:%02x:%02x\n", peer_txt,
		       peer[0], peer[1], peer[2], peer[3], peer[4], peer[5]);
		return 2;
	}

	create_with_name_fn create = (create_with_name_fn)dlsym(RTLD_DEFAULT, "nw_interface_create_with_name");
	nw_interface_t iface = create ? create(ifname) : NULL;
	if (!iface) {
		fprintf(stderr, "nw_interface_create_with_name failed for %s\n", ifname);
		return 1;
	}
	char port_txt[8];
	snprintf(port_txt, sizeof(port_txt), "%lu", port);
	nw_parameters_t params = nw_parameters_create_secure_udp(NW_PARAMETERS_DISABLE_PROTOCOL,
	                                                         NW_PARAMETERS_DEFAULT_CONFIGURATION);
	nw_parameters_require_interface(params, iface);
	nw_parameters_set_local_endpoint(params, nw_endpoint_create_host(local_txt, "0"));
	address_with_ethernet_fn with_ethernet =
		(address_with_ethernet_fn)dlsym(RTLD_DEFAULT, "nw_endpoint_create_address_with_ethernet");
	nw_endpoint_t remote;
	if (with_ethernet) {
		struct sockaddr_in6 sin6;
		memset(&sin6, 0, sizeof(sin6));
		sin6.sin6_len = sizeof(sin6);
		sin6.sin6_family = AF_INET6;
		sin6.sin6_port = htons((unsigned short)port);
		sin6.sin6_addr = peer6;
		remote = with_ethernet((const struct sockaddr *)&sin6, peer);
	} else {
		fprintf(stderr, "no nw_endpoint_create_address_with_ethernet: frames go to 00:00:00:00:00:00\n");
		remote = nw_endpoint_create_host(peer_txt, port_txt);
	}
	if (!remote) {
		fprintf(stderr, "could not make the remote endpoint\n");
		return 1;
	}
	nw_connection_t conn = nw_connection_create(remote, params);

	printf("llwchan: %s, %s -> [%s]:%lu, %lu s, %lu/s of %lu bytes\n", ifname, local_txt, peer_txt,
	       port, seconds, rate, size);
	fflush(stdout);

	dispatch_queue_t q = dispatch_queue_create("llwchan", DISPATCH_QUEUE_SERIAL);
	__block unsigned long sent = 0, sent_ok = 0, sent_err = 0, rx = 0, dup = 0, reorder = 0, bad = 0;
	__block unsigned long max_seq = 0, stalls = 0;
	__block uint64_t start = 0, last_rx = 0, stall_max = 0;
	__block int ready = 0, stopped = 0;
	__block dispatch_source_t timer = NULL;
	__block void (^receive)(void);
	/* The current second: datagrams out and back, round trip sum and max. */
	__block unsigned long sec = 0, sec_sent = 0, sec_rx = 0;
	__block uint64_t sec_sum = 0, sec_max = 0;
	uint64_t interval = 1000000000ull / rate;
	uint64_t stall_ns = 5 * interval > 20000000ull ? 5 * interval : 20000000ull;

	void (^second_line)(void) = ^{
		printf("[%4lu s] sent %lu back %lu", sec + 1, sec_sent, sec_rx);
		if (sec_rx)
			printf(", round trip avg %.2f max %.2f ms", (double)sec_sum / (double)sec_rx / 1e6,
			       sec_max / 1e6);
		printf("\n");
		fflush(stdout);
		sec++;
		sec_sent = sec_rx = 0;
		sec_sum = sec_max = 0;
	};

	receive = ^{
		nw_connection_receive_message(conn, ^(dispatch_data_t content, nw_content_context_t ctx,
		                                      bool done, nw_error_t err) {
			(void)ctx; (void)done;
			if (content) {
				uint64_t t = now_ns();
				unsigned char head[HEAD_LEN] = { 0 };
				const void *buf = NULL;
				size_t got = 0;
				dispatch_data_t flat = dispatch_data_create_map(content, &buf, &got);
				if (flat) {
					memcpy(head, buf, got < HEAD_LEN ? got : HEAD_LEN);
					dispatch_release(flat);
				}
				unsigned long seq = (unsigned long)get_le(head + 4, 4);
				uint64_t sent_at = get_le(head + 8, 8);
				if (got < HEAD_LEN || memcmp(head, MAGIC, 4) != 0 || seq >= sent || track(seq) != 0) {
					bad++;
				} else if (rtt_of[seq]) {
					dup++;
				} else {
					uint64_t rtt = t > sent_at ? t - sent_at : 0;
					uint64_t us = rtt / 1000;
					rtt_of[seq] = (uint32_t)(us < 0xfffffffe ? us : 0xfffffffe) + 1;
					if (rx++ == 0) {
						printf("first datagram back: %zu bytes\n", got);
						fflush(stdout);
					}
					if (seq < max_seq)
						reorder++;
					else
						max_seq = seq;
					sec_rx++;
					sec_sum += rtt;
					if (rtt > sec_max)
						sec_max = rtt;
				}
				if (last_rx && !stopped && t - last_rx > stall_ns) {
					stalls++;
					if (t - last_rx > stall_max)
						stall_max = t - last_rx;
					if (t - last_rx > 100000000ull) {
						struct timeval tv;
						struct tm tm;
						gettimeofday(&tv, NULL);
						localtime_r(&tv.tv_sec, &tm);
						/* uptime in ms: the clock of test_uc scanlog */
						printf("stall %.1f ms, ended %02d:%02d:%02d.%03d (%.3f s), uptime %.3f-%.3f ms\n",
						       (t - last_rx) / 1e6, tm.tm_hour, tm.tm_min, tm.tm_sec,
						       (int)(tv.tv_usec / 1000), (t - start) / 1e9,
						       last_rx / 1e6, t / 1e6);
						fflush(stdout);
					}
				}
				last_rx = t;
			}
			if (!err)
				receive();
		});
	};

	void (^finish)(void) = ^{
		if (stopped)
			return;
		stopped = 1;
		if (timer)
			dispatch_source_cancel(timer);
		/* A second for the stragglers, then the report. */
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), q, ^{
			if (sec_sent || sec_rx)
				second_line();
			report(sent, sent_ok, sent_err, rx, dup, reorder, bad, stalls, stall_max, rate);
			fflush(stdout);
			nw_connection_cancel(conn);
			dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), q, ^{
				exit(rx ? 0 : 1);
			});
		});
	};

	nw_connection_set_queue(conn, q);
	nw_connection_set_state_changed_handler(conn, ^(nw_connection_state_t st, nw_error_t err) {
		if (err)
			printf("state %s, error %d\n", conn_state(st), nw_error_get_error_code(err));
		else
			printf("state %s\n", conn_state(st));
		fflush(stdout);
		if (st != nw_connection_state_ready || ready)
			return;
		ready = 1;
		start = now_ns();
		receive();
		/* A 1 ms tick sends whatever the rate says is due by now. */
		timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, q);
		dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 0), NSEC_PER_MSEC, 0);
		dispatch_source_set_event_handler(timer, ^{
			uint64_t t = now_ns();
			while ((t - start) / NSEC_PER_SEC > sec)
				second_line();
			if (t - start >= seconds * NSEC_PER_SEC || (count && sent >= count)) {
				finish();
				return;
			}
			unsigned long due = (unsigned long)(((t - start) * rate) / NSEC_PER_SEC) + 1;
			if (count && due > count)
				due = count;
			for (int burst = 0; sent < due && burst < 64; burst++) {
				if (track(sent) != 0) {
					finish();
					return;
				}
				unsigned char *msg = calloc(1, size);
				if (!msg)
					return;
				memcpy(msg, MAGIC, 4);
				put_le(msg + 4, sent, 4);
				put_le(msg + 8, now_ns(), 8);
				dispatch_data_t d = dispatch_data_create(msg, size, q, DISPATCH_DATA_DESTRUCTOR_FREE);
				sent++;
				sec_sent++;
				nw_connection_send(conn, d, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT, true, ^(nw_error_t e) {
					if (e) {
						if (sent_err++ == 0)
							printf("send error %d\n", nw_error_get_error_code(e));
					} else {
						sent_ok++;
					}
				});
			}
		});
		dispatch_resume(timer);
	});
	nw_connection_start(conn);

	/* If the flow never comes up, still stop after the given time. */
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(seconds + 10) * NSEC_PER_SEC), q, ^{
		if (!ready) {
			printf("llwchan: the flow never came up\n");
			fflush(stdout);
			exit(1);
		}
	});
	dispatch_main();
}
