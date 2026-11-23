//! Tests for similarity.rs: the C++ suite's cases (src/cpp/test/test.cpp,
//! [levenshtein] and cosine), checked against a plain full-matrix reference.

use super::*;

/// The full (n+1) x (m+1) table, kept as plain as possible: an independent
/// reference for the optimised implementations.
fn reference_levenshtein(a: &[u8], b: &[u8]) -> u32 {
    let mut d = vec![vec![0u32; b.len() + 1]; a.len() + 1];
    for (i, row) in d.iter_mut().enumerate() {
        row[0] = i as u32;
    }
    for j in 0..=b.len() {
        d[0][j] = j as u32;
    }
    for i in 1..=a.len() {
        for j in 1..=b.len() {
            let sub = d[i - 1][j - 1] + u32::from(a[i - 1] != b[j - 1]);
            d[i][j] = sub.min(d[i - 1][j] + 1).min(d[i][j - 1] + 1);
        }
    }
    d[a.len()][b.len()]
}

/// xorshift64*: a fixed-seed generator without a dependency.
struct Rng(u64);
impl Rng {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 >> 12;
        self.0 ^= self.0 << 25;
        self.0 ^= self.0 >> 27;
        self.0.wrapping_mul(0x2545_F491_4F6C_DD1D)
    }
    fn below(&mut self, n: usize) -> usize {
        (self.next() % n as u64) as usize
    }
}

/// An edited copy (close, as funiq's matches are) or an unrelated string.
fn random_pair(rng: &mut Rng, min_len: usize, max_len: usize, alphabet: &[u8]) -> (Vec<u8>, Vec<u8>) {
    let mut random_string = |rng: &mut Rng, len: usize| -> Vec<u8> {
        (0..len).map(|_| alphabet[rng.below(alphabet.len())]).collect()
    };
    let len = min_len + rng.below(max_len - min_len + 1);
    let a = random_string(rng, len);
    let b = if rng.below(2) == 1 {
        let mut b = a.clone();
        for _ in 0..rng.below(20) {
            let at = if b.is_empty() { 0 } else { rng.below(b.len()) };
            match rng.below(3) {
                0 if !b.is_empty() => b[at] = alphabet[rng.below(alphabet.len())],
                1 => b.insert(at, alphabet[rng.below(alphabet.len())]),
                _ if !b.is_empty() => {
                    b.remove(at);
                }
                _ => {}
            }
        }
        b
    } else {
        let len = min_len + rng.below(max_len - min_len + 1);
        random_string(rng, len)
    };
    (a, b)
}

fn bounded(a: &str, b: &str, k: u32) -> u32 {
    bounded_levenshtein(a.as_bytes(), b.as_bytes(), k, &mut Scratch::new())
}

fn capped(full: u32, k: u32) -> u32 {
    if full <= k { full } else { k + 1 }
}

#[test]
fn exact_distances() {
    let exact = |a: &str, b: &str| bounded(a, b, a.len().max(b.len()) as u32);
    assert_eq!(exact("kitten", "sitting"), 3);
    assert_eq!(exact("flaw", "lawn"), 2);
    assert_eq!(exact("", "abc"), 3);
    assert_eq!(exact("same", "same"), 0);
}

#[test]
fn bounded_is_exact_up_to_k_else_k_plus_1() {
    assert_eq!(bounded("kitten", "sitting", 3), 3);
    assert_eq!(bounded("kitten", "sitting", 2), 3);
    assert_eq!(bounded("", "abc", 5), 3);
    assert_eq!(bounded("abc", "", 1), 2);
    assert_eq!(bounded("same", "same", 0), 0);
    assert_eq!(bounded("ab", "ba", 0), 1);
    assert_eq!(bounded("prefix-x-suffix", "prefix-yy-suffix", 2), 2);
}

#[test]
fn short_random_pairs_agree_with_the_reference() {
    let mut rng = Rng(20261113);
    let mut scratch = Scratch::new();
    for _ in 0..20000 {
        let (a, b) = random_pair(&mut rng, 0, 24, b"abc");
        let k = rng.below(13) as u32;
        let full = reference_levenshtein(&a, &b);
        assert_eq!(bounded_levenshtein(&a, &b, k, &mut scratch), capped(full, k), "a={a:?} b={b:?} k={k}");
    }
}

