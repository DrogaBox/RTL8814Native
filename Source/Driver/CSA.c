#include "CSA.h"

enum {
	EID_CSA = 37,
	EID_SEC_OFFSET = 62,
	EID_ECSA = 60,
	EID_CS_WRAPPER = 196,
	EID_WIDE_BW_CS = 194
};

static void csa_wide(const uint8_t *p, uint32_t n, struct rtl_csa *out)
{
	if (n >= 3) {
		out->wide_width = p[0];
		out->wide_seg0 = p[1];
	}
}

void rtl_csa_parse(const uint8_t *ies, uint32_t len, struct rtl_csa *out)
{
	uint32_t i = 0;

	out->found = out->mode = out->channel = out->count = out->sec_offset = out->wide_seg0 = 0;
	out->wide_width = RTL_CSA_NO_WIDE;

	while (i + 2 <= len) {
		uint8_t id = ies[i], n = ies[i + 1];
		const uint8_t *p = ies + i + 2;

		if (i + 2 + n > len)
			break;
		if (id == EID_CSA && n >= 3) {
			out->found = 1;
			out->mode = p[0];
			out->channel = p[1];
			out->count = p[2];
		} else if (id == EID_ECSA && n >= 4 && !out->found) {
			out->found = 1;
			out->mode = p[0];
			out->channel = p[2];
			out->count = p[3];
		} else if (id == EID_SEC_OFFSET && n >= 1) {
			out->sec_offset = p[0];
		} else if (id == EID_WIDE_BW_CS) {
			csa_wide(p, n, out);
		} else if (id == EID_CS_WRAPPER) {
			uint32_t j = 0;

			while (j + 2 <= n) {
				uint8_t sid = p[j], sn = p[j + 1];

				if (j + 2 + sn > n)
					break;
				if (sid == EID_WIDE_BW_CS)
					csa_wide(p + j + 2, sn, out);
				j += 2 + sn;
			}
		}
		i += 2 + n;
	}
}

uint8_t rtl_csa_pick_bw(const struct rtl_csa *csa, uint8_t cur_bw)
{
	uint8_t bw = csa->wide_width == 1 ? 2 : csa->sec_offset ? 1 : 0;

	return bw < cur_bw ? bw : cur_bw;
}
