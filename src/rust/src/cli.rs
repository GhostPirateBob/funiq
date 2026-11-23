//! Command-line parsing with the C++ funiq's semantics (spec §3, Arguments).

use std::ffi::OsString;
use std::path::PathBuf;

use crate::settings::{Method, Rep, Settings};

/// What the command line asks for.
#[derive(Debug)]
pub enum Command {
    Run { settings: Settings, file: Option<PathBuf> },
    Help,
    Version,
}

/// Parses the arguments after argv[0].
///
/// Before parsing, like the C++ binary: `--name=value` becomes `--name value`,
/// and a bare `-d` not followed by a number becomes `--default`; nothing after
/// `--` is rewritten. Each option may appear once; short switches combine.
pub fn parse_args(args: Vec<OsString>) -> Result<Command, String> {
    let tokens = rewrite(&args);
    let mut seen: Vec<&'static str> = Vec::new();
    let mut flags = Flags::default();
    let mut file: Option<PathBuf> = None;
    let mut literal = false;
    let mut i = 0;
    while i < tokens.len() {
        let raw = &tokens[i];
        let token = raw.to_string_lossy().into_owned();
        i += 1;
        if literal || token == "-" || !token.starts_with('-') {
            if file.is_some() {
                return Err(format!("Couldn't find match for argument {token}"));
            }
            file = Some(PathBuf::from(raw));
            continue;
        }
        if token == "--" {
            literal = true;
            continue;
        }
        let names: Vec<&'static str> = match canonical(&token) {
            Some(name) => vec![name],
            // combined short switches, e.g. -iIc (value options can't combine)
            None if !token.starts_with("--") && token.len() > 2 => token[1..]
                .chars()
                .map(|c| canonical(&format!("-{c}")).filter(|n| !VALUE_OPTIONS.contains(n)))
                .collect::<Option<Vec<_>>>()
                .ok_or_else(|| format!("Couldn't find match for argument {token}"))?,
            None => return Err(format!("Couldn't find match for argument {token}")),
        };
        for name in names {
            if seen.contains(&name) {
                return Err(format!("Argument already set! ({token})"));
            }
            seen.push(name);
            match name {
                "help" => return Ok(Command::Help),
                "version" => return Ok(Command::Version),
                _ if VALUE_OPTIONS.contains(&name) => {
                    let value = tokens
                        .get(i)
                        .ok_or_else(|| format!("Missing a value for this argument! ({token})"))?;
                    i += 1;
                    flags.set_value(name, &value.to_string_lossy())?;
                }
                _ => flags.switches.push(name),
            }
        }
    }
    Ok(Command::Run { settings: flags.into_settings(), file })
}

/// The pre-parse rewrites of the C++ binary.
fn rewrite(args: &[OsString]) -> Vec<OsString> {
    let mut tokens = Vec::with_capacity(args.len());
    let mut literal = false;
    for (i, arg) in args.iter().enumerate() {
        let text = arg.to_string_lossy();
        let eq = text.find('=');
        if !literal && text.starts_with("--") && eq.is_some_and(|eq| eq > 2) {
            let eq = eq.unwrap();
            tokens.push(OsString::from(&text[..eq]));
            tokens.push(OsString::from(&text[eq + 1..]));
        } else if !literal && text == "-d" && !args.get(i + 1).is_some_and(|n| is_c_number(&n.to_string_lossy())) {
            tokens.push(OsString::from("--default"));
        } else {
            literal |= text == "--";
            tokens.push(arg.clone());
        }
    }
    tokens
}

const VALUE_OPTIONS: [&str; 5] = ["distance", "method", "threads", "rep", "color"];

/// The option a token names (`-d`, `--distance` -> "distance"), if any.
fn canonical(token: &str) -> Option<&'static str> {
    Some(match token {
        "-d" | "--distance" => "distance",
        "-m" | "--method" => "method",
        "-j" | "--threads" => "threads",
        "--rep" => "rep",
        "--color" => "color",
        "-i" | "--case-insensitive" => "case-insensitive",
        "-I" | "--ignore-non-alpha-numeric" => "ignore-non-alpha-numeric",
        "-D" | "--mask-digits" => "mask-digits",
        "-c" | "--show-counts" => "show-counts",
        "-a" | "--show-all" => "show-all",
        "--scores" => "scores",
        "--best" => "best",
        "--default" => "default",
        "-h" | "--help" => "help",
        "--version" => "version",
        _ => return None,
    })
}

