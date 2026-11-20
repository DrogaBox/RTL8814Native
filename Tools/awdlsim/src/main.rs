//! awdlsim: a virtual AWDL peer for RTL8814Native.
//!
//! With the boot-arg rtlawdl_sim=1 the driver copies every AWDL frame awdl0
//! hands to the radio into a ring, and takes frames back into its AWDL RX hook
//! as if the radio had heard them (Source/Driver/RTLAwdlSim.h). This tool runs
//! a crosswind-awdl node between the two, so the Mac's AWDL stack meets a peer
//! that syncs, elects and carries data, with no second radio. Runs as root.

mod ba;
mod iokit;
mod pcapng;
mod plain;
mod responder;
mod svc;
mod tun;

use std::process::ExitCode;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

use crosswind_awdl::{
    Action, Channel, Config, Input, Ipv6Packet, MacAddr, Node, Presence, RadioConstraints, TU,
    Time,
};

use iokit::{Conn, IoError, Record};

static STOP: AtomicBool = AtomicBool::new(false);

const SIGINT: i32 = 2;
const SIGTERM: i32 = 15;
const QOS_CLASS_USER_INTERACTIVE: u32 = 0x21;
const DEFAULT_CHANNEL: u8 = 149;
const READ_WAIT_MAX_US: u64 = 20_000;
/// With a utun bridge the host's packets are polled this often.
const READ_WAIT_TUN_US: u64 = 2_000;
/// The Mac's PSFs on another channel for this long move the node there.
const FOLLOW_AFTER_US: u64 = 1_000_000;
const REATTACH_TRIES: u32 = 5;
const AWDL_HDR: [u8; 5] = [0x7f, 0x00, 0x17, 0xf2, 0x08];

unsafe extern "C" {
    fn signal(sig: i32, handler: extern "C" fn(i32)) -> usize;
    fn mach_absolute_time() -> u64;
    fn mach_timebase_info(info: *mut [u32; 2]) -> i32;
    fn pthread_set_qos_class_self_np(qos: u32, relative: i32) -> i32;
    fn arc4random_buf(buf: *mut u8, n: usize);
}

extern "C" fn on_signal(_: i32) {
    STOP.store(true, Ordering::Relaxed);
}

fn now_us() -> u64 {
    static TB: std::sync::OnceLock<(u64, u64)> = std::sync::OnceLock::new();
    let (numer, denom) = *TB.get_or_init(|| {
        let mut tb = [0u32; 2];
        // SAFETY: writes the two fields of mach_timebase_info_data_t.
        unsafe { mach_timebase_info(&mut tb) };
        (u64::from(tb[0]), u64::from(tb[1]))
    });
    // SAFETY: no arguments.
    let t = unsafe { mach_absolute_time() };
    (u128::from(t) * u128::from(numer) / u128::from(denom) / 1000) as u64
}

struct Opts {
    channel: Option<u8>,
    mac: MacAddr,
    metric: Option<u32>,
    counter: Option<u32>,
    seconds: Option<u64>,
    rssi: i8,
    capture: Option<String>,
    echo: bool,
    tun: bool,
    umi_secs: Option<u64>,
    six_e: bool,
    rest: bool,
    addba: bool,
    ba_silent: u32,
    airdrop: bool,
    airdrop_flags: u32,
    pairing: bool,
    plain: bool,
}

