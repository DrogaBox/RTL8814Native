//! Both directions of a run as pcapng: 802.11 frames without radiotap
//! (link type 105), microsecond timestamps on the mach clock, the direction
//! in each packet's comment.

use std::fs::File;
use std::io::{self, BufWriter, Write};

pub struct Pcapng {
    out: BufWriter<File>,
}

fn pad4(n: usize) -> usize {
    (n + 3) & !3
}

impl Pcapng {
    pub fn create(path: &str) -> io::Result<Pcapng> {
        let mut out = BufWriter::new(File::create(path)?);
        // Section Header Block
        let mut shb = Vec::new();
        shb.extend_from_slice(&0x0a0d0d0a_u32.to_le_bytes());
        shb.extend_from_slice(&28u32.to_le_bytes());
        shb.extend_from_slice(&0x1a2b3c4d_u32.to_le_bytes());
        shb.extend_from_slice(&1u16.to_le_bytes());
        shb.extend_from_slice(&0u16.to_le_bytes());
        shb.extend_from_slice(&(-1i64).to_le_bytes());
        shb.extend_from_slice(&28u32.to_le_bytes());
        out.write_all(&shb)?;
        // Interface Description Block: IEEE 802.11, default (microsecond) resolution
        let mut idb = Vec::new();
        idb.extend_from_slice(&1u32.to_le_bytes());
        idb.extend_from_slice(&20u32.to_le_bytes());
        idb.extend_from_slice(&105u16.to_le_bytes());
        idb.extend_from_slice(&0u16.to_le_bytes());
        idb.extend_from_slice(&65535u32.to_le_bytes());
        idb.extend_from_slice(&20u32.to_le_bytes());
        out.write_all(&idb)?;
        Ok(Pcapng { out })
    }

    /// One Enhanced Packet Block with an opt_comment.
    pub fn frame(&mut self, t_us: u64, bytes: &[u8], comment: &str) -> io::Result<()> {
        let c = comment.as_bytes();
        let opts = 4 + pad4(c.len()) + 4;
        let total = 28 + pad4(bytes.len()) + opts + 4;
        let mut b = Vec::with_capacity(total);
        b.extend_from_slice(&6u32.to_le_bytes());
        b.extend_from_slice(&(total as u32).to_le_bytes());
        b.extend_from_slice(&0u32.to_le_bytes());
        b.extend_from_slice(&((t_us >> 32) as u32).to_le_bytes());
        b.extend_from_slice(&(t_us as u32).to_le_bytes());
        b.extend_from_slice(&(bytes.len() as u32).to_le_bytes());
        b.extend_from_slice(&(bytes.len() as u32).to_le_bytes());
        b.extend_from_slice(bytes);
        b.resize(28 + pad4(bytes.len()), 0);
        b.extend_from_slice(&1u16.to_le_bytes());
        b.extend_from_slice(&(c.len() as u16).to_le_bytes());
        b.extend_from_slice(c);
        b.resize(28 + pad4(bytes.len()) + 4 + pad4(c.len()), 0);
        b.extend_from_slice(&[0, 0, 0, 0]); // opt_endofopt
        b.extend_from_slice(&(total as u32).to_le_bytes());
        debug_assert_eq!(b.len(), total);
        self.out.write_all(&b)
    }

    pub fn flush(&mut self) -> io::Result<()> {
        self.out.flush()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn blocks_are_aligned_and_sized() {
        let path = std::env::temp_dir().join(format!("awdlsim-{}.pcapng", std::process::id()));
        let p = path.to_str().unwrap();
        let mut c = Pcapng::create(p).unwrap();
        c.frame(1_234_567, &[0xd0, 0, 0, 0, 0xff, 0xff, 0xff], "awdl0 > air ch 149").unwrap();
        c.frame(2_000_000, &[0x08, 0x00], "node > awdl0").unwrap();
        c.flush().unwrap();
        let d = std::fs::read(&path).unwrap();
        std::fs::remove_file(&path).unwrap();
        let mut off = 0;
        let mut kinds = Vec::new();
        while off < d.len() {
            let kind = u32::from_le_bytes(d[off..off + 4].try_into().unwrap());
            let len = u32::from_le_bytes(d[off + 4..off + 8].try_into().unwrap()) as usize;
            assert_eq!(len % 4, 0);
            assert_eq!(&d[off + len - 4..off + len], &d[off + 4..off + 8]);
            kinds.push(kind);
            off += len;
        }
        assert_eq!(off, d.len());
        assert_eq!(kinds, vec![0x0a0d0d0a, 1, 6, 6]);
    }
}
