//! The RTL8814Native user client's sim selectors (Source/Driver/RTLAwdlSim.h,
//! RTLUserClient.hpp), through IOKit.

use std::ffi::{CString, c_char, c_void};
use std::fmt;

const SEL_RADIO_CALL: u32 = 9;
const SEL_SIM_CTL: u32 = 12;
const SEL_SIM_INJECT: u32 = 13;
const SEL_SIM_READ: u32 = 14;
const RADIO_MAC: u64 = 7;

pub const OP_QUERY: u64 = 0;
pub const OP_ENABLE: u64 = 1;
pub const OP_CHANNEL: u64 = 2;
pub const OP_DISABLE: u64 = 3;

pub const COUNTERS: usize = 8;
pub const COUNTER_NAMES: [&str; COUNTERS] = [
    "enabled",
    "channel",
    "tapped",
    "overruns",
    "injected",
    "inj_offch",
    "inj_nohook",
    "inj_notready",
];
pub const C_OVERRUNS: usize = 3;

const FRAME_MAX: usize = 1536;
const REC_HEADER: usize = 16;
const F_OVERRUN: u8 = 0x01;

/// IOReturn values the driver uses here.
pub const IO_UNSUPPORTED: i32 = 0xe00002c7_u32 as i32;
pub const IO_BAD_ARGUMENT: i32 = 0xe00002c2_u32 as i32;
pub const IO_EXCLUSIVE: i32 = 0xe00002c5_u32 as i32;
pub const IO_TIMEOUT: i32 = 0xe00002d6_u32 as i32;
pub const IO_OFFLINE: i32 = 0xe00002d7_u32 as i32;
pub const IO_NOT_READY: i32 = 0xe00002d8_u32 as i32;
pub const IO_NOT_PERMITTED: i32 = 0xe00002e2_u32 as i32;
pub const IO_ABORTED: i32 = 0xe00002eb_u32 as i32;

#[link(name = "IOKit", kind = "framework")]
unsafe extern "C" {
    fn IOServiceMatching(name: *const c_char) -> *mut c_void;
    fn IOServiceGetMatchingServices(main_port: u32, matching: *mut c_void, it: *mut u32) -> i32;
    fn IOIteratorNext(it: u32) -> u32;
    fn IOObjectRelease(obj: u32) -> i32;
    fn IOServiceOpen(service: u32, task: u32, kind: u32, connect: *mut u32) -> i32;
    fn IOServiceClose(connect: u32) -> i32;
    fn IOConnectCallMethod(
        connect: u32,
        selector: u32,
        input: *const u64,
        input_cnt: u32,
        input_struct: *const c_void,
        input_struct_cnt: usize,
        output: *mut u64,
        output_cnt: *mut u32,
        output_struct: *mut c_void,
        output_struct_cnt: *mut usize,
    ) -> i32;
}

unsafe extern "C" {
    static mach_task_self_: u32;
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct IoError(pub i32);

impl fmt::Display for IoError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let what = match self.0 {
            IO_UNSUPPORTED => "unsupported (driver without rtlawdl_sim=1 or too old)",
            IO_BAD_ARGUMENT => "bad argument",
            IO_EXCLUSIVE => "another client owns the sim",
            IO_TIMEOUT => "timeout",
            IO_OFFLINE => "radio on another channel",
            IO_NOT_READY => "not ready",
            IO_NOT_PERMITTED => "not permitted (run as root)",
            IO_ABORTED => "aborted (sim disabled)",
            _ => "error",
        };
        write!(f, "0x{:08x} {}", self.0 as u32, what)
    }
}

fn check(kr: i32) -> Result<(), IoError> {
    if kr == 0 { Ok(()) } else { Err(IoError(kr)) }
}

/// One tapped frame: what awdl0 handed to the radio.
pub struct Record {
    pub t_us: u64,
    pub channel: u8,
    pub overrun: bool,
    pub bytes: Vec<u8>,
}

pub struct Conn {
    connect: u32,
    pub radio_mac: [u8; 6],
}

