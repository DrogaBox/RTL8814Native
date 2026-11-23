/*
 * awdlknob: reads and changes the AWDL layer's runtime knobs on awdl0, so one
 * boot compares settings that are otherwise boot-args
 * (RTLAwdInterface::setProperties, Source/AirLayer/AWDLInterface.cpp).
 *
 * With no arguments it prints the RTLAwdlKnobs property of the interface: the
 * knobs and the windows and data on the social channels and on the BSS's own.
 * With name=value arguments it sends them in one dictionary through
 * IORegistryEntrySetCFProperties, prints the IOReturn and then the property.
 * Changing a knob takes root: the layer checks for an administrator.
 *
 *   bss=0|1       serve the slots the 129 gives the BSS channel (rtlawdl_bss)
 *   mcc=0|1|2     time sharing while associated off the social channels (rtlawdl_mcc)
 *   mccrun=0..15  slots in a row a transfer takes from the BSS (rtlawdl_mccrun)
 *
 * Usage: awdlknob [bss=N] [mcc=N] [mccrun=N]
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach_error.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct {
	const char *arg;
	CFStringRef key;
} kKnobs[] = {
	{ "bss", CFSTR("RTLAwdlBss") },
	{ "mcc", CFSTR("RTLAwdlMcc") },
	{ "mccrun", CFSTR("RTLAwdlMccRun") },
};

static const char *const kShown[] = { "Bss", "Mcc", "MccRun", "WindowsSocial", "WindowsBss", "DataBss" };

static void usage(void)
{
	fprintf(stderr, "usage: awdlknob [bss=0|1] [mcc=0|1|2] [mccrun=0..15]\n"
	                "       with no arguments, prints RTLAwdlKnobs of awdl0\n");
}

static int printKnobs(io_service_t s)
{
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

int main(int argc, char **argv)
{
	if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		usage();
		return 0;
	}
	io_service_t s = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("RTLAwdInterface"));
	if (!s) {
		fprintf(stderr, "awdlknob: no RTLAwdInterface in the registry (awdl0 not created?)\n");
		return 1;
	}
	if (argc < 2) {
		int r = printKnobs(s);
		IOObjectRelease(s);
		return r;
	}
	CFMutableDictionaryRef d = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
	                                                     &kCFTypeDictionaryKeyCallBacks,
	                                                     &kCFTypeDictionaryValueCallBacks);
	if (!d) {
		IOObjectRelease(s);
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
			CFRelease(d);
			IOObjectRelease(s);
			return 2;
		}
		/* Only the digits are checked here: the range is the layer's to judge. */
		const char *val = eq + 1;
		char *end = NULL;
		errno = 0;
		unsigned long long v = strtoull(val, &end, 10);
		if (!*val || *val == '-' || *val == '+' || errno || !end || *end || v > INT64_MAX) {
			fprintf(stderr, "awdlknob: '%s' is not a number for %s\n", val, kKnobs[k].arg);
			CFRelease(d);
			IOObjectRelease(s);
			return 2;
		}
		long long sv = (long long)v;
		CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberLongLongType, &sv);
		if (!n) {
			CFRelease(d);
			IOObjectRelease(s);
			return 1;
		}
		CFDictionarySetValue(d, kKnobs[k].key, n);
		CFRelease(n);
	}
	kern_return_t r = IORegistryEntrySetCFProperties(s, d);
	CFRelease(d);
	printf("IOReturn 0x%08x (%s)\n", r, mach_error_string(r));
	printKnobs(s);
	IOObjectRelease(s);
	return r == KERN_SUCCESS ? 0 : 1;
}
