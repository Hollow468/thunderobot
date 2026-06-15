use anyhow::{bail, Result};
use clap::Subcommand;

use crate::sysfs;

#[derive(Subcommand)]
pub enum GpuAction {
    /// Show current GPU mode
    Status,
    /// Set GPU mode
    Set {
        /// Mode: 1=hybrid, 2=discrete, 3=integrated
        #[arg(value_parser = clap::value_parser!(u8).range(1..=3))]
        mode: u8,
    },
}

fn mode_description(mode: &str) -> &'static str {
    match mode {
        "1" => "hybrid (混合模式)",
        "2" => "discrete GPU (独显模式)",
        "3" => "integrated GPU (核显模式)",
        _ => "unknown",
    }
}

pub fn run(action: GpuAction) -> Result<()> {
    match action {
        GpuAction::Status => {
            println!("GPU hardware:");
            let _ = std::process::Command::new("lspci")
                .args(["-k"])
                .status();
            Ok(())
        }
        GpuAction::Set { mode } => {
            if !sysfs::is_available() {
                bail!("thunderobot module not loaded");
            }
            sysfs::write_attr("gpu/mode", &mode.to_string())?;
            println!("GPU mode switched to {}", mode_description(&mode.to_string()));
            Ok(())
        }
    }
}
