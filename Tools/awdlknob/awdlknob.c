/*
 * awdlknob: reads and changes the AWDL layer's runtime knobs on awdl0, so one
 * boot compares settings that are otherwise boot-args
 * (RTLAwdInterface::setProperties, Source/AirLayer/AWDLInterface.cpp).
 *
 * With no arguments it prints the RTLAwdlKnobs property of the interface: the
 * knobs and the windows and data on the social channels and on the BSS's own,
 * and the RTLLed property of the radio (RTL8814Native). With name=value
 * arguments it sends them through IORegistryEntrySetCFProperties, one
 * dictionary to awdl0 and one to the radio, prints each IOReturn and then the
 * properties. Changing a knob takes root: the layer and the radio check for an
 * administrator.
 *
 *   bss=0|1         serve the slots the 129 gives the BSS channel (rtlawdl_bss)
 *   mcc=0|1|2       time sharing while associated off the social channels (rtlawdl_mcc)
 *   mccrun=0..15    slots in a row a transfer takes from the BSS (rtlawdl_mccrun)
 *   aggsgi=0|1      A-MPDU short guard interval for peers that announce it (rtlawdl_aggsgi)
 *   aggmcs9=0|1     A-MPDU VHT MCS 9 for peers that announce it (rtlawdl_aggmcs9)
 *   aggden=0..8     A-MPDU density code, 8 = the peers' own (rtlawdl_aggden)
 *   aggburst=0..2000 air of an A-MPDU's preamble, BlockAck and backoff, us (rtlawdl_aggburst)
 *   led=0|1|2       the radio's LED: 0 register untouched, 1 status, 2 steady (rtlled)
 *
 * Usage: awdlknob [bss=N] [mcc=N] [mccrun=N] [aggsgi=N] [aggmcs9=N] [aggden=N]
 *                 [aggburst=N] [led=N]
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach_error.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* where a knob goes: awdl0 (RTLAwdInterface) or the radio (RTL8814Native) */
enum { kAwdl, kRadio, kTargets };

static const struct {
	const char *arg;
	CFStringRef key;
	int target;
} kKnobs[] = {
	{ "bss", CFSTR("RTLAwdlBss"), kAwdl },
	{ "mcc", CFSTR("RTLAwdlMcc"), kAwdl },
	{ "mccrun", CFSTR("RTLAwdlMccRun"), kAwdl },
	{ "aggsgi", CFSTR("RTLAwdlAggSgi"), kAwdl },
	{ "aggmcs9", CFSTR("RTLAwdlAggMcs9"), kAwdl },
	{ "aggden", CFSTR("RTLAwdlAggDen"), kAwdl },
	{ "aggburst", CFSTR("RTLAwdlAggBurst"), kAwdl },
	{ "led", CFSTR("RTLLed"), kRadio },
};

static const char *const kTargetName[kTargets] = { "awdl0", "radio" };
static const char *const kTargetClass[kTargets] = { "RTLAwdInterface", "RTL8814Native" };

static const char *const kShown[] = { "Bss", "Mcc", "MccRun", "WindowsSocial", "WindowsBss", "DataBss",
                                      "AggSgi", "AggMcs9", "AggDen", "AggBurst" };

static void usage(void)
{
	fprintf(stderr, "usage: awdlknob [bss=0|1] [mcc=0|1|2] [mccrun=0..15] [aggsgi=0|1] [aggmcs9=0|1]\n"
	                "                [aggden=0..8] [aggburst=0..2000] [led=0|1|2]\n"
	                "       with no arguments, prints RTLAwdlKnobs of awdl0 and RTLLed of the radio\n");
}

/* RTLLed of the radio: the LED mode (Source/Driver/StatusLed.h) */
static void printLed(io_service_t r)
{
	CFTypeRef v = r ? IORegistryEntryCreateCFProperty(r, CFSTR("RTLLed"), kCFAllocatorDefault, 0) : NULL;
	long long n = 0;
	if (v && CFGetTypeID(v) == CFNumberGetTypeID() && CFNumberGetValue((CFNumberRef)v, kCFNumberLongLongType, &n))
		printf("%-14s %lld\n", "Led", n);
	else
		printf("%-14s -\n", "Led");
	if (v)
		CFRelease(v);
}

