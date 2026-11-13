//! --addba: the node asks awdl0 for a Block Ack session (TID 0, window 64,
//! A-MSDU allowed, immediate policy, as an iPhone does) and, once it is
//! accepted, sends its data to awdl0 as QoS data with its own sequence
//! numbers, swapping every eighth frame with the next so the layer's reorder
//! window has gaps to fill.
//!
//! The other way round, always on: when awdl0 asks the node for a session the
//! node accepts it (`answer`), and awdl0's QoS data comes back as plain data for
//! the node, with its sequence numbers checked to count up one by one
//! (`incoming`).

pub const BSSID: [u8; 6] = [0x00, 0x25, 0x00, 0xff, 0x94, 0x73];
const RETRY_US: u64 = 2_000_000;
const TRIES: u32 = 5;
const SWAP_EVERY: u64 = 8;
const PARAMS: u16 = 0x0001 | 0x0002 | (64 << 6); // A-MSDU, immediate, TID 0, 64

#[derive(Debug, PartialEq)]
pub enum State {
    Idle,
    Asked { at: u64, tries: u32 },
    Accepted { win: u16 },
    Declined { status: u16 },
    NoAnswer,
}

pub struct Ba {
    pub state: State,
    token: u8,
    seq: u16,
    held: Option<Vec<u8>>,
    pub qos: u64,
    pub swapped: u64,
    /// awdl0's session to the node: the sequence number expected next.
    rx_next: Option<u16>,
    pub rx_sessions: u64,
    pub rx_qos: u64,
    pub rx_gaps: u64,
}

impl Ba {
    pub fn new() -> Ba {
        Ba { state: State::Idle, token: 1, seq: 0x100, held: None, qos: 0, swapped: 0,
             rx_next: None, rx_sessions: 0, rx_qos: 0, rx_gaps: 0 }
    }
    /// The answer to an ADDBA request from awdl0 to the node: accepted, with
    /// the request's token, TID and policy and a window of at most 64.
    pub fn answer(&mut self, f: &[u8], node: [u8; 6]) -> Option<Vec<u8>> {
        if f.len() < 33 || f[0] != 0xd0 || f[4..10] != node || f[24] != 3 || f[25] != 0 {
            return None;
        }
        let params = u16::from_le_bytes([f[27], f[28]]);
        let win = (params >> 6).clamp(1, 64);
        self.rx_next = Some(u16::from_le_bytes([f[31], f[32]]) >> 4);
        self.rx_sessions += 1;
        let mut r = vec![0xd0, 0x00, 0x00, 0x00];
        r.extend_from_slice(&f[10..16]);
        r.extend_from_slice(&node);
        r.extend_from_slice(&BSSID);
        r.extend_from_slice(&[0x00, 0x00]);
        r.extend_from_slice(&[3, 1, f[26]]);
        r.extend_from_slice(&0u16.to_le_bytes());
        r.extend_from_slice(&((params & 0x3e) | (win << 6)).to_le_bytes());
        r.extend_from_slice(&f[29..31]);
        Some(r)
    }
    /// A frame from awdl0 as the node reads it: QoS data to the node becomes
    /// plain data, and a sequence number that is not the one after the last
    /// counts as a gap.
    pub fn incoming(&mut self, f: &[u8], node: [u8; 6]) -> Option<Vec<u8>> {
        if f.len() < 26 || f[0] != 0x88 || f[4..10] != node {
            return None;
        }
        let seq = u16::from_le_bytes([f[22], f[23]]) >> 4;
        if self.rx_next.is_some_and(|n| n != seq) {
            self.rx_gaps += 1;
        }
        self.rx_next = Some((seq + 1) & 0xfff);
        self.rx_qos += 1;
        let mut d = Vec::with_capacity(f.len() - 2);
        d.extend_from_slice(&f[..24]);
        d[0] = 0x08;
        d.extend_from_slice(&f[26..]);
        Some(d)
    }

