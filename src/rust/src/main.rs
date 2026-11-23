//! funiq-rs: the Rust port of funiq (fuzzy uniq), for benchmarking against
//! the C++ original. Same algorithm, byte-identical output.

mod cli;
mod matcher;
mod settings;
mod similarity;

use std::process::ExitCode;

use cli::Command;

const VERSION: &str = "1.2.0";

/// argv[0] without its directory, like the C++ binary prints it.
fn program_name() -> String {
    let arg0 = std::env::args_os().next().unwrap_or_default();
    let arg0 = arg0.to_string_lossy();
    arg0.rsplit(['/', '\\']).next().unwrap_or("funiq-rs").to_string()
}

fn main() -> ExitCode {
    match cli::parse_args(std::env::args_os().skip(1).collect()) {
        Ok(Command::Version) => {
            println!("{} {VERSION}", program_name());
            ExitCode::SUCCESS
        }
        Ok(Command::Help) => {
            println!("Usage: funiq-rs [options] [filename]  (the options of funiq; see funiq -h)");
            ExitCode::SUCCESS
        }
        Ok(Command::Run { .. }) => ExitCode::SUCCESS,
        Err(message) => {
            eprintln!("{}: {message}", program_name());
            ExitCode::FAILURE
        }
    }
}
