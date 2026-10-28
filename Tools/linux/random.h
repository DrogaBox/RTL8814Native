#ifndef RTL_EAPOL_TEST_LINUX_RANDOM_H
#define RTL_EAPOL_TEST_LINUX_RANDOM_H
#include <stddef.h>
#include <stdint.h>
void rtl_get_random_bytes(void *buf, int len);
int rtl_sae_entropy_fill(void *context, uint8_t *output, size_t length);
#endif
