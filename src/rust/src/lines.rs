//! Input lines as bytes (spec §3, Input): a line ends at \n, a last line
//! without one still counts, and on Windows one trailing \r is dropped (what
//! the C++ build's CRLF handling does); 0x1A and every other byte is data.

use std::io::{BufRead, BufReader, Read};

/// Reads through a 1 MiB buffer, like the C++ LineReader. Not `lines()`:
/// that rejects non-UTF-8 input and strips \r everywhere.
pub struct LineReader<R: Read> {
    inner: BufReader<R>,
}

impl<R: Read> LineReader<R> {
    pub fn new(inner: R) -> Self {
        LineReader { inner: BufReader::with_capacity(1 << 20, inner) }
    }

    /// The next line without its terminator; false at the end of the input.
    pub fn next_line(&mut self, line: &mut Vec<u8>) -> std::io::Result<bool> {
        line.clear();
        if self.inner.read_until(b'\n', line)? == 0 {
            return Ok(false);
        }
        if line.last() == Some(&b'\n') {
            line.pop();
        }
        if cfg!(windows) && line.last() == Some(&b'\r') {
            line.pop();
        }
        Ok(true)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    fn all_lines(input: &[u8]) -> Vec<Vec<u8>> {
        let mut r = LineReader::new(Cursor::new(input.to_vec()));
        let mut out = Vec::new();
        let mut line = Vec::new();
        while r.next_line(&mut line).unwrap() {
            out.push(line.clone());
        }
        out
    }

    #[test]
    fn empty_lines_and_an_unterminated_last_line() {
        assert_eq!(all_lines(b"a\n\nb"), vec![b"a".to_vec(), b"".to_vec(), b"b".to_vec()]);
    }

    #[test]
    fn empty_input_has_no_lines() {
        assert!(all_lines(b"").is_empty());
        assert_eq!(all_lines(b"\n"), vec![b"".to_vec()]);
    }

    #[test]
    fn a_line_longer_than_the_buffer_comes_back_whole() {
        let mut input = vec![b'x'; 3_000_000];
        input.extend_from_slice(b"\nx");
        let lines = all_lines(&input);
        assert_eq!(lines.len(), 2);
        assert_eq!(lines[0].len(), 3_000_000);
        assert_eq!(lines[1], b"x");
    }

    #[test]
    fn crlf_per_platform_and_0x1a_is_data() {
        let lines = all_lines(b"a\r\nb\x1ac\n");
        if cfg!(windows) {
            assert_eq!(lines, vec![b"a".to_vec(), b"b\x1ac".to_vec()]);
        } else {
            assert_eq!(lines, vec![b"a\r".to_vec(), b"b\x1ac".to_vec()]);
        }
    }

    #[test]
    fn non_utf8_and_nul_bytes_pass_through() {
        assert_eq!(all_lines(b"caf\xe9\nn\x00l\n"), vec![b"caf\xe9".to_vec(), b"n\x00l".to_vec()]);
    }
}
