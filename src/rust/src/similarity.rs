//! Distance functions, a line-by-line port of the C++ similarity.h: bounded
//! Levenshtein (Hyyro's bit-parallel band and column kernels, banded DP
//! otherwise) and character-trigram cosine distance. Techniques from
//! rapidfuzz-cpp (MIT), as in the C++.

/// The bit-parallel kernels win from k = 1 against the banded DP (measured on
/// the C++); k = 0 stays on the DP, which settles it in one row.
const BIT_PARALLEL_MIN_K: u32 = 1;
/// Position of the band kernel's untouched window entries.
const FAR_PAST: isize = isize::MIN / 4;

/// Reusable working memory, one per thread, so the hot loop never allocates.
/// The per-byte tables are back to their initial state after every call
/// (each kernel resets only the entries it touched).
pub struct Scratch {
    rows: Vec<u32>,
    /// column kernel: pattern positions of each byte
    peq: [u64; 256],
    /// band kernel: (position of bit 63, bits) of each byte's window
    window: [(isize, u64); 256],
}

impl Scratch {
    pub fn new() -> Self {
        Scratch { rows: Vec::new(), peq: [0; 256], window: [(FAR_PAST, 0); 256] }
    }
}

#[inline]
fn shr64(x: u64, s: isize) -> u64 {
    debug_assert!(s >= 0);
    if s < 64 { x >> s } else { 0 }
}

/// Hyyro's bit-parallel Levenshtein (2003) for a pattern of 1 to 64 bytes:
/// one word holds a whole DP column. Stops once the last row, which moves by
/// at most one per remaining column, can no longer come back within k.
/// Exact distance when <= k, else k + 1.
pub fn levenshtein_word(text: &[u8], pattern: &[u8], k: u32, s: &mut Scratch) -> u32 {
    let (n, m) = (text.len(), pattern.len());
    debug_assert!((1..=64).contains(&m));
    for (j, &c) in pattern.iter().enumerate() {
        s.peq[c as usize] |= 1u64 << j;
    }
    let (mut vp, mut vn) = (!0u64, 0u64);
    let last = 1u64 << (m - 1);
    let mut dist = m as u64;
    let mut i = 0;
    while i < n {
        let x = s.peq[text[i] as usize];
        let d0 = (((x & vp).wrapping_add(vp)) ^ vp) | x | vn;
        let mut hp = vn | !(d0 | vp);
        let mut hn = d0 & vp;
        dist += u64::from(hp & last != 0);
        dist -= u64::from(hn & last != 0);
        if dist > u64::from(k) + (n - 1 - i) as u64 {
            break;
        }
        hp = (hp << 1) | 1;
        hn <<= 1;
        vp = hn | !(d0 | hp);
        vn = hp & d0;
        i += 1;
    }
    for &c in pattern {
        s.peq[c as usize] = 0;
    }
    if i < n || dist > u64::from(k) { k + 1 } else { dist as u32 }
}

/// Hyyro's banded variant: the 2k + 1 diagonals fit one word (k <= 31), the
/// pattern bits are a window sliding along s1, and it gives up once the
/// diagonal, which never decreases, passes the bound. Requires s1 the longer,
/// k <= len(s2) and len(s1) - len(s2) <= k. Exact when <= k, else k + 1.
pub fn levenshtein_band(s1: &[u8], s2: &[u8], k: u32, s: &mut Scratch) -> u32 {
    let (n, m, ku) = (s1.len(), s2.len(), k as usize);
    debug_assert!(ku <= m && n - m <= ku && 2 * ku < 64);
    let pm = &mut s.window;
    let mut inserted = 0usize; // s1 bytes added to the window so far
    fn insert(pm: &mut [(isize, u64); 256], c: u8, at: isize) {
        let w = &mut pm[c as usize];
        w.1 = shr64(w.1, at - w.0) | (1u64 << 63);
        w.0 = at;
    }
    let lookup = |pm: &[(isize, u64); 256], c: u8, at: isize| {
        let w = pm[c as usize];
        shr64(w.1, at - w.0)
    };

    let mut vp = !0u64 << (64 - ku - 1);
    let mut vn = 0u64;
    let mut dist = ku;
    let diagonal = 1u64 << 63;
    let mut horizontal = 1u64 << 62;
    // the score can fall along the horizontal stretch, never on the diagonal
    let break_score = 2 * ku + m - n;
    let mut gave_up = false;

    for j in -(ku as isize)..0 {
        insert(pm, s1[inserted], j);
        inserted += 1;
    }
    let mut i = 0usize;
    while i < n - ku {
        insert(pm, s1[inserted], i as isize);
        inserted += 1;
        let x = lookup(pm, s2[i], i as isize);
        let d0 = (((x & vp).wrapping_add(vp)) ^ vp) | x | vn;
        let hp = vn | !(d0 | vp);
        let hn = d0 & vp;
        dist += usize::from(d0 & diagonal == 0);
        if dist > break_score {
            gave_up = true;
            break;
        }
        vp = hn | !((d0 >> 1) | hp);
        vn = (d0 >> 1) & hp;
        i += 1;
    }
    if !gave_up {
        while i < m {
            if inserted < n {
                insert(pm, s1[inserted], i as isize);
                inserted += 1;
            }
            let x = lookup(pm, s2[i], i as isize);
            let d0 = (((x & vp).wrapping_add(vp)) ^ vp) | x | vn;
            let hp = vn | !(d0 | vp);
            let hn = d0 & vp;
            dist += usize::from(hp & horizontal != 0);
            dist -= usize::from(hn & horizontal != 0);
            horizontal >>= 1;
            if dist > break_score {
                gave_up = true;
                break;
            }
            vp = hn | !((d0 >> 1) | hp);
            vn = (d0 >> 1) & hp;
            i += 1;
        }
    }
    for &c in &s1[..inserted] {
        pm[c as usize] = (FAR_PAST, 0);
    }
    if gave_up || dist > ku { k + 1 } else { dist as u32 }
}

