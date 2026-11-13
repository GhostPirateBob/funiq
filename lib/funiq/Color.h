#ifndef FUNIQ_COLOR_H
#define FUNIQ_COLOR_H

#include <cstring>
#include <string>

// Terminal colour, decided once per stream. Conventions follow chalk /
// yoctocolors and no-color.org, as in time-bucksy's env-manager.sh.
namespace color {

	enum Mode { Auto, Always, Never };

	// Whether to colour one stream. Precedence: --color always/never, then
	// FORCE_COLOR ("0"/"false" turn it off, any other non-empty value on),
	// then NO_COLOR (any non-empty value), then TERM=dumb, then whether the
	// stream is a terminal. Null and empty environment values count as unset.
	inline bool want(Mode mode, const char* forceColor, const char* noColor,
	                 const char* term, bool isTerminal) {
		if(mode == Always) return true;
		if(mode == Never) return false;
		if(forceColor && *forceColor)
			return std::strcmp(forceColor, "0") != 0 && std::strcmp(forceColor, "false") != 0;
		if(noColor && *noColor) return false;
		if(term && std::strcmp(term, "dumb") == 0) return false;
		return isTerminal;
	}

	// An SGR style: its opening code and the code that ends just that
	// attribute (22 bold/dim, 39 foreground) rather than a full reset, so
	// ending an inner style doesn't end an outer one.
	struct Style { const char* open; const char* close; };

	const Style Bold    = { "1", "22" };
	const Style Dim     = { "2", "22" };
	const Style Red     = { "31", "39" };
	const Style Green   = { "32", "39" };
	const Style Yellow  = { "33", "39" };
	const Style Cyan    = { "36", "39" };
	const Style Gray    = { "90", "39" };
	const Style BoldRed = { "1;31", "22;39" };

	// text wrapped in the style, or unchanged when colour is off.
	inline std::string paint(bool on, const Style& style, const std::string& text) {
		if(!on) return text;
		return std::string("\x1b[") + style.open + "m" + text + "\x1b[" + style.close + "m";
	}
}

#endif
