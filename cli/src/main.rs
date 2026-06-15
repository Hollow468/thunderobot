mod gpu;
mod led;
mod sysfs;

use anyhow::Result;
use clap::{Parser, Subcommand};

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
}

fn main() -> Result<()> {
    let cli = Cli::parse();

    match cli.command {
        Commands::Gpu { action } => gpu::run(action),
        Commands::Led { action } => led::run(action),
    }
}