const USAGE: &str = "usage: sudo awdlsim [--channel 6|44|149] [--mac xx:xx:xx:xx:xx:xx] [--metric N]
                    [--counter N] [--seconds N] [--rssi dBm] [--capture file.pcapng] [--echo] [--tun]
                    [--umi SECONDS] [--6e] [--rest] [--addba] [--ba-silent N] [--airdrop] [--airdrop-flags N] [--pairing]
                    [--plain]

  --channel  fix the node on this social channel (default: follow the channel of awdl0's PSFs)
  --mac      the node's AWDL address (default: random, locally administered)
  --metric   the node's election metric (default 60): above the Mac's to make the node master,
             1 to leave the Mac master (25G241 ranks roots by metric, then MAC)
  --counter  the node's election counter (default: random, never 0: 25G241 drops a peer whose
             root counter is 0 as legacy, IO80211AWDLPeerManager::isPeerSafeForElection)
  --rssi     the signal strength the Mac sees on the node's frames (default -40)
  --echo     the node pings the Mac every 3 s (crosswind's neighbour echo)
  --tun      a utun interface carrying the node's IPv6, so host tools (airdrop_send.py, curl,
             ping6) talk to awdl0 as the node; prints the addresses to use
  --umi      send the Mac a unicast Master Indication with option 1 every so many seconds
  --rest     announce only slots 0, 2 and 10, the shared slots of an iPhone at rest
  --airdrop  announce an AirDrop receiver in the node's MIFs as an iPhone does (service TLVs,
             port 8770 on awdlsim.local); serve it with airdrop_recv.py on the utun (--tun)
  --airdrop-flags  the same with these TXT flags (default 46075: an iPhone's 111611 without bit
             0x10000, with which macOS 26 leaves the legacy receiver to Rapport)
  --pairing  also announce the Rapport application service records an iPhone sends for AirDrop
             (_applicationservicepairing and _appsvcprepair, instance iPhone, ports 56962 and
             56963), which rapportd browses on awdl0 while the AirDrop sheet is open
  --addba    ask awdl0 for a Block Ack session (TID 0, window 64) like an iPhone, then send
             the node's data as QoS data, every eighth frame swapped with the next
  --ba-silent N  leave awdl0's first N ADDBA requests unanswered, then accept as usual
  --plain    send the node's data as plain RFC 1042 (no AWDL head), as an iPhone sends
             Continuity Camera's stream: the layer hands it to llw0 (rtlawdl_llw=1), and llw0's
             plain frames reach the node; with --echo the node's pings test both ways;
             UDP to port 9999 (Tools/llwchan) goes straight back to the Mac
  --6e       declare 6E in the Data Path State: without it IO80211 takes the node for a legacy peer
             and handleAirDropTrafficReg sends it no UMI
Needs the boot-arg rtlawdl_sim=1 and awdl0 up (an AirDrop window open).";

fn parse_args() -> Result<Opts, String> {
    let mut mac = [0u8; 6];
    // SAFETY: fills the 6-byte buffer.
    unsafe { arc4random_buf(mac.as_mut_ptr(), 6) };
    mac[0] = (mac[0] & 0xfc) | 0x02;
    let mut counter = [0u8; 2];
    // SAFETY: fills the 2-byte buffer.
    unsafe { arc4random_buf(counter.as_mut_ptr(), 2) };
    let mut o = Opts {
        channel: None,
        mac: MacAddr(mac),
        metric: None,
        counter: Some(u32::from(u16::from_le_bytes(counter)) + 1),
        seconds: None,
        rssi: -40,
        capture: None,
        echo: false,
        tun: false,
        umi_secs: None,
        six_e: false,
        rest: false,
        addba: false,
        ba_silent: 0,
        airdrop: false,
        airdrop_flags: svc::FLAGS_LEGACY,
        pairing: false,
        plain: false,
    };
    let mut args = std::env::args().skip(1);
    while let Some(a) = args.next() {
        let mut val = || args.next().ok_or_else(|| format!("{a} needs a value"));
        match a.as_str() {
            "--channel" => {
                let c: u8 = val()?.parse().map_err(|_| "bad channel")?;
                if !matches!(c, 6 | 44 | 149) {
                    return Err("the channel must be 6, 44 or 149".into());
                }
                o.channel = Some(c);
            }
            "--mac" => {
                let m: MacAddr = val()?.parse().map_err(|_| "bad MAC")?;
                if m.is_multicast() {
                    return Err("the MAC must be unicast".into());
                }
                o.mac = m;
            }
            "--metric" => o.metric = Some(val()?.parse().map_err(|_| "bad metric")?),
            "--counter" => {
                let c: u32 = val()?.parse().map_err(|_| "bad counter")?;
                if c == 0 {
                    return Err("the counter must not be 0: the Mac drops such a peer".into());
                }
                o.counter = Some(c);
            }
            "--seconds" => o.seconds = Some(val()?.parse().map_err(|_| "bad seconds")?),
            "--rssi" => o.rssi = val()?.parse().map_err(|_| "bad rssi")?,
            "--capture" => o.capture = Some(val()?),
            "--echo" => o.echo = true,
            "--tun" => o.tun = true,
            "--umi" => o.umi_secs = Some(val()?.parse().map_err(|_| "bad umi")?),
            "--6e" => o.six_e = true,
            "--rest" => o.rest = true,
            "--addba" => o.addba = true,
            "--ba-silent" => o.ba_silent = val()?.parse().map_err(|_| "bad count")?,
            "--airdrop" => o.airdrop = true,
            "--airdrop-flags" => {
                o.airdrop = true;
                o.airdrop_flags = val()?.parse().map_err(|_| "bad flags")?;
            }
            "--pairing" => o.pairing = true,
            "--plain" => o.plain = true,
            "-h" | "--help" => return Err(String::new()),
            _ => return Err(format!("unknown option {a}")),
        }
    }
    Ok(o)
}

