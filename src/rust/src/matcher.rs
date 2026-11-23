//! Grouping, a port of the C++ Matcher.h: normalisation, the memo,
//! first-match / --best grouping, two-phase threaded batches, counts-only
//! storage and output. Results never depend on the thread count.

use std::collections::HashMap;
use std::io::Write;
use std::sync::Mutex;

use rayon::prelude::*;

use crate::settings::{Method, Rep, Settings};
use crate::similarity::{
    bounded_levenshtein, build_histogram, cosine_distance, histogram_bound, trigram_profile, ByteHistogram, Profile, Scratch,
};

const NO_GROUP: usize = usize::MAX;

/// The group a line joins (NO_GROUP: start a new one) and its distance.
#[derive(Clone, Copy)]
struct Hit {
    group: usize,
    score: f64,
}

const MISS: Hit = Hit { group: NO_GROUP, score: 0.0 };

/// A line prepared for comparison: normalized once, profiled once (cosine).
struct Candidate {
    normalized: Vec<u8>,
    profile: Profile,
    /// built only when a scan needs it (not on memo hits): boxed, so a line
    /// carries 8 bytes rather than 128
    histogram: Option<Box<ByteHistogram>>,
}

/// The first line (the key matching compares against), its normalized form
/// and profile, the group size, and members only when they are printed.
struct Group {
    key: Vec<u8>,
    normalized_key: Vec<u8>,
    profile: Profile,
    count: usize,
    longest: Vec<u8>,
    longest_at: usize,
    matches: Vec<Vec<u8>>,
    scores: Vec<f64>,
}

/// C isspace in the C locale: space, \t, \n, \v, \f, \r.
fn is_c_space(c: u8) -> bool {
    matches!(c, b' ' | b'\t' | b'\n' | 0x0b | 0x0c | b'\r')
}

/// The string actually compared: lowercase (-i), digit runs as one 0 (-D),
/// then sorted words (token-sort; with -I punctuation separates words) or
/// non-alphanumerics deleted (-I). ASCII only, like <cctype> in the C locale.
pub fn normalize(line: &[u8], s: &Settings) -> Vec<u8> {
    let mut r = line.to_vec();
    if s.case_insensitive {
        r.make_ascii_lowercase();
    }
    if s.mask_digits {
        let mut masked = Vec::with_capacity(r.len());
        for (i, &c) in r.iter().enumerate() {
            if c.is_ascii_digit() {
                if i == 0 || !r[i - 1].is_ascii_digit() {
                    masked.push(b'0');
                }
            } else {
                masked.push(c);
            }
        }
        r = masked;
    }
    if s.method == Method::TokenSort {
        if s.ignore_non_alnum {
            for c in r.iter_mut() {
                if !c.is_ascii_alphanumeric() {
                    *c = b' ';
                }
            }
        }
        let mut tokens: Vec<&[u8]> = r.split(|&c| is_c_space(c)).filter(|t| !t.is_empty()).collect();
        tokens.sort_unstable();
        tokens.join(&b' ')
    } else {
        if s.ignore_non_alnum {
            r.retain(u8::is_ascii_alphanumeric);
        }
        r
    }
}

pub struct Matcher {
    opts: Settings,
    groups: Vec<Group>,
    /// normalized line -> where it went (first-match only; see memo_enabled)
    group_of: HashMap<Vec<u8>, Hit>,
    /// key_lengths[i] == groups[i].normalized_key.len(), contiguous for the
    /// length pre-check
    key_lengths: Vec<u32>,
    /// key_histograms[i]: groups[i].normalized_key's byte counts, for the
    /// prefilter (empty for -m cosine, which has no edit budget)
    key_histograms: Vec<ByteHistogram>,
    scratch: Scratch,
    /// add_batch's threads, built once, each with its own persistent scratch
    pool: Option<rayon::ThreadPool>,
    pool_scratch: Vec<Mutex<Scratch>>,
}

impl Matcher {
    pub fn new(settings: Settings) -> Matcher {
        let threads = settings.threads.max(1);
        let pool = (threads > 1)
            .then(|| rayon::ThreadPoolBuilder::new().num_threads(threads).build().expect("thread pool"));
        Matcher {
            opts: settings,
            groups: Vec::new(),
            group_of: HashMap::new(),
            key_lengths: Vec::new(),
            key_histograms: Vec::new(),
            scratch: Scratch::new(),
            pool_scratch: (0..if pool.is_some() { threads } else { 0 }).map(|_| Mutex::new(Scratch::new())).collect(),
            pool,
        }
    }

    fn keep_members(&self) -> bool {
        self.opts.show_all || self.opts.show_scores
    }

    /// A negative threshold matches nothing, not even an identical line, so
    /// repeats must not be memoised there; --best can prefer a newer group.
    fn memo_enabled(&self) -> bool {
        self.opts.max_distance >= 0.0 && !self.opts.best
    }

