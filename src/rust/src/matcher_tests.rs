//! Tests for matcher.rs: the C++ suite's [matcher] and [tier3] cases with
//! the same inputs and expected rows (src/cpp/test/test.cpp).

use super::*;
use crate::settings::{Method, Rep, Settings};

fn settings() -> Settings {
    Settings::default()
}

fn render(m: &Matcher) -> String {
    let mut out = Vec::new();
    m.write_to(&mut out).unwrap();
    String::from_utf8(out).unwrap()
}

fn rows(s: &Settings, lines: &[&str]) -> Vec<String> {
    let mut m = Matcher::new(s.clone());
    for line in lines {
        m.add(line.as_bytes().to_vec());
    }
    render(&m).lines().map(String::from).collect()
}

fn v(rows: &[&str]) -> Vec<String> {
    rows.iter().map(|r| r.to_string()).collect()
}

const SHIPS: [&str; 10] = ["USS Enterprise", "uss enterprize", "U.S.S. Enterprise", "Enterprize", "USS Voyager",
                           "USS Voyagr", "uss-voyager", "USS Defiant", "USS Defient", "Defiant"];

#[test]
fn readme_ships() {
    let mut s = settings();
    s.case_insensitive = true;
    s.ignore_non_alnum = true;
    assert_eq!(rows(&s, &SHIPS), v(&["Enterprize", "USS Defiant", "USS Enterprise", "USS Voyager"]));
    s.max_distance = 4.0;
    assert_eq!(rows(&s, &SHIPS), v(&["USS Defiant", "USS Enterprise", "USS Voyager"]));
}

#[test]
fn normalized_threshold_scales_with_length() {
    let mut s = settings();
    s.method = Method::NormalizedLevenshtein;
    s.max_distance = 0.25;
    s.show_counts = true;
    assert_eq!(rows(&s, &["ab", "ac", "abcd", "abce"]), v(&["      1 ab", "      2 abcd", "      1 ac"]));
}

#[test]
fn a_repeated_line_joins_its_first_occurrences_group() {
    let mut s = settings();
    s.max_distance = 1.0;
    s.show_all = true;
    assert_eq!(rows(&s, &["aaaa", "zzzz", "aaab", "zzzz", "aaab"]), v(&["aaaa\taaab\taaab", "zzzz\tzzzz"]));
}

#[test]
fn show_all_prints_members_as_written() {
    let mut s = settings();
    s.case_insensitive = true;
    s.ignore_non_alnum = true;
    s.show_all = true;
    assert_eq!(rows(&s, &["Hello, World!", "HELLO WORLD"]), v(&["Hello, World!\tHELLO WORLD"]));
}

#[test]
fn a_line_joins_only_the_first_group_it_matches() {
    let mut s = settings();
    s.max_distance = 1.0;
    s.show_counts = true;
    assert_eq!(rows(&s, &["aa", "bb", "ab"]), v(&["      2 aa", "      1 bb"]));
}

#[test]
fn mask_digits() {
    let mut s = settings();
    s.max_distance = 0.0;
    s.show_counts = true;
    s.mask_digits = true;
    assert_eq!(rows(&s, &["pid 123 exited at 10:42:07", "pid 98765 exited at 09:01:59", "pid exited"]),
               v(&["      2 pid 123 exited at 10:42:07", "      1 pid exited"]));
}

#[test]
fn token_sort_ignores_word_order() {
    let mut s = settings();
    s.method = Method::TokenSort;
    s.max_distance = 0.0;
    s.show_counts = true;
    assert_eq!(rows(&s, &["connection reset by peer", "peer by reset connection", "connection refused"]),
               v(&["      1 connection refused", "      2 connection reset by peer"]));
    s.ignore_non_alnum = true;
    assert_eq!(rows(&s, &["user=bob,ip=10.0.0.1", "ip=10.0.0.1 user=bob"]), v(&["      2 user=bob,ip=10.0.0.1"]));
}

