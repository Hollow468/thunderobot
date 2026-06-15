use anyhow::{bail, Result};
use clap::Subcommand;

use crate::sysfs;

#[derive(Subcommand)]
pub enum LedAction {
    /// Show current LED status
    Status,
    /// Set LED mode
    Mode {
        /// Mode: 0=off, 1=static, 3=breathing, 6=cycle, 7=ambient
        #[arg(value_parser = clap::value_parser!(u8).range(0..=7))]
        mode: u8,
    },
    /// Set brightness
    Brightness {
        /// Brightness level (0-15)
        #[arg(value_parser = clap::value_parser!(u8).range(0..=15))]
        level: u8,
    },
    /// Set LED color
    Color {
        /// Color in RRGGBB hex format
        color: String,
    },
    /// Set LED zone
    Zone {
        /// Zone: 0=all, 3/4/5=keyboard, 6=all keyboard, 7=trunk, 8=logo
        #[arg(value_parser = parse_zone)]
        zone: u8,
    },
    /// Apply current settings
    Apply,
}

fn parse_zone(s: &str) -> Result<u8, String> {
    let val: u8 = s.parse().map_err(|_| "invalid number")?;
    if val == 0 || (val >= 3 && val <= 8) {
        Ok(val)
    } else {
        Err("zone must be 0 or 3-8".to_string())
    }
}

fn zone_name(zone: &str) -> &'static str {
    match zone {
        "0" => "ALL (全部)",
        "3" => "LED3 (键盘灯区3)",
        "4" => "LED2 (键盘灯区2)",
        "5" => "LED1 (键盘灯区1)",
        "6" => "KB_ALL (全部键盘)",
        "7" => "TRUNK (尾灯)",
        "8" => "LOGO (Logo)",
        _ => "unknown",
    }
}

fn mode_name(mode: &str) -> &'static str {
    match mode {
        "0" => "off (关闭)",
        "1" => "static (常亮)",
        "3" => "breathing (呼吸)",
        "6" => "cycle (彩虹循环)",
        "7" => "ambient (氛围灯)",
        _ => "unknown",
    }
}

fn validate_color(s: &str) -> Result<u32> {
    if s.len() != 6 {
        bail!("color must be 6 hex digits (RRGGBB)");
    }
    let val = u32::from_str_radix(s, 16)
        .map_err(|_| anyhow::anyhow!("invalid hex color"))?;
    if val > 0xFFFFFF {
        bail!("color value out of range");
    }
    Ok(val)
}

pub fn run(action: LedAction) -> Result<()> {
    if !sysfs::is_available() {
        bail!("thunderobot module not loaded");
    }

    match action {
        LedAction::Status => {
            let status = sysfs::read_attr("led/status")?;
            println!("LED status: {}", status);

            let zone = sysfs::read_attr("led/zone")?;
            println!("Zone: {} ({})", zone, zone_name(&zone));

            let mode = sysfs::read_attr("led/mode")?;
            println!("Mode: {} ({})", mode, mode_name(&mode));

            let brightness = sysfs::read_attr("led/brightness")?;
            println!("Brightness: {}", brightness);

            let color = sysfs::read_attr("led/color")?;
            println!("Color: #{}", color);

            Ok(())
        }
        LedAction::Mode { mode } => {
            sysfs::write_attr("led/mode", &mode.to_string())?;
            println!("LED mode set to {} ({})", mode, mode_name(&mode.to_string()));
            Ok(())
        }
        LedAction::Brightness { level } => {
            sysfs::write_attr("led/brightness", &level.to_string())?;
            println!("Brightness set to {}", level);
            Ok(())
        }
        LedAction::Color { color } => {
            validate_color(&color)?;
            sysfs::write_attr("led/color", &color)?;
            println!("Color set to #{}", color);
            Ok(())
        }
        LedAction::Zone { zone } => {
            sysfs::write_attr("led/zone", &zone.to_string())?;
            println!("Zone set to {} ({})", zone, zone_name(&zone.to_string()));
            Ok(())
        }
        LedAction::Apply => {
            sysfs::write_attr("led/apply", "1")?;
            println!("Settings applied");
            Ok(())
        }
    }
}