/// Whether C `strtod` would consume all of `s` (decimal or hex floats, inf,
/// infinity, nan, after optional leading whitespace): decides what a bare
/// `-d` is followed by, exactly as the C++ binary does.
fn is_c_number(s: &str) -> bool {
    let t = s.trim_start_matches([' ', '\t', '\n', '\x0b', '\x0c', '\r']);
    let unsigned = t.strip_prefix(['+', '-']).unwrap_or(t).to_ascii_lowercase();
    if unsigned.is_empty() {
        return false;
    }
    if let Some(hex) = unsigned.strip_prefix("0x") {
        let (mantissa, exponent) = hex.split_once('p').map_or((hex, None), |(m, e)| (m, Some(e)));
        return mantissa.chars().any(|c| c.is_ascii_hexdigit())
            && mantissa.chars().all(|c| c.is_ascii_hexdigit() || c == '.')
            && mantissa.matches('.').count() <= 1
            && exponent.is_none_or(|e| {
                let e = e.strip_prefix(['+', '-']).unwrap_or(e);
                !e.is_empty() && e.chars().all(|c| c.is_ascii_digit())
            });
    }
    if let Some(inner) = unsigned.strip_prefix("nan(").and_then(|r| r.strip_suffix(')')) {
        return inner.chars().all(|c| c.is_ascii_alphanumeric() || c == '_');
    }
    // like strtod, Rust's parser takes "inf", "infinity" and "nan"
    unsigned.parse::<f64>().is_ok()
}

/// Option values as given, resolved into Settings at the end.
#[derive(Default)]
struct Flags {
    distance: Option<f32>,
    method: Option<Method>,
    threads: usize,
    rep: Option<Rep>,
    switches: Vec<&'static str>,
}

impl Flags {
    fn set_value(&mut self, name: &str, value: &str) -> Result<(), String> {
        match name {
            // like istream >> float: correctly rounded straight to f32, and
            // no inf, nan or hex
            "distance" => {
                self.distance = Some(
                    value
                        .parse::<f32>()
                        .ok()
                        .filter(|d| d.is_finite() && !value.to_ascii_lowercase().contains('x'))
                        .ok_or_else(|| format!("Couldn't read argument value from string '{value}' (-d)"))?,
                )
            }
            "method" => {
                self.method = Some(match value {
                    "levenshtein" => Method::Levenshtein,
                    "normalized-levenshtein" => Method::NormalizedLevenshtein,
                    "token-sort" => Method::TokenSort,
                    "cosine" => Method::Cosine,
                    _ => return Err(format!("Value '{value}' does not meet constraint (-m)")),
                })
            }
            "threads" => {
                self.threads = value
                    .parse::<usize>()
                    .map_err(|_| format!("Value '{value}' does not meet constraint: 0 or more (-j)"))?
            }
            "rep" => {
                self.rep = Some(match value {
                    "first" => Rep::First,
                    "longest" => Rep::Longest,
                    _ => return Err(format!("Value '{value}' does not meet constraint: first|longest (--rep)")),
                })
            }
            // accepted for compatibility; the ports print no colour
            "color" if ["auto", "always", "never"].contains(&value) => {}
            "color" => return Err(format!("Value '{value}' does not meet constraint: auto|always|never (--color)")),
            _ => unreachable!("not a value option: {name}"),
        }
        Ok(())
    }