/// What is printed once a second, then reset.
#[derive(Default)]
struct Window {
    lag_max: u64,
    late_max: u64,
    offset_max: i64,
    offset_samples: u64,
    from_mac: u64,
    to_mac: u64,
}

struct Sim {
    opts: Opts,
    conn: Conn,
    node: Option<Node>,
    chan: u8,
    follow: bool,
    awdl0: Option<MacAddr>,
    next_timer: u64,
    next_umi: u64,
    responder: responder::Responder,
    capture: Option<pcapng::Pcapng>,
    win: Window,
    other_ch: u8,
    other_since: u64,
    start: u64,
    restarts: u32,
    inj_ok: u64,
    inj_offch: u64,
    inj_err: u64,
    last_inj_err: Option<IoError>,
    overruns: u64,
    mac_valid: bool,
    tun: Option<tun::Tun>,
    tun_in: u64,
    tun_out: u64,
    tun_drop: u64,
    ba: ba::Ba,
    next_rotate: u64,
    update: u16,
    plain_tx: u64,
    plain_rx: u64,
    plain_seq: u16,
    reflected: u64,
}

fn is_awdl_af(f: &[u8]) -> bool {
    f.len() >= 31 && f[0] == 0xd0 && f[24..29] == AWDL_HDR
}

fn secs(sim_start: u64, now: u64) -> f64 {
    now.saturating_sub(sim_start) as f64 / 1e6
}

/// How often --pairing draws a new at= and sid=. rapportd ignores a changed
/// result with the same at= ("Changed endpoint ignored (same authTag)") and
/// tries PairVerify again on a new one; an iPhone changes them too.
const PAIRING_ROTATE_US: u64 = 30_000_000;
impl Sim {
    /// The Service Parameters body and Service Response TLVs for --airdrop
    /// and --pairing, with a new at= and sid= each call.
    fn services(&self) -> (Vec<u8>, Vec<u8>) {
        let mut services = Vec::new();
        if self.opts.airdrop {
            services = svc::responses("awdlsim", svc::PORT, self.opts.airdrop_flags);
        }
        if self.opts.pairing {
            let mut r = [0u8; 22];
            // SAFETY: fills the 22-byte buffer.
            unsafe { arc4random_buf(r.as_mut_ptr(), r.len()) };
            let at: String = r[..6].iter().map(|b| format!("{b:02x}")).collect();
            let u: String = r[6..].iter().map(|b| format!("{b:02X}")).collect();
            let sid = format!("{}-{}-{}-{}-{}", &u[..8], &u[8..12], &u[12..16], &u[16..20], &u[20..]);
            services.extend(svc::pairing("awdlsim", &at, &sid));
            println!("[{:8.3}] pairing records: instance {} ports {} and {}, at={at} sid={sid}",
                secs(self.start, now_us()), svc::PAIRING_INSTANCE, svc::PAIRING_PORT, svc::PREPAIR_PORT);
        }
        let params = if self.opts.airdrop || self.opts.pairing {
            svc::params(1, self.opts.airdrop, self.opts.pairing)
        } else {
            Vec::new()
        };
        (params, services)
    }
    fn rotate_pairing(&mut self, now: u64) {
        if !self.opts.pairing || self.node.is_none() || now < self.next_rotate {
            return;
        }
        self.next_rotate = now + PAIRING_ROTATE_US;
        self.update = self.update.wrapping_add(1);
        let (mut params, services) = self.services();
        params[..2].copy_from_slice(&self.update.to_le_bytes());
        self.node.as_mut().unwrap().set_services(params, services);
    }
    fn new_node(&mut self, now: u64) {
        let mut cfg = Config::new(
            self.opts.mac,
            "awdlsim",
            RadioConstraints { channel: Channel(self.chan), presence: Presence::Fixed },
        );
        cfg.rssi_threshold = None;
        cfg.max_frame = 1536;
        cfg.neighbour_echo = self.opts.echo;
        cfg.metric = self.opts.metric;
        cfg.data_path_ext = if self.opts.six_e { 0x0800 } else { 0 };
        if self.opts.rest {
            cfg.slot_mask = 0x0405;
        }
        cfg.counter = self.opts.counter;
        let (params, services) = self.services();
        cfg.service_params = params;
        cfg.services = services;
        self.next_rotate = now + PAIRING_ROTATE_US;
        self.node = Some(Node::new(cfg, Time(now)));
        self.next_timer = now;
        self.mac_valid = false;
        println!(
            "[{:8.3}] node {} on channel {} (metric {}, counter {}), ping6 {}%awdl0",
            secs(self.start, now),
            self.opts.mac,
            self.chan,
            self.opts.metric.unwrap_or(crosswind_awdl::election::METRIC_INIT),
            self.opts.counter.unwrap_or(crosswind_awdl::election::COUNTER_INIT),
            self.opts.mac.link_local()
        );
    }

