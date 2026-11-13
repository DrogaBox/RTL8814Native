/*
 * Registers AirDrop traffic with an AWDL peer, as a real transfer does, so that
 * IO80211 caches the peer and sends it a UMI, and clears it afterwards.
 *
 *   clang -O2 -framework CoreFoundation -o awdl_traffic_reg Tools/awdl_traffic_reg.c
 *   codesign -f -s - --entitlements Tools/awdl_traffic_reg.plist awdl_traffic_reg
 *   ./awdl_traffic_reg 02:00:00:aa:bb:01 [seconds]
 *
 * Socket ioctls on awdl0 answer EOPNOTSUPP. The framework binds awdl0 in its
 * compatibility mode and Apple80211RawSet goes over XPC to wifip2pd, which
 * wants the com.apple.wifip2pd entitlement in the plist; no root is needed. An
 * ad-hoc signature only carries the entitlement with amfi_get_out_of_my_way=1.
 *
 * IO80211 (25G241) sends the UMI only to a peer that declares 6E in the
 * extended flags of its Data Path State TLV (handleAirDropTrafficReg,
 * 0xffffff80022087ac); to any other it logs "legacy peer ... skipped UMI".
 * awdlsim --6e declares it.
 *
 * Layout read from IO80211AWDLPeerManager::setAwdlPeerTrafficRegistration
 * (0xffffff800220946a) and the ioctl's handler (0xffffff8002289465).
 */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define IOC_AWDL_PEER_TRAFFIC_REGISTRATION 164

struct apple80211req {
	char ifname[IFNAMSIZ];
	uint32_t type;
	uint32_t val;
	uint32_t len;
	void *data;
};

struct traffic_reg {
	uint32_t version;       /* +0, must be 1 */
	uint8_t mac[6];         /* +4 */
	uint8_t pad0[2];
	uint32_t id_len;        /* +12, 1 to 128 */
	char id[128];           /* +16, its byte 127 is cleared by the kernel */
	uint8_t pad1[24];
	uint32_t active;        /* +168 */
	uint32_t hint;          /* +172 */
	uint8_t pad2[8];
};
_Static_assert(sizeof(struct traffic_reg) == 184, "the ioctl's handler wants 184 bytes");

static int (*a_open)(void **);
static int (*a_bind)(void *, CFStringRef);
static int (*a_rawset)(void *, struct apple80211req *);
static int (*a_close)(void *);

static int reg(void *h, const uint8_t mac[6], uint32_t active)
{
	struct traffic_reg r;
	struct apple80211req q;

	memset(&r, 0, sizeof(r));
	r.version = 1;
	memcpy(r.mac, mac, 6);
	strlcpy(r.id, "airdrop", sizeof(r.id));
	r.id_len = (uint32_t)strlen(r.id);
	r.active = active;

	memset(&q, 0, sizeof(q));
	strlcpy(q.ifname, "awdl0", sizeof(q.ifname));
	q.type = IOC_AWDL_PEER_TRAFFIC_REGISTRATION;
	q.len = sizeof(r);
	q.data = &r;

	int ret = a_rawset(h, &q);
	printf("active %u: 0x%x\n", active, ret);
	return ret;
}

int main(int argc, char **argv)
{
	unsigned m[6];
	uint8_t mac[6];

	if (argc < 2 || sscanf(argv[1], "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6) {
		fprintf(stderr, "usage: %s PEER_MAC [seconds]\n", argv[0]);
		return 2;
	}
	for (int i = 0; i < 6; i++)
		mac[i] = (uint8_t)m[i];

	void *fw = dlopen("/System/Library/PrivateFrameworks/IO80211.framework/IO80211", RTLD_NOW);
	if (!fw) {
		fprintf(stderr, "%s\n", dlerror());
		return 1;
	}
	a_open = (int (*)(void **))dlsym(fw, "Apple80211Open");
	a_bind = (int (*)(void *, CFStringRef))dlsym(fw, "Apple80211BindToInterface");
	a_rawset = (int (*)(void *, struct apple80211req *))dlsym(fw, "Apple80211RawSet");
	a_close = (int (*)(void *))dlsym(fw, "Apple80211Close");
	if (!a_open || !a_bind || !a_rawset || !a_close) {
		fprintf(stderr, "missing framework symbols\n");
		return 1;
	}

	void *h = NULL;
	int ret = a_open(&h);
	printf("open: 0x%x\n", ret);
	if (ret || !h)
		return 1;
	ret = a_bind(h, CFSTR("awdl0"));
	printf("bind: 0x%x\n", ret);
	if (ret) {
		a_close(h);
		return 1;
	}
	ret = reg(h, mac, 1);
	if (!ret) {
		sleep(argc > 2 ? (unsigned)atoi(argv[2]) : 10);
		reg(h, mac, 0);
	}
	a_close(h);
	return ret ? 1 : 0;
}
