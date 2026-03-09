#include <algorithm>
#include <functional>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include "tclap/CmdLine.h"
#include "funiq/Settings.h"
#include "funiq/Matcher.h"

void parseCommandLine(int argc, char** argv, std::string& filename, Settings& settings) {

	// Strip directory path from argv[0] so --help and --version display
	// "funiq" instead of "bin/funiq" or a full path.
	std::string progName = argv[0];
	std::string::size_type pos = progName.find_last_of("/\\");
	if(pos != std::string::npos)
		progName = progName.substr(pos + 1);
	argv[0] = const_cast<char*>(progName.c_str());

	TCLAP::CmdLine cmd(
		"funiq - Fuzzy Unique Filtering\n\n"
		"Examples:\n\n"
		"  Deduplicate a file with default settings (edit distance <= 3):\n"
		"    funiq names.txt\n\n"
		"  Pipe from another command:\n"
		"    cat names.txt | funiq\n\n"
		"  Ignore case and non-alphanumeric characters:\n"
		"    funiq -iI names.txt\n\n"
		"  Increase the edit distance threshold:\n"
		"    funiq -iI -d 5 names.txt\n\n"
		"  Use normalized Levenshtein (0.0-1.0 scale) for length-independent matching:\n"
		"    funiq -iI -m normalized-levenshtein -d 0.125 names.txt\n\n"
		"  Show duplicate counts (like uniq -c):\n"
		"    funiq -iI -c names.txt\n\n"
		"  Show all duplicates grouped with their match:\n"
		"    funiq -iI -a names.txt\n\n"
		"  Combine with sort and other tools:\n"
		"    grep -i error log.txt | funiq -iI -d 5 | sort",
		' ', "0.4.0");

	TCLAP::UnlabeledValueArg<std::string> filenameArg (
		"filename",
		"File to read. If omitted will read from stdin.",
		false, "", "filename");
	TCLAP::ValueArg<float> distanceArg(
		"d","distance",
		"Maximum distance threshold between two strings to be considered duplicates.\n"
		"For the default Levenshtein comparison method, it is the maximum edit distance "
		"allowed for two strings to be considered duplicates.\n"
		"For the Normalized Levenshtein comparison method, it is a number between 0.0 and 1.0 "
		"representing 0% and 100% similarity respectively.",
		false, 3, "number");
	TCLAP::SwitchArg caseSwitch(
		"i","case-insensitive",
		"When active, case differences do not contribute to distance between strings.");
	TCLAP::SwitchArg showAllSwitch(
		"a","show-all",
		"Will show all found duplicates");
	TCLAP::SwitchArg showTotalsSwitch(
		"c","show-counts",
		"Precede each output line with the count of the number of times the line occurred "
		"in the input, followed by a single space.");
	TCLAP::SwitchArg ignoreNonAlphaNumericSwitch(
		"I","ignore-non-alpha-numeric",
		"When active, non-alphanumeric characters do not contribute to edit distance.");

	std::vector<std::string> allowedComparisonMethods;
	allowedComparisonMethods.push_back("levenshtein");
	allowedComparisonMethods.push_back("normalized-levenshtein");
	TCLAP::ValuesConstraint<std::string> comparisonMethodsConstraint(allowedComparisonMethods);

	TCLAP::ValueArg<std::string> comparisonMethodArg(
			"m","method",
			"The method used to compare similarity of strings. Defaults to 'levenshtein'",
			false, "levenshtein", &comparisonMethodsConstraint);

	cmd.add(filenameArg);
	cmd.add(comparisonMethodArg);
	cmd.add(distanceArg);
	cmd.add(caseSwitch);
	cmd.add(showAllSwitch);
	cmd.add(showTotalsSwitch);
	cmd.add(ignoreNonAlphaNumericSwitch);
	cmd.parse(argc, argv);

	settings.maxDistance = distanceArg.getValue();
	settings.caseInsensitive = caseSwitch.getValue();
	settings.showAllMatches	= showAllSwitch.getValue();
	settings.showTotals = showTotalsSwitch.getValue();
	settings.ignoreNonAlphaNumeric = ignoreNonAlphaNumericSwitch.getValue();

	std::string comparisonMethod = comparisonMethodArg.getValue();
	if(comparisonMethod == "levenshtein") settings.comparisonMethod = Levenshtein;
	if(comparisonMethod == "normalized-levenshtein") settings.comparisonMethod = NormalizedLevenshtein;

	filename = filenameArg.getValue();
}

// Read from file if a filename is provided, otherwise read from stdin.
// Returns a file stream if applicable, leaving the caller to manage its lifetime.
std::ifstream getFileInput(const std::string& filename) {
	return std::ifstream(filename.c_str());
}

int main(int argc, char* argv[]) {

	// Exit quietly on broken pipe (e.g. funiq file.txt | head) instead
	// of printing an error or crashing.
#ifndef _WIN32
	std::signal(SIGPIPE, SIG_DFL);
#endif

	// Disable C/C++ I/O synchronization for faster stdin/stdout throughput
	// when reading large inputs or piping output.
	std::ios_base::sync_with_stdio(false);
	std::cin.tie(nullptr);

	try {

		std::string filename;
		Settings settings;
		parseCommandLine(argc, argv, filename, settings);

		Matcher matcher(settings);
		// Use file stream if filename provided, otherwise read from stdin
		std::ifstream fileStream;
		if(!filename.empty()) {
			fileStream = getFileInput(filename);
		}
		std::istream& inputStream = filename.empty() ? std::cin : fileStream;
		for (std::string line; getline(inputStream, line); ) {
			matcher.add(line);
		}

		matcher.show(&std::cout);

	} catch (TCLAP::ArgException &e) {
		std::cerr << "An error occurred: ";
		std::cerr << e.error() << " for arg " << e.argId() << std::endl;
		return 1;
	}

	return 0;
}