    /// One input, then the replies the responder makes, each fed back in.
    fn feed(&mut self, first: Input) {
        let Some(node) = self.node.as_mut() else { return };
        let mut pending: Vec<Ipv6Packet> = Vec::new();
        let mut actions: Vec<Action> = node.handle(first).collect();
        loop {
            for a in actions.drain(..) {
                match a {
                    Action::Transmit { frame, .. } => self.transmit(&frame.bytes),
                    Action::ArmTimer(t) => self.next_timer = t.micros(),
                    Action::PeerUp(info) => {
                        println!(
                            "[{:8.3}] PeerUp {} '{}' devclass {}",
                            secs(self.start, now_us()),
                            info.mac,
                            info.name,
                            info.devclass
                        );
                        if Some(info.mac) == self.awdl0 {
                            self.mac_valid = true;
                        }
                    }
                    Action::PeerDown(m) => {
                        println!("[{:8.3}] PeerDown {}", secs(self.start, now_us()), m);
                        if Some(m) == self.awdl0 {
                            self.mac_valid = false;
                        }
                    }
                    Action::Deliver(p) => {
                        // With a utun the host's stack answers everything but
                        // neighbour solicitations (utun has no NDP).
                        if self.tun.is_some() && !responder::is_neighbor_solicitation(&p.data) {
                            self.to_tun(&p);
                        } else if let Some(r) = self.responder.handle(&p) {
                            pending.push(r);
                        }
                    }
                }
            }
            let Some(p) = pending.pop() else { break };
            let node = self.node.as_mut().unwrap();
            actions = node.handle(Input::Outbound { now: Time(now_us()), packet: p }).collect();
        }
    }

    /// A packet the node delivered, up the utun to the host's stack.
    fn to_tun(&mut self, p: &Ipv6Packet) {
        let Some(t) = self.tun.as_ref() else { return };
        if p.dst != self.opts.mac && !p.dst.is_multicast() {
            self.tun_drop += 1;
            return;
        }
        match t.send(&p.data) {
            Ok(()) => self.tun_in += 1,
            Err(_) => self.tun_drop += 1,
        }
    }

    /// Every packet the host's stack sent on the utun, to the node as
    /// outbound IPv6.
    fn from_tun(&mut self) {
        let node_ll = self.opts.mac.link_local();
        loop {
            let pkt = match self.tun.as_ref().map(|t| t.recv()) {
                Some(Ok(Some(p))) => p,
                Some(Err(e)) => {
                    println!("[-] utun: {e}");
                    self.tun = None;
                    return;
                }
                _ => return,
            };
            if self.node.is_none() || !tun::forwardable(&pkt, node_ll) {
                self.tun_drop += 1;
                continue;
            }
            match Ipv6Packet::from_ip(pkt) {
                Some(p) => {
                    self.tun_out += 1;
                    self.feed(Input::Outbound { now: Time(now_us()), packet: p });
                }
                None => self.tun_drop += 1,
            }
        }
    }

    fn print_tun_hint(&self, m: MacAddr) {
        if let Some(t) = self.tun.as_ref() {
            println!(
                "           utun bridge: {name}, node {node}%{name}, awdl0 {mac}%{name}\n           \
                 python3 -I Tools/awdlsim/airdrop_send.py --iface {name} --src {node} --dst {mac} FILE",
                name = t.name,
                node = self.opts.mac.link_local(),
                mac = m.link_local()
            );
        }
    }