    /// The ADDBA request to send now, if one is due.
    pub fn request(&mut self, now: u64, node: [u8; 6], awdl0: [u8; 6]) -> Option<Vec<u8>> {
        let tries = match self.state {
            State::Idle => 0,
            State::Asked { at, tries } if now - at >= RETRY_US => {
                if tries >= TRIES {
                    self.state = State::NoAnswer;
                    return None;
                }
                tries
            }
            _ => return None,
        };
        self.token = self.token.wrapping_add(1).max(1);
        self.state = State::Asked { at: now, tries: tries + 1 };
        let mut f = vec![0xd0, 0x00, 0x00, 0x00];
        f.extend_from_slice(&awdl0);
        f.extend_from_slice(&node);
        f.extend_from_slice(&BSSID);
        f.extend_from_slice(&[0x00, 0x00]);
        f.extend_from_slice(&[3, 0, self.token]);
        f.extend_from_slice(&PARAMS.to_le_bytes());
        f.extend_from_slice(&0u16.to_le_bytes());
        f.extend_from_slice(&(self.seq << 4).to_le_bytes());
        Some(f)
    }

    /// An ADDBA response from awdl0 to the node: (status, window) when it
    /// answers our current token.
    pub fn response(&mut self, f: &[u8], node: [u8; 6]) -> Option<(u16, u16, bool)> {
        if f.len() < 33 || f[0] != 0xd0 || f[4..10] != node || f[24] != 3 || f[25] != 1 {
            return None;
        }
        let status = u16::from_le_bytes([f[27], f[28]]);
        let params = u16::from_le_bytes([f[29], f[30]]);
        let ours = f[26] == self.token;
        if ours && matches!(self.state, State::Asked { .. }) {
            self.state = if status == 0 {
                State::Accepted { win: params >> 6 }
            } else {
                State::Declined { status }
            };
        }
        Some((status, params >> 6, ours))
    }

    /// The frames to inject for one the node built: unchanged before the
    /// session, else QoS data to awdl0, swapped in pairs every eighth.
    pub fn outgoing(&mut self, bytes: &[u8], awdl0: Option<[u8; 6]>) -> Vec<Vec<u8>> {
        let to_awdl0 = awdl0.is_some_and(|m| bytes.len() >= 24 && bytes[4..10] == m);
        if !matches!(self.state, State::Accepted { .. }) || bytes[0] != 0x08 || !to_awdl0 {
            return vec![bytes.to_vec()];
        }
        let mut q = Vec::with_capacity(bytes.len() + 2);
        q.extend_from_slice(&bytes[..24]);
        q[0] = 0x88;
        q[22..24].copy_from_slice(&(self.seq << 4).to_le_bytes());
        q.extend_from_slice(&[0x00, 0x00]);
        q.extend_from_slice(&bytes[24..]);
        self.seq = (self.seq + 1) & 0xfff;
        self.qos += 1;
        if let Some(h) = self.held.take() {
            self.swapped += 1;
            return vec![q, h];
        }
        if self.qos % SWAP_EVERY == 0 {
            self.held = Some(q);
            return vec![];
        }
        vec![q]
    }

