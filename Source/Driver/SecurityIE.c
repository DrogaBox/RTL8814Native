#include "SecurityIE.h"

static void bytes_zero(void *destination, size_t length)
{
    uint8_t *p = (uint8_t *)destination;
    while (length--)
        *p++ = 0;
}

static void bytes_copy(void *destination, const void *source, size_t length)
{
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    while (length--)
        *out++ = *in++;
}

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

int rtl_security_parse_rsn(const uint8_t *ies, size_t ies_len,
                           uint8_t *group_cipher, int *pmf_required)
{
    size_t pos = 0;
    if (!ies || !group_cipher || !pmf_required)
        return -1;
    uint8_t parsed_group = 4;
    int parsed_pmf = 0;
    *group_cipher = 4;
    *pmf_required = 0;
    while (pos < ies_len) {
        size_t total;
        const uint8_t *p;
        size_t body_len;
        size_t off;
        uint16_t count;
        if (ies_len - pos < 2)
            return -1;
        total = (size_t)ies[pos + 1] + 2;
        if (total > ies_len - pos)
            return -1;
        p = ies + pos;
        body_len = total - 2;
        if (p[0] == 48) {
            if (body_len < 8 || read_le16(p + 2) != 1)
                return -1;
            if (p[4] == 0x00 && p[5] == 0x0f && p[6] == 0xac)
                parsed_group = p[7];
            off = 6;
            if (body_len - off < 2)
                return -1;
            count = read_le16(p + 2 + off);
            off += 2;
            if (!count || (size_t)count > (body_len - off) / 4)
                return -1;
            off += (size_t)count * 4;
            if (body_len - off < 2)
                return -1;
            count = read_le16(p + 2 + off);
            off += 2;
            if (!count || (size_t)count > (body_len - off) / 4)
                return -1;
            off += (size_t)count * 4;
            if (body_len - off == 1)
                return -1;
            if (body_len - off >= 2) {
                uint16_t capabilities = read_le16(p + 2 + off);
                parsed_pmf = (capabilities & 0x0040) != 0;
                off += 2;
                if (body_len - off == 1)
                    return -1;
                if (body_len - off >= 2) {
                    count = read_le16(p + 2 + off);
                    off += 2;
                    if ((size_t)count > (body_len - off) / 16)
                        return -1;
                    off += (size_t)count * 16;
                    if (body_len - off != 0 && body_len - off != 4)
                        return -1;
                }
            }
            *group_cipher = parsed_group;
            *pmf_required = parsed_pmf;
            return 0;
        }
        pos += total;
    }
    return -1;
}

int rtl_security_parse_key_data(const uint8_t *data, size_t data_len,
                                int allow_raw_gtk,
                                struct rtl_security_key_data *result)
{
    size_t pos = 0;
    if (!result || (!data && data_len))
        return -1;
    bytes_zero(result, sizeof(*result));
    if (allow_raw_gtk && data_len >= 16 && data[0] != 0xdd) {
        size_t key_len = data_len >= 32 ? 32 : 16;
        bytes_copy(result->gtk, data, key_len);
        result->gtk_len = key_len;
        result->gtk_key_id = 0xff;
        result->has_gtk = 1;
        return 0;
    }
    while (pos < data_len) {
        size_t element_len;
        const uint8_t *body;
        if (data_len - pos < 2) {
            /* a single trailing byte is the 0xdd pad of the key data */
            if (data[pos] == 0xdd || data[pos] == 0x00)
                break;
            goto malformed;
        }
        element_len = data[pos + 1];
        if (element_len > data_len - pos - 2)
            goto malformed;
        if (data[pos] == 0xdd && element_len == 0) {
            pos += 2;
            continue;
        }
        body = data + pos + 2;
        if (data[pos] == 0xdd && element_len >= 4 &&
            body[0] == 0x00 && body[1] == 0x0f && body[2] == 0xac) {
            if (body[3] == 1) {
                size_t key_len;
                if (element_len < 6)
                    goto malformed;
                key_len = element_len - 6;
                if (key_len != 16 && key_len != 32)
                    goto malformed;
                result->gtk_key_id = body[4] & 0x03;
                bytes_copy(result->gtk, body + 6, key_len);
                result->gtk_len = key_len;
                result->has_gtk = 1;
            } else if (body[3] == 9) {
                uint64_t ipn = 0;
                if (element_len != 28)
                    goto malformed;
                for (size_t i = 0; i < 6; i++)
                    ipn |= (uint64_t)body[6 + i] << (8U * i);
                result->igtk_id = read_le16(body + 4);
                bytes_copy(result->igtk, body + 12, sizeof(result->igtk));
                result->igtk_ipn = ipn;
                result->has_igtk = 1;
            }
        }
        pos += element_len + 2;
    }
    return 0;

malformed:
    bytes_zero(result, sizeof(*result));
    return -1;
}
