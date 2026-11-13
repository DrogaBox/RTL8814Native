//! --airdrop: the node announces an AirDrop receiver the way an iPhone does
//! (measured 2026-11-08, iOS 27): not by multicast mDNS but in its MIFs, as
//! three Service Response TLVs (PTR, SRV, TXT of `<id>._airdrop._tcp.local`)
//! plus the bits of those names in the Service Parameters bitmap. The record
//! layout is name length (name and type), compressed name, type, data
//! length, two zero bytes, data. Compression codes: `c0 07` is
//! `_airdrop._tcp.local`, `c0 0c` is `local`, `c0 00` ends a name.
//!
//! The bitmap is a 256-bit filter of name hashes, sent as a 32-bit mask of
//! its non-zero bytes and then those bytes. The hash is not known here, so
//! the instance id is one an iPhone used, whose bit was read off the air:
//! byte 19 bit 6 for `_airdrop._tcp.local`, byte 25 bit 6 for the instance.

pub const INSTANCE: &str = "311dbc80cdc3";
pub const PORT: u16 = 8770;
/// The iPhone's TXT flags (iOS 27).
pub const FLAGS_IPHONE: u32 = 111611;
/// The same without bit 0x10000, "supports PIN pairing": with it macOS 26's
/// sharingd drops the `_airdrop._tcp` endpoint ("should be discovered over
/// Rapport", `sub_1003F0B00` in 25G241) and never contacts the receiver.
pub const FLAGS_LEGACY: u32 = FLAGS_IPHONE & !0x10000;
const TLV_SERVICE_RESPONSE: u8 = 2;
const PTR: u8 = 12;
const TXT: u8 = 16;
const SRV: u8 = 33;

fn record(name: &[u8], kind: u8, data: &[u8]) -> Vec<u8> {
    let mut v = Vec::new();
    v.extend_from_slice(&(name.len() as u16 + 1).to_le_bytes());
    v.extend_from_slice(name);
    v.push(kind);
    v.extend_from_slice(&(data.len() as u16).to_le_bytes());
    v.extend_from_slice(&[0, 0]);
    v.extend_from_slice(data);
    let mut t = vec![TLV_SERVICE_RESPONSE];
    t.extend_from_slice(&(v.len() as u16).to_le_bytes());
    t.extend_from_slice(&v);
    t
}

fn label(s: &str) -> Vec<u8> {
    let mut v = vec![s.len() as u8];
    v.extend_from_slice(s.as_bytes());
    v
}

/// The three Service Response TLVs for the receiver on `host`.local.
pub fn responses(host: &str, port: u16, flags: u32) -> Vec<u8> {
    let mut instance = label(INSTANCE);
    instance.extend_from_slice(&[0xc0, 0x07]);
    let mut ptr = label(INSTANCE);
    ptr.extend_from_slice(&[0xc0, 0x00]);
    let mut srv = vec![0, 0, 0, 0];
    srv.extend_from_slice(&port.to_be_bytes());
    srv.extend_from_slice(&label(host));
    srv.extend_from_slice(&[0xc0, 0x0c]);
    let mut out = record(&[0xc0, 0x07], PTR, &ptr);
    out.extend_from_slice(&record(&instance, SRV, &srv));
    out.extend_from_slice(&record(&instance, TXT, &label(&format!("flags={flags}"))));
    out
}

/// --pairing: the Rapport application service records an iPhone (iOS 27)
/// sends next to the AirDrop ones, for `_applicationservicepairing._tcp`
/// and `_appsvcprepair._tcp` (`c0 0a` is `_tcp.local`). macOS 26's rapportd
/// browses both on awdl0 while the AirDrop sheet is open (RPPairing). The
/// PTR carries the instance as given, the SRV and TXT names in lower case,
/// as the iPhone sent them. TXT: `sn=` the service, `at=` 12 hex digits,
/// `sid=` a UUID, and on the pre-pairing one `dnm=` the display name.
pub const PAIRING_INSTANCE: &str = "iPhone";
pub const PAIRING_PORT: u16 = 56962;
pub const PREPAIR_PORT: u16 = 56963;
const PAIRING: &str = "_applicationservicepairing";
const PREPAIR: &str = "_appsvcprepair";
const SERVICE_NAME: &str = "sn=com.apple.sharingd.AirDrop";

pub fn pairing(host: &str, at: &str, sid: &str) -> Vec<u8> {
    let mut out = Vec::new();
    for (svc, port, dnm) in [(PAIRING, PAIRING_PORT, false), (PREPAIR, PREPAIR_PORT, true)] {
        let mut kind = label(svc);
        kind.extend_from_slice(&[0xc0, 0x0a]);
        let mut target = label(PAIRING_INSTANCE);
        target.extend_from_slice(&[0xc0, 0x00]);
        let mut instance = label(&PAIRING_INSTANCE.to_lowercase());
        instance.extend_from_slice(&kind);
        let mut srv = vec![0, 0, 0, 0];
        srv.extend_from_slice(&port.to_be_bytes());
        srv.extend_from_slice(&label(host));
        srv.extend_from_slice(&[0xc0, 0x0c]);
        let mut txt = label(SERVICE_NAME);
        txt.extend_from_slice(&label(&format!("at={at}")));
        txt.extend_from_slice(&label(&format!("sid={sid}")));
        if dnm {
            txt.extend_from_slice(&label(&format!("dnm={PAIRING_INSTANCE}")));
        }
        out.extend_from_slice(&record(&kind, PTR, &target));
        out.extend_from_slice(&record(&instance, SRV, &srv));
        out.extend_from_slice(&record(&instance, TXT, &txt));
    }
    out
}