    /// With --umi: a unicast MIF to awdl0 with option bit 0, which takes
    /// IO80211AWDLPeer::processUnicastAWSF to the branch that caches the peer.
    fn send_umi(&mut self, now: u64) {
        let Some(period) = self.opts.umi_secs else { return };
        let (Some(m), true) = (self.awdl0, now >= self.next_umi) else { return };
        let Some(node) = self.node.as_mut() else { return };
        let f = node.unicast_mi(Time(now), m, 1);
        self.next_umi = now + period * 1_000_000;
        self.transmit(&f.bytes);
        println!("[{:8.3}] umi to {m}", secs(self.start, now));
    }

    fn transmit(&mut self, bytes: &[u8]) {
        let rewritten = if self.opts.plain { plain::to_plain(bytes) } else { None };
        if rewritten.is_some() {
            self.plain_tx += 1;
        }
        let bytes = rewritten.as_deref().unwrap_or(bytes);
        if !self.opts.addba {
            self.inject_one(bytes);
            return;
        }
        let awdl0 = self.awdl0.map(|m| m.0);
        for f in self.ba.outgoing(bytes, awdl0) {
            self.inject_one(&f);
        }
    }
    /// With --addba: the ADDBA request once awdl0 is a valid peer, retried
    /// until it answers; a frame held for a swap goes out at the report.
    fn send_addba(&mut self, now: u64) {
        if !self.opts.addba || !self.mac_valid {
            return;
        }
        let Some(m) = self.awdl0 else { return };
        if let Some(f) = self.ba.request(now, self.opts.mac.0, m.0) {
            println!("[{:8.3}] ADDBA request to {m} ({:?})", secs(self.start, now), self.ba.state);
            self.inject_one(&f);
        } else if self.ba.state == ba::State::NoAnswer {
            println!("[{:8.3}] ADDBA: no answer from {m}", secs(self.start, now));
            self.ba.state = ba::State::Declined { status: 0xffff };
        }
    }
    fn inject_one(&mut self, bytes: &[u8]) {
        if let Some(c) = self.capture.as_mut() {
            let _ = c.frame(now_us(), bytes, "node > awdl0");
        }
        match self.conn.inject(bytes, self.opts.rssi) {
            Ok(()) => {
                self.inj_ok += 1;
                self.win.to_mac += 1;
            }
            Err(IoError(iokit::IO_OFFLINE)) => self.inj_offch += 1,
            Err(e) => {
                self.inj_err += 1;
                self.last_inj_err = Some(e);
            }
        }
    }