/// Levenshtein distance with a cutoff: the exact distance when it is <= k,
/// otherwise k + 1. Length bound, common affix strip, then the bit-parallel
/// kernels where they fit and a banded DP that aborts once a whole row
/// exceeds k otherwise.
pub fn bounded_levenshtein(s1: &[u8], s2: &[u8], k: u32, s: &mut Scratch) -> u32 {
    let (mut a, mut b) = if s1.len() >= s2.len() { (s1, s2) } else { (s2, s1) };
    if (a.len() - b.len()) as u64 > u64::from(k) {
        return k + 1;
    }
    let prefix = a.iter().zip(b).take_while(|(x, y)| x == y).count();
    a = &a[prefix..];
    b = &b[prefix..];
    let suffix = a.iter().rev().zip(b.iter().rev()).take_while(|(x, y)| x == y).count();
    a = &a[..a.len() - suffix];
    b = &b[..b.len() - suffix];
    let (n, m) = (a.len(), b.len());
    if m == 0 {
        return n as u32; // n <= k, guaranteed by the length bound
    }

    if k >= BIT_PARALLEL_MIN_K {
        if k as usize <= m && 2 * k as usize + 1 <= 64 {
            return levenshtein_band(a, b, k, s);
        }
        if m <= 64 {
            return levenshtein_word(a, b, k, s);
        }
    }

    let ku = k as usize;
    if s.rows.len() < 2 * (m + 2) {
        s.rows.resize(2 * (m + 2), 0);
    }
    let (mut prev, mut cur) = s.rows.split_at_mut(m + 2);
    let inf = k + 1; // anything > k is "too far"
    // Row 0 only needs initialising inside the band: row i reads prev up to
    // column i + k, and every later row writes its own sentinel past its band.
    for (j, cell) in prev.iter_mut().enumerate().take(m.min(ku + 1) + 1) {
        *cell = (j as u32).min(inf);
    }
    for i in 1..=n {
        let lo = if i > ku { i - ku } else { 1 };
        let hi = m.min(i + ku);
        cur[lo - 1] = if lo == 1 { (i as u32).min(inf) } else { inf };
        let mut row_min = cur[lo - 1];
        for j in lo..=hi {
            let v = (prev[j - 1] + u32::from(a[i - 1] != b[j - 1])).min(prev[j] + 1).min(cur[j - 1] + 1);
            cur[j] = v.min(inf);
            row_min = row_min.min(cur[j]);
        }
        if hi < m {
            cur[hi + 1] = inf;
        }
        if row_min > k {
            return k + 1;
        }
        std::mem::swap(&mut prev, &mut cur);
    }
    prev[m].min(inf)
}

/// Character-trigram counts sorted by gram, plus their Euclidean norm.
pub struct Profile {
    pub grams: Vec<(u32, u32)>,
    pub norm: f64,
}

/// Trigrams packed exactly into 24 bits; a string of 1-2 bytes is one gram
/// tagged above bit 24; the empty string has none.
pub fn trigram_profile(s: &[u8]) -> Profile {
    let mut keys: Vec<u32> = if s.len() >= 3 {
        s.windows(3).map(|w| u32::from(w[0]) << 16 | u32::from(w[1]) << 8 | u32::from(w[2])).collect()
    } else if !s.is_empty() {
        vec![1 << 24 | (s.len() as u32) << 16 | u32::from(s[0]) << 8 | u32::from(*s.get(1).unwrap_or(&0))]
    } else {
        Vec::new()
    };
    keys.sort_unstable();
    let mut grams: Vec<(u32, u32)> = Vec::new();
    let mut sum_squares = 0.0f64;
    let mut i = 0;
    while i < keys.len() {
        let j = i + keys[i..].iter().take_while(|&&key| key == keys[i]).count();
        let count = (j - i) as u32;
        grams.push((keys[i], count));
        sum_squares += f64::from(count) * f64::from(count);
        i = j;
    }
    Profile { grams, norm: sum_squares.sqrt() }
}

/// 1 - cosine similarity, in [0, 1]: a sorted merge, accumulated in f64 in
/// the same order as the C++. Two empty profiles are identical (0); an empty
/// and a non-empty one share nothing (1).
pub fn cosine_distance(a: &Profile, b: &Profile) -> f64 {
    if a.norm == 0.0 || b.norm == 0.0 {
        return if a.norm == 0.0 && b.norm == 0.0 { 0.0 } else { 1.0 };
    }
    let mut dot = 0.0f64;
    let (mut i, mut j) = (0, 0);
    while i < a.grams.len() && j < b.grams.len() {
        match a.grams[i].0.cmp(&b.grams[j].0) {
            std::cmp::Ordering::Less => i += 1,
            std::cmp::Ordering::Greater => j += 1,
            std::cmp::Ordering::Equal => {
                dot += f64::from(a.grams[i].1) * f64::from(b.grams[j].1);
                i += 1;
                j += 1;
            }
        }
    }
    let distance = 1.0 - dot / (a.norm * b.norm);
    if distance < 0.0 { 0.0 } else { distance } // rounding can dip just below 0
}

#[cfg(test)]
#[path = "similarity_tests.rs"]
mod tests;
