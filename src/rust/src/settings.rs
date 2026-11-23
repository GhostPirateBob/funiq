//! Everything that shapes matching and output (C++: Settings.h).

/// How two lines are compared.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Method {
    /// Edit distance.
    Levenshtein,
    /// Edits divided by the longer length (0.0-1.0).
    NormalizedLevenshtein,
    /// Normalized Levenshtein on the words sorted alphabetically.
    TokenSort,
    /// 1 - cosine similarity of character-trigram counts.
    Cosine,
}

/// Which member a group is printed as.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Rep {
    /// The first line seen (the key matching compares against).
    First,
    /// The first strictly longest member.
    Longest,
}

/// The defaults are those of a plain `funiq` run.
#[derive(Clone, Debug)]
pub struct Settings {
    pub method: Method,
    /// The threshold, in the method's units; f32 like the C++ float.
    pub max_distance: f32,
    pub case_insensitive: bool,
    pub ignore_non_alnum: bool,
    pub mask_digits: bool,
    pub best: bool,
    pub threads: usize,
    pub show_counts: bool,
    pub show_all: bool,
    pub show_scores: bool,
    pub rep: Rep,
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            method: Method::Levenshtein,
            max_distance: 3.0,
            case_insensitive: false,
            ignore_non_alnum: false,
            mask_digits: false,
            best: false,
            threads: 1,
            show_counts: false,
            show_all: false,
            show_scores: false,
            rep: Rep::First,
        }
    }
}