    fn record(&mut self, rec: Record) {
        let now = now_us();
        if rec.overrun {
            self.overruns += 1;
        }
        let f = &rec.bytes;
        if let Some(c) = self.capture.as_mut() {
            let _ = c.frame(rec.t_us, f, &format!("awdl0 > air ch {}", rec.channel));
        }
        let psf = is_awdl_af(f) && f[30] == 0;
        if is_awdl_af(f) && self.awdl0.is_none() {
            let m = MacAddr(f[10..16].try_into().unwrap());
            println!("[{:8.3}] awdl0 is {} (radio {})", secs(self.start, now), m,
                     MacAddr(self.conn.radio_mac));
            self.awdl0 = Some(m);
            self.print_tun_hint(m);
        }
        if psf && self.follow {
            if rec.channel == self.chan && self.node.is_some() {
                self.other_ch = 0;
            } else if self.node.is_none() {
                self.set_channel(rec.channel, now);
            } else if rec.channel != self.other_ch {
                self.other_ch = rec.channel;
                self.other_since = now;
            } else if now - self.other_since > FOLLOW_AFTER_US {
                println!("[{:8.3}] awdl0 moved to channel {}", secs(self.start, now), rec.channel);
                self.restarts += 1;
                self.set_channel(rec.channel, now);
            }
        }
        if self.opts.addba {
            if let Some((status, win, ours)) = self.ba.response(f, self.opts.mac.0) {
                println!("[{:8.3}] ADDBA response from awdl0: status {status} window {win}{}",
                         secs(self.start, now), if ours { "" } else { " (old token)" });
            }
        }
        if self.node.is_none() || rec.channel != self.chan {
            return; // the node was not listening there
        }
        // awdl0 asks for a Block Ack session of its own: accepted, and its
        // QoS data reaches the node as plain data.
        let asked = f.len() >= 33 && f[0] == 0xd0 && f[4..10] == self.opts.mac.0 && f[24] == 3 && f[25] == 0;
        if asked && self.opts.ba_silent > 0 {
            self.opts.ba_silent -= 1;
            println!("[{:8.3}] ADDBA request from awdl0: token {} -> left unanswered ({} more)",
                     secs(self.start, now), f[26], self.opts.ba_silent);
        } else if let Some(r) = self.ba.answer(f, self.opts.mac.0) {
            if self.ba.rx_sessions <= 8 {
                println!("[{:8.3}] ADDBA request from awdl0: token {} ssn {} -> accepted",
                         secs(self.start, now), f[26], u16::from_le_bytes([f[31], f[32]]) >> 4);
            }
            self.inject_one(&r);
        }
        let plain = self.ba.incoming(f, self.opts.mac.0);
        let f = plain.as_ref().unwrap_or(f);
        // llwchan: UDP to port 9999 over llw0 goes straight back to the Mac.
        if let Some(r) = self.opts.plain.then(|| plain::reflect(f, plain::REFLECT_PORT)).flatten() {
            if self.reflected == 0 {
                println!("[{:8.3}] first llwchan datagram from the Mac (llw0), sent back", secs(self.start, now));
            }
            self.reflected += 1;
            self.inject_one(&r);
            return;
        }
        let from_llw = if self.opts.plain { plain::from_plain(f, self.plain_seq) } else { None };
        if from_llw.is_some() {
            if self.plain_rx == 0 {
                println!("[{:8.3}] first plain frame from the Mac (llw0)", secs(self.start, now));
            }
            self.plain_rx += 1;
            self.plain_seq = self.plain_seq.wrapping_add(1) & 0xfff;
        }
        let f = from_llw.as_ref().unwrap_or(f);
        self.win.lag_max = self.win.lag_max.max(now.saturating_sub(rec.t_us));
        self.win.from_mac += 1;
        let sample = is_awdl_af(f) && (f[30] == 0 || f[30] == 3);
        self.feed(Input::Frame { rx_time: Time(rec.t_us), rssi: Some(-40), bytes: f });
        if sample {
            if let (Some(m), Some(node)) = (self.awdl0, self.node.as_ref()) {
                if let Some(off) = node.peer(m).and_then(|p| p.sync_offset_us) {
                    if off.abs() > self.win.offset_max.abs() {
                        self.win.offset_max = off;
                    }
                    self.win.offset_samples += 1;
                }
            }
        }
    }

    fn set_channel(&mut self, ch: u8, now: u64) {
        if let Err(e) = self.conn.ctl(iokit::OP_CHANNEL, u64::from(ch)) {
            println!("[-] channel {ch}: {e}");
            return;
        }
        self.chan = ch;
        self.other_ch = 0;
        self.new_node(now);
    }

