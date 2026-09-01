mod gpu;
mod led;
mod power;
mod sysfs;

use std::io;
use anyhow::Result;
use clap::{CommandFactory, Parser, Subcommand};
use clap_complete::{generate, Shell};

#[derive(Parser)]
#[command(name = "thunderobot")]
#[command(version = "1.0.0")]
#[command(about = "Thunderobot laptop management tool")]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// GPU mode management
    Gpu {
        #[command(subcommand)]
        action: gpu::GpuAction,
    },
    /// LED control
    Led {
        #[command(subcommand)]
        action: led::LedAction,
    },
    /// Power / performance mode management
    Power {
        #[command(subcommand)]
        action: power::PowerAction,
    },
    /// Generate shell completion scripts
    Completions {
        /// Shell to generate completions for
        #[arg(value_enum)]
        shell: Shell,
    },
}

fn main() -> Result<()> {
    let cli = Cli::parse();

    match cli.command {
        Commands::Gpu { action } => gpu::run(action),
        Commands::Led { action } => led::run(action),
        Commands::Power { action } => power::run(action),
        Commands::Completions { shell } => {
            let mut cmd = Cli::command();
            generate(shell, &mut cmd, "thunderobot", &mut io::stdout());
            Ok(())
        }
    }
}
