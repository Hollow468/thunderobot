use anyhow::{bail, Result};
use clap::Subcommand;

use crate::sysfs;

#[derive(Subcommand)]
pub enum PowerAction {
    /// Show current performance mode
    Status,
    /// Set performance mode
    Set {
        /// Mode: 0=High Performance, 1=Gaming, 2=Office/Audio
        #[arg(value_parser = clap::value_parser!(u8).range(0..=2))]
        mode: u8,
    },
}

fn mode_description(mode: &str) -> &'static str {
    match mode {
        "0" => "High Performance (高性能)",
        "1" => "Gaming (游戏模式)",
        "2" => "Office / Audio (办公模式)",
        _ => "unknown",
    }
}

pub fn run(action: PowerAction) -> Result<()> {
    match action {
        PowerAction::Status => {
            if !sysfs::is_available() {
                bail!("thunderobot module not loaded");
            }
            let mode = sysfs::read_attr("power/mode")?;
            println!("Power mode: {} ({})", mode, mode_description(&mode));
            Ok(())
        }
        PowerAction::Set { mode } => {
            if !sysfs::is_available() {
                bail!("thunderobot module not loaded");
            }
            sysfs::write_attr("power/mode", &mode.to_string())?;
            println!("Power mode switched to {} ({})", mode, mode_description(&mode.to_string()));
            Ok(())
        }
    }
}
