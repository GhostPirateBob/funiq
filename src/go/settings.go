package main

// Method is how two lines are compared.
type Method int

const (
	Levenshtein           Method = iota // edit distance
	NormalizedLevenshtein               // edits / longer length (0.0-1.0)
	TokenSort                           // normalized, on alphabetically sorted words
	Cosine                              // 1 - cosine similarity of trigram counts
)

// Rep is which member a group is printed as.
type Rep int

const (
	RepFirst   Rep = iota // the first line seen (the key)
	RepLongest            // the first strictly longest member
)

// Settings holds everything that shapes matching and output (C++:
// Settings.h). The zero value is not a plain run; use defaultSettings.
type Settings struct {
	Method          Method
	MaxDistance     float32 // the threshold, f32 like the C++ float
	CaseInsensitive bool
	IgnoreNonAlnum  bool
	MaskDigits      bool
	Best            bool
	Threads         int
	ShowCounts      bool
	ShowAll         bool
	ShowScores      bool
	Rep             Rep
}

// defaultSettings is a plain `funiq` run.
func defaultSettings() Settings {
	return Settings{Method: Levenshtein, MaxDistance: 3, Threads: 1, Rep: RepFirst}
}