/// The Service Parameters body after its three zero bytes: update
/// indicator, mask of the non-zero bitmap bytes, and those bytes. AirDrop:
/// bytes 19 and 25, bit 6. Pairing with the instance `iPhone`, read off the
/// iPhone's frames: byte 2 bit 3, byte 4 bit 0, byte 5 bit 5, byte 10 bit 5
/// (bytes 4 and 5 are also set by the Mac's own records: the two service
/// types; 2 and 10 are the instance).
pub fn params(update: u16, airdrop: bool, pairing: bool) -> Vec<u8> {
    let mut map = [0u8; 32];
    if airdrop {
        map[19] |= 0x40;
        map[25] |= 0x40;
    }
    if pairing {
        map[2] |= 0x08;
        map[4] |= 0x01;
        map[5] |= 0x20;
        map[10] |= 0x20;
    }
    let mask = map.iter().enumerate().filter(|(_, b)| **b != 0).fold(0u32, |m, (i, _)| m | 1 << i);
    let mut v = update.to_le_bytes().to_vec();
    v.extend_from_slice(&mask.to_le_bytes());
    v.extend(map.iter().filter(|b| **b != 0));
    v
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hex(s: &str) -> Vec<u8> {
        (0..s.len()).step_by(2).map(|i| u8::from_str_radix(&s[i..i + 2], 16).unwrap()).collect()
    }

    /// The records an iPhone sent on 2026-11-08, byte for byte.
    #[test]
    fn matches_the_iphones_records() {
        let host = "11282ef0-0598-45ee-8ba7-aec1278df147";
        let mut want = vec![2, 24, 0];
        want.extend(hex("0300c0070c0f0000000c333131646263383063646333c000"));
        want.extend([2, 67, 0]);
        want.extend(hex(
            "10000c333131646263383063646333c007212d0000000000000022422431313238326566302d303539382d\
             343565652d386261372d616563313237386466313437c00c",
        ));
        want.extend([2, 35, 0]);
        want.extend(hex("10000c333131646263383063646333c007100d0000000c666c6167733d313131363131"));
        assert_eq!(responses(host, 8770, FLAGS_IPHONE), want);
        assert_eq!(FLAGS_LEGACY, 46075);
    }

    /// The iPhone's bitmap with only the two AirDrop bits kept.
    #[test]
    fn params_carry_the_two_airdrop_bits() {
        assert_eq!(params(0x35, true, false), hex("3500000008024040"));
    }

    /// The iPhone's bitmap when it announced only the pairing records.
    #[test]
    fn params_carry_the_pairing_bits() {
        assert_eq!(params(0x13f, false, true), hex("3f013404000008012020"));
    }

    /// The six pairing records an iPhone sent on 2026-11-08, byte for byte.
    #[test]
    fn matches_the_iphones_pairing_records() {
        let host = "acdb5de3-d83d-49b8-b920-637825febdb7";
        let tlv = |h: &str| {
            let b = hex(h);
            let mut t = vec![2];
            t.extend_from_slice(&(b.len() as u16).to_le_bytes());
            t.extend(b);
            t
        };
        let sn = "1d736e3d636f6d2e6170706c652e73686172696e67642e41697244726f70";
        let at = "0f61743d333138366633366135376133";
        let sid = "287369643d33344637424137412d303446312d343345452d383136322d453032333335434335414242";
        let srv_host = "2461636462356465332d643833642d343962382d623932302d363337383235666562646237c00c";
        let pair = "066970686f6e651a5f6170706c69636174696f6e7365727669636570616972696e67c00a";
        let pre = "066970686f6e650e5f61707073766370726570616972c00a";
        let mut want = tlv("1e001a5f6170706c69636174696f6e7365727669636570616972696e67c00a0c09000000066950686f6e65c000");
        want.extend(tlv(&format!("2500{pair}212d00000000000000de82{srv_host}")));
        want.extend(tlv(&format!("2500{pair}1057000000{sn}{at}{sid}")));
        want.extend(tlv("12000e5f61707073766370726570616972c00a0c09000000066950686f6e65c000"));
        want.extend(tlv(&format!("1900{pre}212d00000000000000de83{srv_host}")));
        want.extend(tlv(&format!("1900{pre}1062000000{sn}{at}{sid}0a646e6d3d6950686f6e65")));
        assert_eq!(pairing(host, "3186f36a57a3", "34F7BA7A-04F1-43EE-8162-E02335CC5ABB"), want);
    }
}
