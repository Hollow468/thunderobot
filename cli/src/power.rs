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
        mode: Option<u8>,
    },
    /// Save current performance mode to local config
    Save,
    /// Restore saved performance mode from local config
    Restore,
}

fn mode_description(mode: u8) -> &'static str {
    match mode {
        0 => "High Performance (高性能)",
        1 => "Gaming (游戏模式)",
        2 => "Office / Audio (办公模式)",
        _ => "unknown",
    }
}

fn power_conf_path() -> Result<std::path::PathBuf> {
    let path = if let Some(home) = std::env::var_os("HOME") {
        let p = std::path::Path::new(&home).join(".config").join("thunderobot").join("power.conf");
        if let Some(parent) = p.parent() {
            std::fs::create_dir_all(parent)?;
        }
        p
    } else {
        std::path::PathBuf::from("thunderobot-power.conf")
    };
    Ok(path)
}

pub fn run(action: PowerAction) -> Result<()> {
    match action {
        PowerAction::Status => {
            if !sysfs::is_available() {
                bail!("thunderobot module not loaded");
            }
            let mode = sysfs::read_attr("power/mode")?;
            let mode_u8: u8 = mode.parse()?;
            println!("Power mode: {} ({})", mode, mode_description(mode_u8));
            Ok(())
        }
        PowerAction::Set { mode } => {
            let mode = mode.ok_or_else(|| {
                anyhow::anyhow!(
                    "MODE is required; supported values: 0=High Performance, 1=Gaming, 2=Office/Audio"
                )
            })?;
            if !sysfs::is_available() {
                bail!("thunderobot module not loaded");
            }
            sysfs::write_attr("power/mode", &mode.to_string())?;
            println!("Power mode switched to {} ({})", mode, mode_description(mode));
            Ok(())
        }
        PowerAction::Save => {
            if !sysfs::is_available() {
                bail!("thunderobot module not loaded");
            }
            let mode = sysfs::read_attr("power/mode")?;
            let path = power_conf_path()?;
            std::fs::write(&path, mode.trim())?;
            println!("Power mode saved to {}", path.display());
            Ok(())
        }
        PowerAction::Restore => {
            if !sysfs::is_available() {
                bail!("thunderobot module not loaded");
            }
            let path = power_conf_path()?;
            let mode = std::fs::read_to_string(&path)?;
            let mode = mode.trim();
            if mode.is_empty() {
                bail!("saved power mode is empty");
            }
            let mode: u8 = mode.parse()?;
            if mode > 2 {
                bail!("invalid saved power mode: {}", mode);
            }
            sysfs::write_attr("power/mode", &mode.to_string())?;
            println!("Power mode restored to {} ({})", mode, mode_description(mode));
            Ok(())
        }
    }
}