#[test]
fn token_sort_splits_on_c_whitespace_only() {
    // \x0b (vertical tab) is C whitespace; U+00A0 (bytes C2 A0) is not
    let mut s = settings();
    s.method = Method::TokenSort;
    s.max_distance = 0.0;
    s.show_counts = true;
    assert_eq!(rows(&s, &["b\x0ba", "a b"]).len(), 1);
    assert_eq!(rows(&s, &["b\u{a0}a", "a\u{a0}b"]).len(), 2);
}

#[test]
fn cosine_matches_reordered_fields() {
    let lines = ["user=alice action=login status=ok", "status=ok action=login user=alice", "disk full on /dev/sda1"];
    let mut s = settings();
    s.max_distance = 0.3;
    s.method = Method::NormalizedLevenshtein;
    assert_eq!(rows(&s, &lines).len(), 3);
    s.method = Method::Cosine;
    assert_eq!(rows(&s, &lines).len(), 2);
}

#[test]
fn rep_longest() {
    let mut s = settings();
    s.show_counts = true;
    s.rep = Rep::Longest;
    assert_eq!(rows(&s, &["abc", "abcdef", "abcd"]), v(&["      3 abcdef"]));
}

#[test]
fn scores() {
    let mut s = settings();
    s.max_distance = 2.0;
    s.show_scores = true;
    assert_eq!(rows(&s, &["hello", "hallo", "help!"]), v(&["hello\thallo (1)\thelp! (2)"]));
    let mut n = settings();
    n.method = Method::NormalizedLevenshtein;
    n.max_distance = 0.5;
    n.show_scores = true;
    assert_eq!(rows(&n, &["abcd", "abce"]), v(&["abcd\tabce (0.250)"]));
    n.max_distance = 0.1; // 1/16 = 0.0625: ties round to even, like C++
    assert_eq!(rows(&n, &["abcdefghijklmnop", "abcdefghijklmnoX"]), v(&["abcdefghijklmnop\tabcdefghijklmnoX (0.062)"]));
}

#[test]
fn best_joins_the_closest_group() {
    let mut s = settings();
    s.max_distance = 2.0;
    s.show_counts = true;
    let lines = ["aaaaaa", "aaabbb", "aaaabb"];
    assert_eq!(rows(&s, &lines), v(&["      2 aaaaaa", "      1 aaabbb"]));
    s.best = true;
    assert_eq!(rows(&s, &lines), v(&["      1 aaaaaa", "      2 aaabbb"]));
}

#[test]
fn negative_threshold_matches_nothing_not_even_repeats() {
    let mut s = settings();
    s.max_distance = -1.0;
    s.show_counts = true;
    assert_eq!(rows(&s, &["x", "x"]), v(&["      1 x", "      1 x"]));
}

#[test]
fn equal_representatives_keep_creation_order() {
    let mut s = settings();
    s.max_distance = 2.0;
    s.best = true;
    s.rep = Rep::Longest;
    s.show_counts = true;
    s.show_all = true;
    let block = ["bba", "baab", "a", "aab", "aaaaa", "baab"];
    assert_eq!(rows(&s, &block), v(&["      1 aaaaa", "      3 baab\tbba\ta", "      2 baab\taab"]));
    let mut lines = Vec::new();
    let mut prefixes = Vec::new();
    for b in 0..40u8 {
        let p = String::from_utf8(vec![b'0' + b; 4]).unwrap();
        for line in block {
            lines.push(format!("{p}{line}"));
        }
        prefixes.push(p);
    }
    let lines: Vec<&str> = lines.iter().map(String::as_str).collect();
    let out = rows(&s, &lines);
    assert_eq!(out.len(), 120);
    for p in prefixes {
        let at = |start: String| out.iter().position(|r| r.starts_with(&start)).unwrap();
        assert!(at(format!("      3 {p}baab\t")) < at(format!("      2 {p}baab\t")), "prefix {p}");
    }
}

/// xorshift64*, as in similarity_tests.rs.
struct Rng(u64);
impl Rng {
    fn below(&mut self, n: usize) -> usize {
        self.0 ^= self.0 >> 12;
        self.0 ^= self.0 << 25;
        self.0 ^= self.0 >> 27;
        (self.0.wrapping_mul(0x2545_F491_4F6C_DD1D) % n as u64) as usize
    }
}

