#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <CoreFoundation/CoreFoundation.h>
int main(void) {
  void *h = dlopen("/System/Library/PrivateFrameworks/Apple80211.framework/Apple80211", RTLD_LAZY);
  if (!h) { printf("dlopen %s\n", dlerror()); return 1; }
  int (*op)(void **) = dlsym(h, "Apple80211Open");
  int (*bind)(void *, CFStringRef) = dlsym(h, "Apple80211BindToInterface");
  int (*raw)(void *, void *) = dlsym(h, "Apple80211RawGet");
  void *ctx = NULL;
  printf("open=%d\n", op(&ctx));
  printf("bind=%d\n", bind(ctx, CFSTR("en2")));
  uint8_t buf[64]; memset(buf, 0xAA, sizeof buf);
  uint8_t req[40]; memset(req, 0, sizeof req);
  *(uint32_t *)(req + 16) = 12; *(uint64_t *)(req + 24) = 0x15; *(void **)(req + 32) = buf;
  int r = raw(ctx, req);
  printf("rawget=%d\n", r);
  for (int i = 0; i < 28; i++) printf("%02x%s", buf[i], (i % 4 == 3) ? " " : "");
  printf("\n");
  return 0;
}