impl Conn {
    /// The RTL8814Native instance that owns the radio: during a USB takeover
    /// the stale one still matches, but answers radio calls with NotReady.
    pub fn open() -> Result<Conn, IoError> {
        let name = CString::new("RTL8814Native").unwrap();
        let mut it = 0u32;
        let mut last = IoError(IO_NOT_READY);
        // SAFETY: plain IOKit calls; the matching dictionary is consumed by
        // IOServiceGetMatchingServices and every object is released.
        unsafe {
            check(IOServiceGetMatchingServices(0, IOServiceMatching(name.as_ptr()), &mut it))?;
            loop {
                let svc = IOIteratorNext(it);
                if svc == 0 {
                    break;
                }
                let mut connect = 0u32;
                let kr = IOServiceOpen(svc, mach_task_self_, 0, &mut connect);
                IOObjectRelease(svc);
                if kr != 0 {
                    last = IoError(kr);
                    continue;
                }
                let mut c = Conn { connect, radio_mac: [0; 6] };
                match c.mac() {
                    Ok(m) => {
                        c.radio_mac = m;
                        IOObjectRelease(it);
                        return Ok(c);
                    }
                    Err(e) => last = e,
                }
            }
            IOObjectRelease(it);
        }
        Err(last)
    }

    fn mac(&self) -> Result<[u8; 6], IoError> {
        let input = [RADIO_MAC];
        let mut out = [0u64; 1];
        let mut out_n = 1u32;
        let mut mac = [0u8; 6];
        let mut mac_n = 6usize;
        // SAFETY: buffers outlive the call and their sizes are passed.
        check(unsafe {
            IOConnectCallMethod(
                self.connect,
                SEL_RADIO_CALL,
                input.as_ptr(),
                1,
                std::ptr::null(),
                0,
                out.as_mut_ptr(),
                &mut out_n,
                mac.as_mut_ptr().cast(),
                &mut mac_n,
            )
        })?;
        Ok(mac)
    }

    pub fn ctl(&self, op: u64, arg0: u64) -> Result<[u64; COUNTERS], IoError> {
        let input = [op, arg0];
        let mut out = [0u64; COUNTERS];
        let mut out_n = COUNTERS as u32;
        // SAFETY: as above.
        let kr = unsafe {
            IOConnectCallMethod(
                self.connect,
                SEL_SIM_CTL,
                input.as_ptr(),
                2,
                std::ptr::null(),
                0,
                out.as_mut_ptr(),
                &mut out_n,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
            )
        };
        check(kr).map(|_| out)
    }

    pub fn inject(&self, frame: &[u8], rssi: i8) -> Result<(), IoError> {
        let input = [rssi as i64 as u64];
        // SAFETY: as above.
        check(unsafe {
            IOConnectCallMethod(
                self.connect,
                SEL_SIM_INJECT,
                input.as_ptr(),
                1,
                frame.as_ptr().cast(),
                frame.len(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
            )
        })
    }

    /// The oldest tapped frame, waiting up to `timeout_us` (0 polls, the
    /// driver caps it at 20 ms). `Ok(None)` on timeout.
    pub fn read(&self, timeout_us: u64) -> Result<Option<Record>, IoError> {
        let input = [timeout_us];
        let mut buf = [0u8; REC_HEADER + FRAME_MAX];
        let mut n = buf.len();
        // SAFETY: as above.
        let kr = unsafe {
            IOConnectCallMethod(
                self.connect,
                SEL_SIM_READ,
                input.as_ptr(),
                1,
                std::ptr::null(),
                0,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                buf.as_mut_ptr().cast(),
                &mut n,
            )
        };
        if kr == IO_TIMEOUT {
            return Ok(None);
        }
        check(kr)?;
        let len = u16::from_ne_bytes([buf[8], buf[9]]) as usize;
        if n < REC_HEADER || len > FRAME_MAX || n < REC_HEADER + len {
            return Err(IoError(IO_BAD_ARGUMENT));
        }
        Ok(Some(Record {
            t_us: u64::from_ne_bytes(buf[0..8].try_into().unwrap()),
            channel: buf[10],
            overrun: buf[11] & F_OVERRUN != 0,
            bytes: buf[REC_HEADER..REC_HEADER + len].to_vec(),
        }))
    }
}

impl Drop for Conn {
    fn drop(&mut self) {
        // SAFETY: the connection was opened by IOServiceOpen.
        unsafe {
            IOServiceClose(self.connect);
        }
    }
}
