//! A utun interface for the node, so a host TCP/IP stack can talk to awdl0
//! through it: packets the stack sends on utunN go to the node as outbound
//! IPv6, and IPv6 the node delivers comes back up utunN. The node's
//! link-local address is assigned to utunN, so a socket bound to it with
//! scope utunN reaches the Mac's awdl0 address as a peer would.

use std::ffi::{CStr, c_char, c_int, c_void};
use std::io;
use std::net::Ipv6Addr;
use std::process::Command;

const PF_SYSTEM: c_int = 32;
const AF_SYSTEM: u8 = 32;
const AF_SYS_CONTROL: u16 = 2;
const SYSPROTO_CONTROL: c_int = 2;
const SOCK_DGRAM: c_int = 2;
const UTUN_OPT_IFNAME: c_int = 2;
/// `_IOWR('N', 3, struct ctl_info)`, `struct ctl_info` being 100 bytes.
const CTLIOCGINFO: u64 = 0xc064_4e03;
const F_SETFL: c_int = 4;
const O_NONBLOCK: c_int = 4;
const EAGAIN: i32 = 35;
const AF_INET6: u32 = 30;
/// awdl0's MTU (the AirLayer's RegistrationInfo +0x48).
pub const MTU: usize = 1428;

#[repr(C)]
struct CtlInfo {
    ctl_id: u32,
    ctl_name: [c_char; 96],
}

#[repr(C)]
struct SockaddrCtl {
    sc_len: u8,
    sc_family: u8,
    ss_sysaddr: u16,
    sc_id: u32,
    sc_unit: u32,
    sc_reserved: [u32; 5],
}

unsafe extern "C" {
    fn socket(domain: c_int, ty: c_int, protocol: c_int) -> c_int;
    fn ioctl(fd: c_int, request: u64, ...) -> c_int;
    fn connect(fd: c_int, addr: *const c_void, len: u32) -> c_int;
    fn getsockopt(fd: c_int, level: c_int, name: c_int, val: *mut c_void, len: *mut u32) -> c_int;
    fn fcntl(fd: c_int, cmd: c_int, ...) -> c_int;
    fn read(fd: c_int, buf: *mut c_void, n: usize) -> isize;
    fn write(fd: c_int, buf: *const c_void, n: usize) -> isize;
    fn close(fd: c_int) -> c_int;
}

pub struct Tun {
    fd: c_int,
    pub name: String,
}