    fn report(&mut self, now: u64) {
        let counters = self.conn.ctl(iokit::OP_QUERY, 0).unwrap_or([0; iokit::COUNTERS]);
        let w = std::mem::take(&mut self.win);
        let mut line = format!("[{:8.3}] ", secs(self.start, now));
        match (self.node.as_ref(), self.awdl0) {
            (Some(node), Some(m)) => {
                let e = node.election();
                let master = if e.is_master() { "node".to_string() } else { e.master.to_string() };
                line += &format!("master {} h{} m{} c{}", master, e.height, e.master_metric,
                                 e.master_counter);
                match node.peer(m) {
                    Some(p) => line += &format!(
                        " | awdl0 {} m{} c{} follows {} h{}",
                        if p.valid { "valid" } else { "heard" },
                        p.election.self_metric,
                        p.election.self_counter,
                        if p.election.master == self.opts.mac { "node".to_string() }
                        else { p.election.master.to_string() },
                        p.election.height
                    ),
                    None => line += " | awdl0 not heard",
                }
            }
            (Some(_), None) => line += "awdl0 not heard",
            (None, _) => line += "waiting for awdl0's PSFs",
        }
        line += &format!(
            " | offset max {} us n{} | lag {} late {} us | rx {} tx {} | inj ok {} offch {} err {} \
             | tap {} ovr {} | ns {} echo {}",
            w.offset_max,
            w.offset_samples,
            w.lag_max,
            w.late_max,
            w.from_mac,
            w.to_mac,
            self.inj_ok,
            self.inj_offch,
            self.inj_err,
            counters[2],
            counters[iokit::C_OVERRUNS],
            self.responder.counters.ns,
            self.responder.counters.echo,
        );
        if self.tun.is_some() {
            line += &format!(" | utun in {} out {} drop {}", self.tun_in, self.tun_out,
                             self.tun_drop);
        }
        if self.opts.plain {
            line += &format!(" | plain to Mac {} from Mac {} | llwchan back {}",
                             self.plain_tx, self.plain_rx, self.reflected);
        }
        if self.ba.rx_sessions > 0 {
            line += &format!(" | from awdl0: sessions {} qos {} gaps {}", self.ba.rx_sessions,
                             self.ba.rx_qos, self.ba.rx_gaps);
        }
        if self.opts.addba {
            line += &format!(" | ba {:?} qos {} swapped {}", self.ba.state, self.ba.qos,
                             self.ba.swapped);
            if let Some(h) = self.ba.flush() {
                self.inject_one(&h);
            }
        }
        if w.lag_max > TU || w.late_max > TU {
            line += " [lag > 1 TU: offsets not valid]";
        }
        println!("{line}");
        if let Some(e) = self.last_inj_err.take() {
            println!("           last inject error: {e}");
        }
    }

    /// A read failed: the sim was disabled under us (USB takeover, sleep) or
    /// the connection died. False when the run should end.
    fn read_failed(&mut self, e: IoError) -> bool {
        if STOP.load(Ordering::Relaxed) {
            return true;
        }
        println!("[-] read: {e}");
        self.reattach()
    }

    fn reattach(&mut self) -> bool {
        for i in 1..=REATTACH_TRIES {
            if STOP.load(Ordering::Relaxed) {
                return false;
            }
            std::thread::sleep(Duration::from_secs(2));
            match Conn::open().and_then(|c| {
                c.ctl(iokit::OP_ENABLE, u64::from(self.chan)).map(|_| c)
            }) {
                Ok(c) => {
                    self.conn = c;
                    self.restarts += 1;
                    self.awdl0 = None;
                    if self.follow {
                        self.node = None;
                    } else {
                        self.new_node(now_us());
                    }
                    println!("[+] reattached (try {i})");
                    return true;
                }
                Err(e) => println!("[-] reattach try {i}: {e}"),
            }
        }
        false
    }

    fn run(&mut self) -> ExitCode {
        let end = self.opts.seconds.map(|s| self.start + s * 1_000_000);
        let mut next_report = self.start + 1_000_000;
        loop {
            if STOP.load(Ordering::Relaxed) {
                break;
            }
            let now = now_us();
            if end.is_some_and(|e| now >= e) {
                break;
            }
            // Everything already tapped first, so a timer never runs ahead of
            // frames that came before it; then, with nothing ready, wait for
            // a frame or the node's timer, whichever comes first.
            let mut drained = 0;
            while drained < 256 {
                match self.conn.read(0) {
                    Ok(Some(rec)) => {
                        self.record(rec);
                        drained += 1;
                    }
                    Ok(None) => break,
                    Err(e) => {
                        if !self.read_failed(e) {
                            return ExitCode::FAILURE;
                        }
                        break;
                    }
                }
            }
            self.from_tun();
            if drained == 0 {
                let cap = if self.tun.is_some() { READ_WAIT_TUN_US } else { READ_WAIT_MAX_US };
                let wait = if self.node.is_some() {
                    self.next_timer.saturating_sub(now_us()).min(cap)
                } else {
                    cap
                };
                if wait > 0 {
                    match self.conn.read(wait) {
                        Ok(Some(rec)) => self.record(rec),
                        Ok(None) => {}
                        Err(e) => {
                            if !self.read_failed(e) {
                                return ExitCode::FAILURE;
                            }
                        }
                    }
                }
            }
            let now = now_us();
            if self.node.is_some() && now >= self.next_timer {
                self.win.late_max = self.win.late_max.max(now - self.next_timer);
                self.feed(Input::Timer(Time(now)));
            }
            self.send_umi(now);
            self.send_addba(now);
            self.rotate_pairing(now);
            if now >= next_report {
                self.report(now);
                next_report += 1_000_000;
                if let Some(c) = self.capture.as_mut() {
                    let _ = c.flush();
                }
            }
        }
        ExitCode::SUCCESS
    }
}