    fn into_settings(self) -> Settings {
        let mut s = Settings::default();
        // the preset first; explicit flags then refine it
        if self.switches.contains(&"default") {
            s.case_insensitive = true;
            s.ignore_non_alnum = true;
            s.method = Method::NormalizedLevenshtein;
        }
        for name in &self.switches {
            match *name {
                "case-insensitive" => s.case_insensitive = true,
                "ignore-non-alpha-numeric" => s.ignore_non_alnum = true,
                "mask-digits" => s.mask_digits = true,
                "show-counts" => s.show_counts = true,
                "show-all" => s.show_all = true,
                "scores" => s.show_scores = true,
                "best" => s.best = true,
                _ => {}
            }
        }
        if let Some(m) = self.method {
            s.method = m;
        }
        s.max_distance = self.distance.unwrap_or(match s.method {
            Method::Levenshtein => 3.0,
            Method::Cosine => 0.05,
            _ => 0.125,
        });
        s.rep = self.rep.unwrap_or(Rep::First);
        s.threads = if self.threads > 0 {
            self.threads
        } else {
            std::thread::available_parallelism().map(|n| n.get()).unwrap_or(1)
        };
        s
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(args: &[&str]) -> Result<Command, String> {
        parse_args(args.iter().map(OsString::from).collect())
    }
    fn run(args: &[&str]) -> Settings {
        match parse(args) {
            Ok(Command::Run { settings, .. }) => settings,
            other => panic!("{args:?}: expected a run, got {other:?}"),
        }
    }
    fn file(args: &[&str]) -> Option<PathBuf> {
        match parse(args) {
            Ok(Command::Run { file, .. }) => file,
            other => panic!("{args:?}: expected a run, got {other:?}"),
        }
    }

    #[test]
    fn bare_d_is_the_preset() {
        let s = run(&["-d"]);
        assert_eq!(s.max_distance, 0.125);
        assert_eq!(s.method, Method::NormalizedLevenshtein);
        assert!(s.case_insensitive && s.ignore_non_alnum);
    }

    #[test]
    fn d_with_a_number_is_a_threshold() {
        assert_eq!(run(&["-d", "0.125", "x"]).max_distance, 0.125f32);
        assert_eq!(run(&["-d", "0.125", "x"]).method, Method::Levenshtein);
    }

    #[test]
    fn bare_d_then_a_filename() {
        assert_eq!(file(&["-d", "log.txt"]), Some(PathBuf::from("log.txt")));
        assert_eq!(run(&["-d", "log.txt"]).method, Method::NormalizedLevenshtein);
    }

    #[test]
    fn default_threshold_depends_on_the_method() {
        assert_eq!(run(&[]).max_distance, 3.0);
        assert_eq!(run(&["-m", "cosine"]).max_distance, 0.05);
        assert_eq!(run(&["-m", "token-sort"]).max_distance, 0.125);
        assert_eq!(run(&["-m", "normalized-levenshtein"]).max_distance, 0.125);
    }

    #[test]
    fn a_value_may_start_with_a_dash() {
        assert_eq!(run(&["-d", "-1"]).max_distance, -1.0);
    }

    #[test]
    fn long_option_equals_value() {
        assert_eq!(run(&["--rep=longest"]).rep, Rep::Longest);
        assert_eq!(run(&["--distance=2.5"]).max_distance, 2.5);
    }

    #[test]
    fn short_switches_combine() {
        let s = run(&["-iIc"]);
        assert!(s.case_insensitive && s.ignore_non_alnum && s.show_counts);
    }

    #[test]
    fn explicit_flags_refine_the_preset() {
        assert_eq!(run(&["--default", "-m", "levenshtein"]).method, Method::Levenshtein);
        assert_eq!(run(&["--default", "-m", "levenshtein"]).max_distance, 3.0);
    }

    #[test]
    fn rejects_bad_values_and_unknown_options() {
        for bad in [&["-d", "inf"][..], &["-d", "nan"], &["-d", "0x10"], &["-x"], &["--nope"],
                    &["-j", "-1"], &["-j", "1.5"], &["--rep", "sideways"], &["-m", "jaro"],
                    &["--color", "sometimes"], &["-iQ"], &["-c", "-c"], &["--=x"], &["-m"]] {
            assert!(parse(bad).is_err(), "{bad:?} should be an error");
        }
    }

    #[test]
    fn after_double_dash_a_leading_dash_is_a_filename() {
        assert_eq!(file(&["--", "-dash.txt"]), Some(PathBuf::from("-dash.txt")));
    }

    #[test]
    fn one_filename_at_most() {
        assert!(parse(&["a.txt", "b.txt"]).is_err());
    }

    #[test]
    fn help_and_version() {
        assert!(matches!(parse(&["-h"]), Ok(Command::Help)));
        assert!(matches!(parse(&["--version"]), Ok(Command::Version)));
    }

    #[test]
    fn output_and_matching_flags() {
        let s = run(&["-D", "-a", "--scores", "--best", "-j", "3", "--color", "never"]);
        assert!(s.mask_digits && s.show_all && s.show_scores && s.best);
        assert_eq!(s.threads, 3);
    }
}
