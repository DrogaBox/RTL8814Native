#include "SecurityIE.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        failures++; \
    } \
} while (0)

int main(void)
{
    static const uint8_t rsn[] = {
        0x30,0x14, 0x01,0x00, 0x00,0x0f,0xac,0x04,
        0x01,0x00, 0x00,0x0f,0xac,0x04,
        0x01,0x00, 0x00,0x0f,0xac,0x08, 0xc0,0x00
    };
    uint8_t group = 0;
    int pmf = 0;
    CHECK(rtl_security_parse_rsn(rsn, sizeof(rsn), &group, &pmf) == 0);
    CHECK(group == 4 && pmf == 1);
    CHECK(rtl_security_parse_rsn(rsn, sizeof(rsn) - 1, &group, &pmf) != 0);
    CHECK(rtl_security_parse_rsn(rsn, 4, &group, &pmf) != 0);
    {
        uint8_t malformed[sizeof(rsn)];
        memcpy(malformed, rsn, sizeof(rsn));
        malformed[8] = 0xff;
        malformed[9] = 0xff;
        CHECK(rtl_security_parse_rsn(malformed, sizeof(malformed), &group, &pmf) != 0);
        memcpy(malformed, rsn, sizeof(rsn));
        malformed[1] = 0x13;
        CHECK(rtl_security_parse_rsn(malformed, sizeof(malformed), &group, &pmf) != 0);
    }

    {
        uint8_t kde[24] = {0xdd,22,0x00,0x0f,0xac,0x01,0x02,0x00};
        struct rtl_security_key_data parsed;
        memset(kde + 8, 0x5a, 16);
        CHECK(rtl_security_parse_key_data(kde, sizeof(kde), 0, &parsed) == 0);
        CHECK(parsed.has_gtk && parsed.gtk_len == 16 && parsed.gtk_key_id == 2);
        CHECK(parsed.gtk[0] == 0x5a && parsed.gtk[15] == 0x5a);
        CHECK(rtl_security_parse_key_data(kde, sizeof(kde) - 1, 0, &parsed) != 0);
        kde[1] = 6;
        CHECK(rtl_security_parse_key_data(kde, sizeof(kde), 0, &parsed) != 0);
        kde[1] = 255;
        CHECK(rtl_security_parse_key_data(kde, sizeof(kde), 0, &parsed) != 0);
        kde[1] = 21;
        CHECK(rtl_security_parse_key_data(kde, sizeof(kde) - 1, 0, &parsed) != 0);
        CHECK(parsed.has_gtk == 0);
    }

    {
        uint8_t kde[30] = {0xdd,28,0x00,0x0f,0xac,0x09,0x04,0x00};
        struct rtl_security_key_data parsed;
        kde[8] = 1;
        memset(kde + 14, 0xa5, 16);
        CHECK(rtl_security_parse_key_data(kde, sizeof(kde), 0, &parsed) == 0);
        CHECK(parsed.has_igtk && parsed.igtk_id == 4 && parsed.igtk_ipn == 1);
        CHECK(parsed.igtk[0] == 0xa5 && parsed.igtk[15] == 0xa5);
        CHECK(rtl_security_parse_key_data(kde, sizeof(kde) - 1, 0, &parsed) != 0);
        kde[1] = 27;
        CHECK(rtl_security_parse_key_data(kde, sizeof(kde), 0, &parsed) != 0);
    }

    if (failures) {
        fprintf(stderr, "%d security IE test(s) failed\n", failures);
        return 1;
    }
    puts("security IE tests passed");
    return 0;
}