    /// A held frame that found no successor, sent alone.
    pub fn flush(&mut self) -> Option<Vec<u8>> {
        self.held.take()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const NODE: [u8; 6] = [2, 0xa5, 0x3e, 0, 0, 1];
    const MAC: [u8; 6] = [2, 0, 0, 0, 0, 9];

    fn data(n: u8) -> Vec<u8> {
        let mut f = vec![0x08, 0, 0, 0];
        f.extend_from_slice(&MAC);
        f.extend_from_slice(&NODE);
        f.extend_from_slice(&BSSID);
        f.extend_from_slice(&[0, 0, n, n]);
        f
    }

    #[test]
    fn request_layout_and_retries() {
        let mut b = Ba::new();
        let r = b.request(0, NODE, MAC).unwrap();
        assert_eq!(r.len(), 33);
        assert_eq!(&r[24..27], &[3, 0, 2]);
        assert_eq!(u16::from_le_bytes([r[27], r[28]]), PARAMS);
        assert!(b.request(1_000_000, NODE, MAC).is_none());
        for i in 1..TRIES as u64 {
            assert!(b.request(i * RETRY_US, NODE, MAC).is_some());
        }
        assert!(b.request(TRIES as u64 * RETRY_US, NODE, MAC).is_none());
        assert_eq!(b.state, State::NoAnswer);
    }

    #[test]
    fn accepted_then_qos_with_swaps() {
        let mut b = Ba::new();
        let r = b.request(0, NODE, MAC).unwrap();
        let mut resp = r.clone();
        resp[4..10].copy_from_slice(&NODE);
        resp[25] = 1;
        resp[27..29].copy_from_slice(&0u16.to_le_bytes());
        resp[29..31].copy_from_slice(&(3 | (64 << 6) as u16).to_le_bytes());
        assert_eq!(b.response(&resp, NODE), Some((0, 64, true)));
        assert_eq!(b.state, State::Accepted { win: 64 });
        let mut seqs = vec![];
        for n in 0..16 {
            for f in b.outgoing(&data(n), Some(MAC)) {
                assert_eq!(f[0], 0x88);
                assert_eq!(f.len(), 28);
                seqs.push(u16::from_le_bytes([f[22], f[23]]) >> 4);
            }
        }
        let base = 0x100;
        let mut want: Vec<u16> = (0..16).map(|i| base + i).collect();
        want.swap(7, 8);
        want.truncate(15);
        assert_eq!(seqs, want);
        assert!(b.flush().is_some());
    }

    #[test]
    fn answers_a_request_and_reads_qos_in_order() {
        let mut ba = Ba::new();
        let mut req = vec![0xd0, 0, 0, 0];
        req.extend_from_slice(&NODE);
        req.extend_from_slice(&MAC);
        req.extend_from_slice(&BSSID);
        req.extend_from_slice(&[0, 0, 3, 0, 7]);
        req.extend_from_slice(&(0x0002u16 | (64 << 6)).to_le_bytes());
        req.extend_from_slice(&[0, 0]);
        req.extend_from_slice(&(0x123u16 << 4).to_le_bytes());
        assert!(ba.answer(&req, MAC).is_none());
        let r = ba.answer(&req, NODE).unwrap();
        assert_eq!(r.len(), 33);
        assert_eq!(&r[4..10], &MAC);
        assert_eq!(&r[10..16], &NODE);
        assert_eq!(&r[24..27], &[3, 1, 7]);
        assert_eq!(u16::from_le_bytes([r[27], r[28]]), 0);
        assert_eq!(u16::from_le_bytes([r[29], r[30]]), 0x0002 | (64 << 6));
        let qos = |seq: u16| {
            let mut q = vec![0x88, 0, 0, 0];
            q.extend_from_slice(&NODE);
            q.extend_from_slice(&MAC);
            q.extend_from_slice(&BSSID);
            q.extend_from_slice(&(seq << 4).to_le_bytes());
            q.extend_from_slice(&[0, 0, 0xaa, 0xbb]);
            q
        };
        let d = ba.incoming(&qos(0x123), NODE).unwrap();
        assert_eq!(d.len(), 26);
        assert_eq!(d[0], 0x08);
        assert_eq!(&d[24..], &[0xaa, 0xbb]);
        ba.incoming(&qos(0x124), NODE).unwrap();
        assert_eq!((ba.rx_qos, ba.rx_gaps), (2, 0));
        ba.incoming(&qos(0x126), NODE).unwrap();
        assert_eq!(ba.rx_gaps, 1);
        assert!(ba.incoming(&data(1), NODE).is_none());
    }
    fn untouched_before_the_session() {
        let mut b = Ba::new();
        assert_eq!(b.outgoing(&data(1), Some(MAC)), vec![data(1)]);
    }
}