fn main() -> ExitCode {
    let opts = match parse_args() {
        Ok(o) => o,
        Err(e) => {
            if !e.is_empty() {
                eprintln!("awdlsim: {e}");
            }
            eprintln!("{USAGE}");
            return ExitCode::from(2);
        }
    };
    // SAFETY: the handler only stores to an atomic.
    unsafe {
        signal(SIGINT, on_signal);
        signal(SIGTERM, on_signal);
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    }
    let conn = match Conn::open() {
        Ok(c) => c,
        Err(e) => {
            eprintln!("awdlsim: RTL8814Native: {e}");
            return ExitCode::FAILURE;
        }
    };
    let chan = opts.channel.unwrap_or(DEFAULT_CHANNEL);
    if let Err(e) = conn.ctl(iokit::OP_ENABLE, u64::from(chan)) {
        eprintln!("awdlsim: enable: {e}");
        if e.0 == iokit::IO_BAD_ARGUMENT {
            eprintln!("awdlsim: the loaded RTL8814Native predates the sim port");
        }
        return ExitCode::FAILURE;
    }
    let capture = match opts.capture.as_deref().map(pcapng::Pcapng::create) {
        Some(Ok(c)) => Some(c),
        Some(Err(e)) => {
            eprintln!("awdlsim: capture: {e}");
            let _ = conn.ctl(iokit::OP_DISABLE, 0);
            return ExitCode::FAILURE;
        }
        None => None,
    };
    let start = now_us();
    let follow = opts.channel.is_none();
    let mac = opts.mac;
    let mut sim = Sim {
        conn,
        node: None,
        chan,
        follow,
        awdl0: None,
        next_timer: start,
        next_umi: start,
        responder: responder::Responder::new(mac),
        capture,
        win: Window::default(),
        other_ch: 0,
        other_since: 0,
        start,
        restarts: 0,
        inj_ok: 0,
        inj_offch: 0,
        inj_err: 0,
        last_inj_err: None,
        overruns: 0,
        mac_valid: false,
        tun: None,
        tun_in: 0,
        tun_out: 0,
        tun_drop: 0,
        ba: ba::Ba::new(),
        next_rotate: 0,
        plain_tx: 0,
        plain_rx: 0,
        plain_seq: 0,
        reflected: 0,
        update: 1,
        opts,
    };
    if sim.opts.tun {
        match tun::Tun::open(mac.link_local()) {
            Ok(t) => {
                println!("[{:8.3}] {} up with {}%{}", 0.0, t.name, mac.link_local(), t.name);
                sim.tun = Some(t);
            }
            Err(e) => {
                eprintln!("awdlsim: utun: {e}");
                let _ = sim.conn.ctl(iokit::OP_DISABLE, 0);
                return ExitCode::FAILURE;
            }
        }
    }
    println!(
        "[{:8.3}] sim on, {}; radio {}",
        0.0,
        if follow { "following awdl0's channel".to_string() } else { format!("channel {chan}") },
        MacAddr(sim.conn.radio_mac)
    );
    if !follow {
        sim.new_node(start);
    }
    let rc = sim.run();
    let end = now_us();
    let counters = sim.conn.ctl(iokit::OP_DISABLE, 0).or_else(|_| sim.conn.ctl(iokit::OP_QUERY, 0));
    if let Some(c) = sim.capture.as_mut() {
        let _ = c.flush();
    }
    println!(
        "[{:8.3}] done: awdl0 valid {} restarts {} overruns {} inj ok {} offch {} err {} ns {} na {} \
         echo {} other {}",
        secs(sim.start, end),
        sim.mac_valid,
        sim.restarts,
        sim.overruns,
        sim.inj_ok,
        sim.inj_offch,
        sim.inj_err,
        sim.responder.counters.ns,
        sim.responder.counters.na,
        sim.responder.counters.echo,
        sim.responder.counters.other,
    );
    if let Ok(c) = counters {
        let parts: Vec<String> =
            iokit::COUNTER_NAMES.iter().zip(c.iter()).map(|(n, v)| format!("{n} {v}")).collect();
        println!("           kernel: {}", parts.join(", "));
    }
    rc
}