    fn prepare(&self, line: &[u8]) -> Candidate {
        let normalized = normalize(line, &self.opts);
        let profile = if self.opts.method == Method::Cosine { trigram_profile(&normalized) } else { Profile::default() };
        Candidate { normalized, profile, histogram: None }
    }

    fn uses_histogram(&self) -> bool {
        self.opts.method != Method::Cosine
    }

    /// Before a scan: the candidate's histogram, if the method uses one.
    fn prepare_scan(&self, c: &mut Candidate) {
        if self.uses_histogram() && c.histogram.is_none() {
            c.histogram = Some(Box::new(build_histogram(&c.normalized)));
        }
    }

    /// The candidate can't be within k edits of group i (byte counts differ
    /// by more than k).
    fn histogram_rules_out(&self, c: &Candidate, i: usize, k: u32) -> bool {
        let h = c.histogram.as_deref().expect("prepare_scan runs before a scan");
        histogram_bound(h, &self.key_histograms[i]) > k
    }

    fn lookup_memo(&self, normalized: &[u8]) -> Hit {
        if !self.memo_enabled() {
            return MISS;
        }
        self.group_of.get(normalized).copied().unwrap_or(MISS)
    }

    /// Distance is at least the length difference: skip pairs that can't
    /// match, with the same f32 arithmetic as distance_to. None for cosine.
    fn length_rules_out(&self, len: u32, key_len: u32) -> bool {
        if self.opts.method == Method::Cosine {
            return false;
        }
        let diff = len.abs_diff(key_len);
        if self.opts.method != Method::Levenshtein {
            let max_len = len.max(key_len);
            return max_len > 0 && diff as f32 / max_len as f32 > self.opts.max_distance;
        }
        diff as f32 > self.opts.max_distance
    }

    /// The distance to group i's key when within the threshold, in the
    /// method's units, computed exactly as the C++ (f32 ratio, f64 budget).
    fn distance_to(&self, c: &Candidate, i: usize, scratch: &mut Scratch) -> Option<f64> {
        let d = self.opts.max_distance;
        if !(d >= 0.0) {
            return None;
        }
        let g = &self.groups[i];
        if self.opts.method == Method::Cosine {
            let score = cosine_distance(&c.profile, &g.profile);
            return (score <= f64::from(d)).then_some(score);
        }
        let (s1, s2) = (&c.normalized, &g.normalized_key);
        let max_len = s1.len().max(s2.len()) as u32;
        if self.opts.method != Method::Levenshtein {
            if max_len == 0 {
                return Some(0.0);
            }
            // one edit of slack so rounding can't make k too tight; the f32
            // test has the final say
            let budget = f64::from(d) * f64::from(max_len);
            let k = if budget >= f64::from(max_len) { max_len } else { budget as u32 + 1 };
            if self.histogram_rules_out(c, i, k) {
                return None;
            }
            let ratio = bounded_levenshtein(s1, s2, k, scratch) as f32 / max_len as f32;
            return (ratio <= d).then_some(f64::from(ratio));
        }
        let k = if d >= max_len as f32 { max_len } else { d as u32 };
        if self.histogram_rules_out(c, i, k) {
            return None;
        }
        let dist = bounded_levenshtein(s1, s2, k, scratch);
        (dist as f32 <= d).then_some(f64::from(dist))
    }

    /// Without --best the first group in [from, to) within the threshold;
    /// with --best the closest, lowest index on ties.
    fn find_match(&self, c: &Candidate, from: usize, to: usize, scratch: &mut Scratch) -> Hit {
        let len = c.normalized.len() as u32;
        let mut best = MISS;
        for i in from..to {
            if self.length_rules_out(len, self.key_lengths[i]) {
                continue;
            }
            let Some(score) = self.distance_to(c, i, scratch) else { continue };
            if !self.opts.best {
                return Hit { group: i, score };
            }
            if best.group == NO_GROUP || score < best.score {
                best = Hit { group: i, score };
            }
            if score == 0.0 {
                break; // nothing beats an exact match
            }
        }
        best
    }

    fn place(&mut self, line: Vec<u8>, c: Candidate, hit: Hit) {
        if self.memo_enabled() {
            let memo = if hit.group == NO_GROUP { Hit { group: self.groups.len(), score: 0.0 } } else { hit };
            self.group_of.entry(c.normalized.clone()).or_insert(memo);
        }
        let keep = self.keep_members();
        let longest = self.opts.rep == Rep::Longest;
        if hit.group != NO_GROUP {
            let g = &mut self.groups[hit.group];
            if longest && line.len() > g.longest.len() {
                g.longest = line.clone();
                g.longest_at = g.count;
            }
            g.count += 1;
            if keep {
                g.matches.push(line);
                g.scores.push(hit.score);
            }
            return;
        }
        self.key_lengths.push(c.normalized.len() as u32);
        if let Some(h) = c.histogram {
            self.key_histograms.push(*h); // only memo misses start groups, and they have one
        }
        self.groups.push(Group {
            longest: if longest { line.clone() } else { Vec::new() },
            longest_at: 0,
            matches: if keep { vec![line.clone()] } else { Vec::new() },
            scores: if keep { vec![0.0] } else { Vec::new() },
            key: line,
            normalized_key: c.normalized,
            profile: c.profile,
            count: 1,
        });
    }

