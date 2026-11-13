//! The node's IPv6 end without a host stack: Neighbor Advertisements for its
//! link-local address and Echo Replies, enough for `ping6` from the Mac.

use core::net::Ipv6Addr;

use crosswind_awdl::{Ipv6Packet, MacAddr};

const ICMPV6: u8 = 58;
const ECHO_REQUEST: u8 = 128;
const ECHO_REPLY: u8 = 129;
const NEIGHBOR_SOLICIT: u8 = 135;
const NEIGHBOR_ADVERT: u8 = 136;

#[derive(Debug, Default, Clone)]
pub struct Counters {
    pub ns: u64,
    pub na: u64,
    pub echo: u64,
    pub other: u64,
}

pub struct Responder {
    mac: MacAddr,
    pub counters: Counters,
}

impl Responder {
    pub fn new(mac: MacAddr) -> Responder {
        Responder { mac, counters: Counters::default() }
    }

    /// The reply to a packet the node delivered, if it is one to answer.
    pub fn handle(&mut self, packet: &Ipv6Packet) -> Option<Ipv6Packet> {
        let own = self.mac.link_local();
        let reply = answer(own, self.mac, &packet.data);
        match reply {
            Some((kind, _)) if kind == NEIGHBOR_ADVERT => {
                self.counters.ns += 1;
                self.counters.na += 1;
            }
            Some(_) => self.counters.echo += 1,
            None => self.counters.other += 1,
        }
        reply.and_then(|(_, data)| Ipv6Packet::from_ip(data))
    }
}

/// An ICMPv6 Neighbor Solicitation, the one message a utun bridge still has
/// to answer itself.
pub fn is_neighbor_solicitation(data: &[u8]) -> bool {
    data.len() > 40 && data[0] >> 4 == 6 && data[6] == ICMPV6 && data[40] == NEIGHBOR_SOLICIT
}

/// The ICMPv6 message type and the packet answering `data`, from the IPv6
/// header on. Only packets with ICMPv6 as the first next header.
fn answer(own: Ipv6Addr, mac: MacAddr, data: &[u8]) -> Option<(u8, Vec<u8>)> {
    if data.len() < 48 || data[0] >> 4 != 6 || data[6] != ICMPV6 {
        return None;
    }
    let plen = u16::from_be_bytes([data[4], data[5]]) as usize;
    if data.len() < 40 + plen || plen < 8 {
        return None;
    }
    let src = Ipv6Addr::from(<[u8; 16]>::try_from(&data[8..24]).ok()?);
    let dst = Ipv6Addr::from(<[u8; 16]>::try_from(&data[24..40]).ok()?);
    let icmp = &data[40..40 + plen];
    match icmp[0] {
        NEIGHBOR_SOLICIT if plen >= 24 && data[7] == 255 => {
            let target = Ipv6Addr::from(<[u8; 16]>::try_from(&icmp[8..24]).ok()?);
            // DAD probes come from :: and are not answered for a free address.
            if target != own || src.is_unspecified() {
                return None;
            }
            let mut msg = Vec::with_capacity(32);
            msg.extend_from_slice(&[NEIGHBOR_ADVERT, 0, 0, 0]);
            msg.extend_from_slice(&[0x60, 0, 0, 0]); // Solicited, Override
            msg.extend_from_slice(&own.octets());
            msg.extend_from_slice(&[2, 1]); // Target Link-Layer Address, 8 bytes
            msg.extend_from_slice(&mac.0);
            Some((NEIGHBOR_ADVERT, packet(own, src, 255, msg)))
        }
        ECHO_REQUEST if dst == own => {
            let mut msg = icmp.to_vec();
            msg[0] = ECHO_REPLY;
            msg[2] = 0;
            msg[3] = 0;
            Some((ECHO_REPLY, packet(own, src, 64, msg)))
        }
        _ => None,
    }
}

fn packet(src: Ipv6Addr, dst: Ipv6Addr, hop_limit: u8, mut msg: Vec<u8>) -> Vec<u8> {
    let sum = checksum(src, dst, &msg);
    msg[2..4].copy_from_slice(&sum.to_be_bytes());
    let mut out = Vec::with_capacity(40 + msg.len());
    out.extend_from_slice(&[0x60, 0, 0, 0]);
    out.extend_from_slice(&(msg.len() as u16).to_be_bytes());
    out.push(ICMPV6);
    out.push(hop_limit);
    out.extend_from_slice(&src.octets());
    out.extend_from_slice(&dst.octets());
    out.extend_from_slice(&msg);
    out
}

