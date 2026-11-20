//! --plain: the node's data as plain RFC 1042 frames, the way an iPhone
//! carries Continuity Camera's stream to llw0: `AA AA 03 00 00 00`, the
//! ethertype and the payload, with no AWDL head. crosswind only speaks the
//! AWDL head, so the frames are rewritten on the way out of the node and
//! back on the way in.

const SNAP: [u8; 8] = [0xaa, 0xaa, 0x03, 0x00, 0x17, 0xf2, 0x08, 0x00];
const RFC1042: [u8; 6] = [0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00];
/// SNAP 8 + 03 04 + sequence + 00 00 + ethertype.
const AWDL_HEAD: usize = 16;

/// Length of the 802.11 header of a data frame with no A-MSDU, or None.
fn data_hdr(f: &[u8]) -> Option<usize> {
    let hdr = match f.first()? {
        0x08 => 24,
        0x88 => 26,
        _ => return None,
    };
    if f.len() < hdr || (hdr == 26 && f[24] & 0x80 != 0) {
        return None;
    }
    Some(hdr)
}

/// An AWDL data frame from the node, rewritten plain; None leaves it alone.
pub fn to_plain(f: &[u8]) -> Option<Vec<u8>> {
    let hdr = data_hdr(f)?;
    let b = &f[hdr..];
    if b.len() < AWDL_HEAD || b[..8] != SNAP || b[8] != 0x03 || b[9] != 0x04 {
        return None;
    }
    let mut out = Vec::with_capacity(f.len() - 8);
    out.extend_from_slice(&f[..hdr]);
    out.extend_from_slice(&RFC1042);
    out.extend_from_slice(&b[14..]);
    Some(out)
}

/// A plain frame from the Mac (llw0's), given the AWDL head the node expects;
/// None when it is not one.
pub fn from_plain(f: &[u8], seq: u16) -> Option<Vec<u8>> {
    let hdr = data_hdr(f)?;
    let b = &f[hdr..];
    if b.len() < 8 || b[..6] != RFC1042 || u16::from_be_bytes([b[6], b[7]]) < 0x0600 {
        return None;
    }
    let mut out = Vec::with_capacity(f.len() + 8);
    out.extend_from_slice(&f[..hdr]);
    out.extend_from_slice(&SNAP);
    out.extend_from_slice(&[0x03, 0x04]);
    out.extend_from_slice(&seq.to_le_bytes());
    out.extend_from_slice(&[0, 0]);
    out.extend_from_slice(&b[6..]);
    Some(out)
}

/// The UDP port llwchan sends to (Tools/llwchan).
pub const REFLECT_PORT: u16 = 9999;

/// A plain IPv6 UDP datagram from the Mac to `port`, sent back with the MAC
/// addresses, the IPv6 addresses and the ports swapped. The UDP checksum
/// stays valid: the pseudo-header sum does not depend on the order. None
/// when it is not one.
pub fn reflect(f: &[u8], port: u16) -> Option<Vec<u8>> {
    let hdr = data_hdr(f)?;
    let b = &f[hdr..];
    if b.len() < 8 + 48 || b[..6] != RFC1042 || b[6..8] != [0x86, 0xdd] {
        return None;
    }
    let ip = hdr + 8;
    if f[ip] >> 4 != 6 || f[ip + 6] != 17 || u16::from_be_bytes([f[ip + 42], f[ip + 43]]) != port {
        return None;
    }
    let mut out = f.to_vec();
    out[4..10].copy_from_slice(&f[10..16]);
    out[10..16].copy_from_slice(&f[4..10]);
    out[ip + 8..ip + 24].copy_from_slice(&f[ip + 24..ip + 40]);
    out[ip + 24..ip + 40].copy_from_slice(&f[ip + 8..ip + 24]);
    out[ip + 40..ip + 42].copy_from_slice(&f[ip + 42..ip + 44]);
    out[ip + 42..ip + 44].copy_from_slice(&f[ip + 40..ip + 42]);
    Some(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn awdl_frame(qos: bool) -> Vec<u8> {
        let mut f = vec![0u8; if qos { 26 } else { 24 }];
        f[0] = if qos { 0x88 } else { 0x08 };
        f.extend_from_slice(&SNAP);
        f.extend_from_slice(&[0x03, 0x04, 0x34, 0x12, 0, 0, 0x86, 0xdd]);
        f.extend_from_slice(&[0x60, 1, 2, 3]);
        f
    }

    #[test]
    fn round_trip() {
        for qos in [false, true] {
            let f = awdl_frame(qos);
            let p = to_plain(&f).unwrap();
            let h = if qos { 26 } else { 24 };
            assert_eq!(&p[h..h + 8], &[0xaa, 0xaa, 0x03, 0, 0, 0, 0x86, 0xdd]);
            assert_eq!(&p[h + 8..], &[0x60, 1, 2, 3]);
            assert_eq!(from_plain(&p, 0x1234).unwrap(), f);
        }
    }

    #[test]
    fn leaves_others_alone() {
        let f = awdl_frame(false);
        assert!(from_plain(&f, 0).is_none());
        // a wrapped MSDU carries a length below 0x0600, not an ethertype
        let mut w = f[..24].to_vec();
        w.extend_from_slice(&[0xaa, 0xaa, 0x03, 0, 0, 0, 0x00, 0x14]);
        w.extend_from_slice(&f[24..]);
        assert!(from_plain(&w, 0).is_none());
        let mut mgmt = f.clone();
        mgmt[0] = 0xd0;
        assert!(to_plain(&mgmt).is_none());
    }

    #[test]
    fn reflects_udp_to_its_port() {
        let mut f = vec![0u8; 24];
        f[0] = 0x08;
        f[4..10].copy_from_slice(&[1; 6]);
        f[10..16].copy_from_slice(&[2; 6]);
        f.extend_from_slice(&[0xaa, 0xaa, 0x03, 0, 0, 0, 0x86, 0xdd]);
        let mut ip = vec![0x60, 0, 0, 0, 0, 10, 17, 64];
        ip.extend_from_slice(&[0xa; 16]);
        ip.extend_from_slice(&[0xb; 16]);
        ip.extend_from_slice(&[0xc3, 0x50, 0x27, 0x0f, 0, 10, 0x12, 0x34, b'h', b'i']);
        f.extend_from_slice(&ip);
        let r = reflect(&f, REFLECT_PORT).unwrap();
        let i = 32;
        assert_eq!(&r[4..10], &[2; 6]);
        assert_eq!(&r[10..16], &[1; 6]);
        assert_eq!(&r[i + 8..i + 24], &[0xb; 16]);
        assert_eq!(&r[i + 24..i + 40], &[0xa; 16]);
        assert_eq!(&r[i + 40..i + 44], &[0x27, 0x0f, 0xc3, 0x50]);
        assert_eq!(&r[i + 44..], &f[i + 44..]);
        let mut other = f.clone();
        other[i + 43] = 0x10;
        assert!(reflect(&other, REFLECT_PORT).is_none());
        assert!(reflect(&awdl_frame(false), REFLECT_PORT).is_none());
    }
}