    /// A line joins only one group, so counts sum to the number of lines.
    /// The serial path: the binary uses add_batch, the tests compare the two.
    #[cfg_attr(not(test), allow(dead_code))]
    pub fn add(&mut self, line: Vec<u8>) {
        let mut c = self.prepare(&line);
        let mut hit = self.lookup_memo(&c.normalized);
        if hit.group == NO_GROUP {
            self.prepare_scan(&mut c);
            let mut scratch = std::mem::take(&mut self.scratch);
            hit = self.find_match(&c, 0, self.groups.len(), &mut scratch);
            self.scratch = scratch;
        }
        self.place(line, c, hit);
    }

    /// The same result as add() on each line in order: phase 1 matches every
    /// line against the groups that existed before the batch, in parallel;
    /// phase 2, serial and in input order, checks the groups created earlier
    /// in the batch (all with higher indices).
    pub fn add_batch(&mut self, lines: Vec<Vec<u8>>) {
        let frozen = self.groups.len();
        let phase1 = |this: &Matcher, line: &Vec<u8>, scratch: &mut Scratch| {
            let mut c = this.prepare(line);
            let memo = this.lookup_memo(&c.normalized);
            let hit = if memo.group != NO_GROUP {
                memo
            } else {
                this.prepare_scan(&mut c); // phase 2 reuses it
                this.find_match(&c, 0, frozen, scratch)
            };
            (c, hit)
        };
        let prepared: Vec<(Candidate, Hit)> = match &self.pool {
            Some(pool) => pool.install(|| {
                lines
                    .par_iter()
                    .map(|line| {
                        let worker = rayon::current_thread_index().unwrap_or(0);
                        let mut scratch = self.pool_scratch[worker].lock().unwrap();
                        phase1(self, line, &mut scratch)
                    })
                    .collect()
            }),
            None => {
                let mut scratch = std::mem::take(&mut self.scratch);
                let out = lines.iter().map(|line| phase1(self, line, &mut scratch)).collect();
                self.scratch = scratch;
                out
            }
        };

        let mut scratch = std::mem::take(&mut self.scratch);
        for (line, (c, first)) in lines.into_iter().zip(prepared) {
            // an earlier line in this batch may have memoised it
            let mut h = self.lookup_memo(&c.normalized);
            if h.group == NO_GROUP {
                h = first;
                if self.opts.best {
                    let later = self.find_match(&c, frozen, self.groups.len(), &mut scratch);
                    if later.group != NO_GROUP && (h.group == NO_GROUP || later.score < h.score) {
                        h = later;
                    }
                } else if h.group == NO_GROUP {
                    h = self.find_match(&c, frozen, self.groups.len(), &mut scratch);
                }
            }
            self.place(line, c, h);
        }
        self.scratch = scratch;
    }

    fn format_score(&self, score: f64) -> String {
        if self.opts.method == Method::Levenshtein {
            (score as u64).to_string()
        } else {
            format!("{score:.3}")
        }
    }

    /// Groups sorted by representative bytes (stable: equal representatives
    /// keep creation order), as `uniq` would print sorted input.
    pub fn write_to<W: Write>(&self, out: &mut W) -> std::io::Result<()> {
        let longest = self.opts.rep == Rep::Longest;
        let mut rows: Vec<(&Group, &[u8], usize)> = self
            .groups
            .iter()
            .map(|g| if longest { (g, g.longest.as_slice(), g.longest_at) } else { (g, g.key.as_slice(), 0) })
            .collect();
        rows.sort_by(|a, b| a.1.cmp(b.1));
        let all = self.keep_members();
        for (g, rep, rep_at) in rows {
            if self.opts.show_counts {
                write!(out, "{:>7} ", g.count)?;
            }
            out.write_all(rep)?;
            if all {
                for (j, member) in g.matches.iter().enumerate() {
                    if j == rep_at {
                        continue;
                    }
                    out.write_all(b"\t")?;
                    out.write_all(member)?;
                    if self.opts.show_scores {
                        write!(out, " ({})", self.format_score(g.scores[j]))?;
                    }
                }
            }
            out.write_all(b"\n")?;
        }
        Ok(())
    }
}

#[cfg(test)]
#[path = "matcher_tests.rs"]
mod tests;