#[test]
fn word_kernel_agrees_with_the_reference_on_its_domain() {
    let mut rng = Rng(424243);
    let mut scratch = Scratch::new();
    for iter in 0..20000 {
        let alphabet: &[u8] = if iter % 2 == 1 { b"abc" } else { b"abcdefghijklmnopqrstuvwxyz0123456789" };
        let (p, q) = random_pair(&mut rng, 1, 64, alphabet);
        let (text, pattern) = if p.len() >= q.len() { (p, q) } else { (q, p) };
        if pattern.is_empty() {
            continue;
        }
        let k = rng.below(40) as u32;
        let full = reference_levenshtein(&text, &pattern);
        assert_eq!(levenshtein_word(&text, &pattern, k, &mut scratch), capped(full, k),
                   "text={text:?} pattern={pattern:?} k={k}");
    }
}

#[test]
fn band_kernel_agrees_with_the_reference_on_its_domain() {
    let mut rng = Rng(77);
    let mut scratch = Scratch::new();
    let mut checked = 0;
    for iter in 0..20000 {
        let alphabet: &[u8] = if iter % 2 == 1 { b"abc" } else { b"abcdefghijklmnopqrstuvwxyz0123456789" };
        let (p, q) = random_pair(&mut rng, 1, 200, alphabet);
        let (s1, s2) = if p.len() >= q.len() { (p, q) } else { (q, p) };
        let k = rng.below(32) as u32; // 2k + 1 <= 64
        if k as usize > s2.len() || s1.len() - s2.len() > k as usize {
            continue; // outside the band kernel's domain
        }
        let full = reference_levenshtein(&s1, &s2);
        assert_eq!(levenshtein_band(&s1, &s2, k, &mut scratch), capped(full, k), "s1={s1:?} s2={s2:?} k={k}");
        checked += 1;
    }
    assert!(checked > 5000, "only {checked} pairs in the band domain");
}

#[test]
fn dispatcher_agrees_with_the_reference_on_long_strings() {
    // every path: DP (k = 0, wide bands), column kernel, band kernel; one
    // scratch throughout, so a kernel that left its tables dirty would fail
    let mut rng = Rng(20261123);
    let mut scratch = Scratch::new();
    for iter in 0..6000 {
        let alphabet: &[u8] = if iter % 2 == 1 { b"ab" } else { b"abcdefghijklmnopqrstuvwxyz" };
        let (a, b) = random_pair(&mut rng, 0, 200, alphabet);
        let k = rng.below(46) as u32;
        let full = reference_levenshtein(&a, &b);
        assert_eq!(bounded_levenshtein(&a, &b, k, &mut scratch), capped(full, k), "a={a:?} b={b:?} k={k}");
    }
}

#[test]
fn cosine_basics() {
    let d = |a: &str, b: &str| cosine_distance(&trigram_profile(a.as_bytes()), &trigram_profile(b.as_bytes()));
    assert!(d("abcdef", "abcdef").abs() < 1e-12);
    assert!((d("abcdef", "uvwxyz") - 1.0).abs() < 1e-12);
    assert_eq!(d("", ""), 0.0);
    assert_eq!(d("", "abc"), 1.0);
    assert!(d("ab", "ab").abs() < 1e-12);
    assert!((d("ab", "abc") - 1.0).abs() < 1e-12); // a short string's gram never equals a trigram
}

#[test]
fn trigrams_are_packed_exactly() {
    let p = trigram_profile(b"abab");
    assert_eq!(p.grams, vec![(0x616261, 1), (0x626162, 1)]);
    assert_eq!(trigram_profile(b"aaaa").grams, vec![(0x616161, 2)]);
    assert_eq!(trigram_profile(b"ab").grams, vec![(1 << 24 | 2 << 16 | 0x61 << 8 | 0x62, 1)]);
    assert_eq!(trigram_profile(b"a").grams, vec![(1 << 24 | 1 << 16 | 0x61 << 8, 1)]);
    assert!(trigram_profile(b"").grams.is_empty());
    assert_eq!(trigram_profile(b"aaaa").norm, 2.0);
}