impl Tun {
    /// A new utunN (the next free unit), non-blocking, with `addr` as its
    /// link-local address. Needs root.
    pub fn open(addr: Ipv6Addr) -> io::Result<Tun> {
        // SAFETY: plain socket calls on structures laid out as in
        // <sys/kern_control.h>; every buffer outlives its call.
        let fd = unsafe { socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL) };
        if fd < 0 {
            return Err(io::Error::last_os_error());
        }
        // A single Tun owns fd from here on: its Drop closes it on every early
        // return, so no second Tun may be built from the same fd.
        let mut tun = Tun { fd, name: String::new() };
        let mut info = CtlInfo { ctl_id: 0, ctl_name: [0; 96] };
        for (d, s) in info.ctl_name.iter_mut().zip(b"com.apple.net.utun_control") {
            *d = *s as c_char;
        }
        if unsafe { ioctl(fd, CTLIOCGINFO, &mut info as *mut CtlInfo) } < 0 {
            return Err(io::Error::last_os_error());
        }
        let sa = SockaddrCtl {
            sc_len: size_of::<SockaddrCtl>() as u8,
            sc_family: AF_SYSTEM,
            ss_sysaddr: AF_SYS_CONTROL,
            sc_id: info.ctl_id,
            sc_unit: 0, // the kernel picks the next free utun
            sc_reserved: [0; 5],
        };
        let sa_len = size_of::<SockaddrCtl>() as u32;
        if unsafe { connect(fd, (&sa as *const SockaddrCtl).cast(), sa_len) } < 0 {
            return Err(io::Error::last_os_error());
        }
        let mut name = [0 as c_char; 32];
        let mut len = name.len() as u32;
        let rc = unsafe {
            getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, name.as_mut_ptr().cast(), &mut len)
        };
        if rc < 0 {
            return Err(io::Error::last_os_error());
        }
        if unsafe { fcntl(fd, F_SETFL, O_NONBLOCK) } < 0 {
            return Err(io::Error::last_os_error());
        }
        // SAFETY: the kernel wrote a NUL-terminated name.
        tun.name = unsafe { CStr::from_ptr(name.as_ptr()) }.to_string_lossy().into_owned();
        tun.configure(addr)?;
        Ok(tun)
    }

    fn configure(&self, addr: Ipv6Addr) -> io::Result<()> {
        let scoped = format!("{addr}%{}", self.name);
        let mtu = MTU.to_string();
        for args in [
            vec![self.name.as_str(), "mtu", mtu.as_str()],
            vec![self.name.as_str(), "inet6", scoped.as_str(), "prefixlen", "64"],
            vec![self.name.as_str(), "up"],
        ] {
            let st = Command::new("/sbin/ifconfig").args(&args).status()?;
            if !st.success() {
                return Err(io::Error::other(format!("ifconfig {} failed", args.join(" "))));
            }
        }
        Ok(())
    }

    /// The next IPv6 packet the host sent, without the utun family header;
    /// `None` when nothing is waiting.
    pub fn recv(&self) -> io::Result<Option<Vec<u8>>> {
        let mut buf = [0u8; 4 + 65535];
        // SAFETY: buf is valid for its length.
        let n = unsafe { read(self.fd, buf.as_mut_ptr().cast(), buf.len()) };
        if n < 0 {
            let e = io::Error::last_os_error();
            return if e.raw_os_error() == Some(EAGAIN) { Ok(None) } else { Err(e) };
        }
        let n = n as usize;
        if n < 4 + 40 || u32::from_be_bytes(buf[0..4].try_into().unwrap()) != AF_INET6 {
            return Ok(Some(Vec::new())); // not IPv6: the caller drops it
        }
        Ok(Some(buf[4..n].to_vec()))
    }

    /// Hands the host one IPv6 packet (from the IPv6 header on).
    pub fn send(&self, packet: &[u8]) -> io::Result<()> {
        let mut buf = Vec::with_capacity(4 + packet.len());
        buf.extend_from_slice(&AF_INET6.to_be_bytes());
        buf.extend_from_slice(packet);
        // SAFETY: buf is valid for its length.
        let n = unsafe { write(self.fd, buf.as_ptr().cast(), buf.len()) };
        if n < 0 { Err(io::Error::last_os_error()) } else { Ok(()) }
    }
}

impl Drop for Tun {
    fn drop(&mut self) {
        // SAFETY: fd came from socket(); closing it removes the interface.
        unsafe {
            close(self.fd);
        }
    }
}

/// Which packets from the host go to the Mac: only those from the node's
/// address, and of ICMPv6 only echo and neighbour discovery messages, so the
/// stack's router solicitations and MLD reports stay off the link.
pub fn forwardable(packet: &[u8], node: Ipv6Addr) -> bool {
    if packet.len() < 40 || packet[0] >> 4 != 6 || packet[8..24] != node.octets() {
        return false;
    }
    if packet[6] != 58 {
        return true;
    }
    packet.len() > 40 && matches!(packet[40], 128 | 129 | 135 | 136)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn pkt(src: Ipv6Addr, next: u8, icmp_type: u8) -> Vec<u8> {
        let mut p = vec![0x60, 0, 0, 0, 0, 8, next, 64];
        p.extend_from_slice(&src.octets());
        p.extend_from_slice(&"fe80::1".parse::<Ipv6Addr>().unwrap().octets());
        p.extend_from_slice(&[icmp_type, 0, 0, 0, 0, 0, 0, 0]);
        p
    }

    #[test]
    fn only_the_nodes_tcp_echo_and_nd_are_forwarded() {
        let node: Ipv6Addr = "fe80::ff:fe00:1".parse().unwrap();
        let other: Ipv6Addr = "fe80::2".parse().unwrap();
        assert!(forwardable(&pkt(node, 6, 0), node));
        assert!(forwardable(&pkt(node, 58, 128), node));
        assert!(forwardable(&pkt(node, 58, 136), node));
        assert!(!forwardable(&pkt(node, 58, 133), node)); // router solicitation
        assert!(!forwardable(&pkt(node, 58, 143), node)); // MLDv2 report
        assert!(!forwardable(&pkt(other, 6, 0), node));
        assert_eq!(size_of::<SockaddrCtl>(), 32);
        assert_eq!(size_of::<CtlInfo>(), 100);
    }
}
