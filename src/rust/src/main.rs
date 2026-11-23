//! funiq-rs: the Rust port of funiq (fuzzy uniq), for benchmarking against
//! the C++ original. Same algorithm, byte-identical output.

mod cli;
mod lines;
mod matcher;
mod settings;
mod similarity;

use std::fs::File;
use std::io::{self, BufWriter, IsTerminal, Read, Write};
use std::path::PathBuf;
use std::process::ExitCode;

use cli::Command;
use lines::LineReader;
use matcher::Matcher;
use settings::Settings;

const VERSION: &str = "1.3.0";
/// Lines per batch, as in the C++: each batch's scan runs on all threads.
const BATCH_SIZE: usize = 256;

/// argv[0] without its directory, like the C++ binary prints it.
fn program_name() -> String {
    let arg0 = std::env::args_os().next().unwrap_or_default();
    let arg0 = arg0.to_string_lossy();
    arg0.rsplit(['/', '\\']).next().unwrap_or("funiq-rs").to_string()
}

/// The C library's wording for the common cases, as the C++ prints them.
fn describe(e: &io::Error) -> String {
    match e.kind() {
        io::ErrorKind::NotFound => "No such file or directory".to_string(),
        io::ErrorKind::PermissionDenied => "Permission denied".to_string(),
        io::ErrorKind::IsADirectory => "Is a directory".to_string(),
        _ => e.to_string(),
    }
}

fn main() -> ExitCode {
    match cli::parse_args(std::env::args_os().skip(1).collect()) {
        Ok(Command::Version) => say(&format!("{} {VERSION}", program_name())),
        Ok(Command::Help) => say("Usage: funiq-rs [options] [filename]  (the options of funiq; see funiq -h)"),
        Ok(Command::Run { settings, file }) => run(settings, file),
        Err(message) => {
            eprintln!("{}: {message}", program_name());
            ExitCode::FAILURE
        }
    }
}

/// One line to stdout. Not println!, which panics when the reader has gone
/// (`funiq-rs --version | true`); exit quietly like the C++ instead.
fn say(line: &str) -> ExitCode {
    let _ = writeln!(io::stdout(), "{line}");
    ExitCode::SUCCESS
}

fn fail(what: &str, e: &io::Error) -> ExitCode {
    eprintln!("{}: {what}: {}", program_name(), describe(e));
    ExitCode::FAILURE
}

fn run(settings: Settings, file: Option<PathBuf>) -> ExitCode {
    let name = file.as_ref().map_or("stdin".to_string(), |p| p.display().to_string());
    let input: Box<dyn Read> = match &file {
        Some(path) => match File::open(path) {
            Ok(f) => Box::new(f),
            Err(e) => return fail(&name, &e),
        },
        None => Box::new(io::stdin().lock()),
    };

    let mut matcher = Matcher::new(settings);
    let mut reader = LineReader::new(input);
    let mut batch = Vec::with_capacity(BATCH_SIZE);
    let mut line = Vec::new();
    loop {
        match reader.next_line(&mut line) {
            Ok(true) => {
                batch.push(std::mem::take(&mut line));
                if batch.len() == BATCH_SIZE {
                    matcher.add_batch(std::mem::replace(&mut batch, Vec::with_capacity(BATCH_SIZE)));
                }
            }
            Ok(false) => break,
            Err(e) => return fail(&name, &e), // e.g. a directory on Linux
        }
    }
    if !batch.is_empty() {
        matcher.add_batch(batch);
    }

    let result = if cfg!(windows) && io::stdout().is_terminal() {
        // A Windows console takes only UTF-8, so non-UTF-8 lines (Latin-1
        // logs) print with replacement characters instead of failing.
        let mut rendered = Vec::new();
        matcher.write_to(&mut rendered).and_then(|_| {
            let mut out = io::stdout().lock();
            out.write_all(String::from_utf8_lossy(&rendered).as_bytes())?;
            out.flush()
        })
    } else {
        let mut out = BufWriter::with_capacity(1 << 20, io::stdout().lock());
        matcher.write_to(&mut out).and_then(|_| out.flush())
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        // `funiq-rs ... | head`: Rust ignores SIGPIPE, so the write fails
        // instead of the process dying; exit as quietly as the C++ does.
        Err(e) if e.kind() == io::ErrorKind::BrokenPipe => ExitCode::SUCCESS,
        Err(e) => fail("stdout", &e),
    }
}