fn template_lines(rng: &mut Rng, templates: &[&str], count: usize, edit_one_in: usize) -> Vec<Vec<u8>> {
    (0..count)
        .map(|_| {
            let mut s: Vec<u8> = templates[rng.below(templates.len())]
                .bytes()
                .map(|c| if c == b'#' { b'0' + rng.below(10) as u8 } else { c })
                .collect();
            if rng.below(edit_one_in) == 0 && !s.is_empty() {
                let at = rng.below(s.len());
                s[at] = b'a' + rng.below(26) as u8;
            }
            s
        })
        .collect()
}

/// Serial add() (threads 0) or add_batch with random chunk sizes, empty included.
fn run_matcher(s: &Settings, lines: &[Vec<u8>], threads: usize, rng: &mut Rng) -> String {
    let mut s = s.clone();
    s.threads = threads.max(1);
    let mut m = Matcher::new(s);
    if threads == 0 {
        for line in lines {
            m.add(line.clone());
        }
    } else {
        let mut pos = 0;
        while pos < lines.len() {
            let n = (lines.len() - pos).min(rng.below(40));
            m.add_batch(lines[pos..pos + n].to_vec());
            pos += n;
        }
    }
    render(&m)
}

#[test]
fn add_batch_with_threads_equals_serial_add() {
    let mut rng = Rng(424242);
    let lines = template_lines(&mut rng, &["error at line ##", "warning: x## not found", "GET /a/## 200", "ok"], 600, 5);
    // (method, d, case-insensitive, best, mask digits, scores)
    let cases = [(Method::Levenshtein, 3.0, false, false, false, false), (Method::Levenshtein, 1.0, true, false, false, false),
                 (Method::NormalizedLevenshtein, 0.125, false, false, false, false),
                 (Method::NormalizedLevenshtein, 0.3, true, false, false, false),
                 (Method::Levenshtein, 3.0, false, true, false, true), (Method::NormalizedLevenshtein, 0.3, true, true, false, false),
                 (Method::Levenshtein, 2.0, false, false, true, false), (Method::TokenSort, 0.2, false, false, false, true),
                 (Method::Cosine, 0.2, false, false, false, true), (Method::Cosine, 0.3, true, true, true, false)];
    for (method, d, ci, best, mask, scores) in cases {
        let mut s = settings();
        s.method = method;
        s.max_distance = d;
        s.case_insensitive = ci;
        s.best = best;
        s.mask_digits = mask;
        s.show_scores = scores;
        s.show_all = true;
        s.show_counts = true;
        let serial = run_matcher(&s, &lines, 0, &mut rng);
        for threads in [1, 2, 3, 8] {
            assert_eq!(run_matcher(&s, &lines, threads, &mut rng), serial, "{method:?} d={d} best={best} threads={threads}");
        }
    }
}

#[test]
fn counts_only_runs_equal_show_all_runs() {
    let mut rng = Rng(777);
    let mut lines = template_lines(&mut rng, &["GET /x/## 200", "error ## at line ##", "ok", "warning: v## is deprecated"], 500, 1000);
    for line in lines.iter_mut() {
        if rng.below(4) == 0 {
            line.extend(std::iter::repeat_n(b'z', rng.below(6)));
        }
    }
    for method in [Method::Levenshtein, Method::NormalizedLevenshtein, Method::Cosine] {
        for best in [false, true] {
            for rep in [Rep::First, Rep::Longest] {
                let mut counted = settings();
                counted.method = method;
                counted.max_distance = if method == Method::Levenshtein { 2.0 } else { 0.2 };
                counted.best = best;
                counted.rep = rep;
                counted.show_counts = true;
                let mut listed = counted.clone();
                listed.show_all = true;
                let cut: String = run_matcher(&listed, &lines, 3, &mut rng)
                    .lines()
                    .map(|r| format!("{}\n", r.split('\t').next().unwrap()))
                    .collect();
                assert_eq!(run_matcher(&counted, &lines, 3, &mut rng), cut, "{method:?} best={best} rep={rep:?}");
                assert_eq!(run_matcher(&counted, &lines, 0, &mut rng), cut, "{method:?} best={best} rep={rep:?} serial");
            }
        }
    }
}