/// RFC 4443 §2.3: over the pseudo-header and the message with its checksum
/// field zero.
pub fn checksum(src: Ipv6Addr, dst: Ipv6Addr, msg: &[u8]) -> u16 {
    let mut sum: u32 = 0;
    let mut add = |b: &[u8]| {
        for c in b.chunks(2) {
            sum += u32::from(u16::from_be_bytes([c[0], *c.get(1).unwrap_or(&0)]));
        }
    };
    add(&src.octets());
    add(&dst.octets());
    add(&(msg.len() as u32).to_be_bytes());
    add(&[0, 0, 0, ICMPV6]);
    add(msg);
    while sum >> 16 != 0 {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    !(sum as u16)
}

#[cfg(test)]
mod tests {
    use super::*;

    const NODE: MacAddr = MacAddr([0x02, 0x00, 0x00, 0xaa, 0xbb, 0x01]);
    const MAC: MacAddr = MacAddr([0x5e, 0x11, 0x22, 0x33, 0x44, 0x55]);

    /// A packet's checksum verifies when summing it with the field in place
    /// gives zero.
    fn verifies(p: &[u8]) -> bool {
        let src = Ipv6Addr::from(<[u8; 16]>::try_from(&p[8..24]).unwrap());
        let dst = Ipv6Addr::from(<[u8; 16]>::try_from(&p[24..40]).unwrap());
        let mut msg = p[40..].to_vec();
        let given = u16::from_be_bytes([msg[2], msg[3]]);
        msg[2] = 0;
        msg[3] = 0;
        checksum(src, dst, &msg) == given
    }

    fn ns(src: Ipv6Addr, target: Ipv6Addr) -> Vec<u8> {
        let mut msg = vec![NEIGHBOR_SOLICIT, 0, 0, 0, 0, 0, 0, 0];
        msg.extend_from_slice(&target.octets());
        msg.extend_from_slice(&[1, 1]);
        msg.extend_from_slice(&MAC.0);
        let o = target.octets();
        let group = Ipv6Addr::new(0xff02, 0, 0, 0, 0, 1, 0xff00 | o[13] as u16,
                                  u16::from_be_bytes([o[14], o[15]]));
        packet(src, group, 255, msg)
    }

    #[test]
    fn known_checksum() {
        // Echo request fe80::1 -> fe80::2, id 1 seq 1, no data, by hand:
        // fe80 + 1 + fe80 + 2 + 8 (length) + 3a + 8000 + 1 + 1 = 0x27d47,
        // folded 0x7d49, complemented 0x82b6.
        let a = "fe80::1".parse().unwrap();
        let b = "fe80::2".parse().unwrap();
        let msg = [128, 0, 0, 0, 0, 1, 0, 1];
        assert_eq!(checksum(a, b, &msg), 0x82b6);
    }

    #[test]
    fn neighbor_solicitation_gets_an_advertisement() {
        let mut r = Responder::new(NODE);
        let p = Ipv6Packet::from_ip(ns(MAC.link_local(), NODE.link_local())).unwrap();
        let na = r.handle(&p).expect("an NA");
        let d = &na.data;
        assert_eq!(d[6], ICMPV6);
        assert_eq!(d[7], 255);
        assert_eq!(&d[8..24], &NODE.link_local().octets());
        assert_eq!(&d[24..40], &MAC.link_local().octets());
        assert_eq!(d[40], NEIGHBOR_ADVERT);
        assert_eq!(d[44], 0x60);
        assert_eq!(&d[48..64], &NODE.link_local().octets());
        assert_eq!(&d[64..66], &[2, 1]);
        assert_eq!(&d[66..72], &NODE.0);
        assert!(verifies(d));
        assert_eq!(na.dst, MAC);
        assert_eq!(na.src, NODE);
        assert_eq!(r.counters.na, 1);
    }

    #[test]
    fn other_targets_and_dad_are_ignored() {
        let mut r = Responder::new(NODE);
        let other = MacAddr([0x02, 0, 0, 0, 0, 9]).link_local();
        assert!(r.handle(&Ipv6Packet::from_ip(ns(MAC.link_local(), other)).unwrap()).is_none());
        let dad = ns(Ipv6Addr::UNSPECIFIED, NODE.link_local());
        assert!(answer(NODE.link_local(), NODE, &dad).is_none());
    }

    #[test]
    fn echo_request_gets_a_reply() {
        let mut r = Responder::new(NODE);
        let req = Ipv6Packet::echo_request(MAC, NODE, 0x1234, 7, b"abc");
        let rep = r.handle(&req).expect("a reply");
        let d = &rep.data;
        assert_eq!(d[40], ECHO_REPLY);
        assert_eq!(&d[44..48], &[0x12, 0x34, 0, 7]);
        assert_eq!(&d[48..], b"abc");
        assert_eq!(&d[8..24], &NODE.link_local().octets());
        assert_eq!(&d[24..40], &MAC.link_local().octets());
        assert!(verifies(d));
        assert!(verifies(&req.data));
        assert_eq!(r.counters.echo, 1);
    }
}