static int printKnobs(io_service_t s)
{
	if (!s) {
		printf("no RTLAwdInterface in the registry (awdl0 not created?)\n");
		return 1;
	}
	CFTypeRef p = IORegistryEntryCreateCFProperty(s, CFSTR("RTLAwdlKnobs"), kCFAllocatorDefault, 0);
	if (!p || CFGetTypeID(p) != CFDictionaryGetTypeID()) {
		printf("no RTLAwdlKnobs on the interface (a layer without the knobs?)\n");
		if (p)
			CFRelease(p);
		return 1;
	}
	for (size_t i = 0; i < sizeof(kShown) / sizeof(kShown[0]); i++) {
		CFStringRef k = CFStringCreateWithCString(kCFAllocatorDefault, kShown[i], kCFStringEncodingUTF8);
		CFTypeRef v = k ? CFDictionaryGetValue((CFDictionaryRef)p, k) : NULL;
		long long n = 0;
		if (v && CFGetTypeID(v) == CFNumberGetTypeID() &&
		    CFNumberGetValue((CFNumberRef)v, kCFNumberLongLongType, &n))
			printf("%-14s %lld\n", kShown[i], n);
		else
			printf("%-14s -\n", kShown[i]);
		if (k)
			CFRelease(k);
	}
	CFRelease(p);
	return 0;
}

static void releaseAll(io_service_t *svc, CFMutableDictionaryRef *d)
{
	for (int t = 0; t < kTargets; t++) {
		if (d && d[t])
			CFRelease(d[t]);
		if (svc[t])
			IOObjectRelease(svc[t]);
	}
}

int main(int argc, char **argv)
{
	if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		usage();
		return 0;
	}
	io_service_t svc[kTargets];
	for (int t = 0; t < kTargets; t++)
		svc[t] = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching(kTargetClass[t]));
	if (argc < 2) {
		if (!svc[kAwdl] && !svc[kRadio]) {
			fprintf(stderr, "awdlknob: neither RTLAwdInterface nor RTL8814Native in the registry\n");
			return 1;
		}
		int r = printKnobs(svc[kAwdl]);
		printLed(svc[kRadio]);
		releaseAll(svc, NULL);
		return svc[kAwdl] ? r : 0;
	}
	CFMutableDictionaryRef d[kTargets];
	for (int t = 0; t < kTargets; t++)
		d[t] = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
		                                 &kCFTypeDictionaryValueCallBacks);
	if (!d[kAwdl] || !d[kRadio]) {
		releaseAll(svc, d);
		return 1;
	}
	for (int i = 1; i < argc; i++) {
		char *eq = strchr(argv[i], '=');
		size_t k;
		size_t nameLen = eq ? (size_t)(eq - argv[i]) : 0;
		for (k = 0; k < sizeof(kKnobs) / sizeof(kKnobs[0]); k++)
			if (eq && strlen(kKnobs[k].arg) == nameLen && !strncmp(argv[i], kKnobs[k].arg, nameLen))
				break;
		if (!eq || k == sizeof(kKnobs) / sizeof(kKnobs[0])) {
			fprintf(stderr, "awdlknob: unknown argument '%s'\n", argv[i]);
			usage();
			releaseAll(svc, d);
			return 2;
		}
		if (!svc[kKnobs[k].target]) {
			fprintf(stderr, "awdlknob: no %s in the registry for %s\n", kTargetClass[kKnobs[k].target],
			        kKnobs[k].arg);
			releaseAll(svc, d);
			return 1;
		}
		/* Only the digits are checked here: the range is the layer's to judge. */
		const char *val = eq + 1;
		char *end = NULL;
		errno = 0;
		unsigned long long v = strtoull(val, &end, 10);
		if (!*val || *val == '-' || *val == '+' || errno || !end || *end || v > INT64_MAX) {
			fprintf(stderr, "awdlknob: '%s' is not a number for %s\n", val, kKnobs[k].arg);
			releaseAll(svc, d);
			return 2;
		}
		long long sv = (long long)v;
		CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberLongLongType, &sv);
		if (!n) {
			releaseAll(svc, d);
			return 1;
		}
		CFDictionarySetValue(d[kKnobs[k].target], kKnobs[k].key, n);
		CFRelease(n);
	}
	int failed = 0;
	for (int t = 0; t < kTargets; t++) {
		if (!CFDictionaryGetCount(d[t]))
			continue;
		kern_return_t r = IORegistryEntrySetCFProperties(svc[t], d[t]);
		printf("%s: IOReturn 0x%08x (%s)\n", kTargetName[t], r, mach_error_string(r));
		failed |= r != KERN_SUCCESS;
	}
	if (svc[kAwdl])
		printKnobs(svc[kAwdl]);
	printLed(svc[kRadio]);
	releaseAll(svc, d);
	return failed;
}
