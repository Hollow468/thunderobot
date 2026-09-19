use anyhow::{bail, Result};
use clap::Subcommand;

use crate::sysfs;

#[derive(Subcommand)]
pub enum FanAction {
    /// Show fan speeds, temperatures, and status
    Status,
    /// Set fan control mode (0=auto/EC, 1=manual)
    Mode {
        /// Mode: 0=auto, 1=manual
        #[arg(value_parser = clap::value_parser!(u8).range(0..=1))]
        mode: u8,
    },
    /// Set fan speed duty percentage (0-100% or 255 for auto)
    Set {
        /// Fan speed for all fans (0-100% or 255 for auto)
        #[arg(value_parser = parse_speed_arg)]
        speed: Option<u8>,
        /// CPU fan speed (0-100% or 255 for auto)
        #[arg(long, value_parser = parse_speed_arg)]
        cpu: Option<u8>,
        /// GPU fan speed (0-100% or 255 for auto)
        #[arg(long, value_parser = parse_speed_arg)]
        gpu: Option<u8>,
        /// SYS fan speed (0-100% or 255 for auto)
        #[arg(long, value_parser = parse_speed_arg)]
        sys: Option<u8>,
    },
    /// Restore EC automatic fan control
    Auto,
    /// Set fan to 100% maximum cooling (狂暴全速)
    Boost,
    /// Show hardware preset temperature curves
    Curve {
        #[command(subcommand)]
        action: Option<CurveAction>,
    },
}

#[derive(Subcommand)]
pub enum CurveAction {
    /// Show hardware preset temperature curves (8-point table)
    Show,
}

fn parse_speed_arg(s: &str) -> Result<u8, String> {
    if s.eq_ignore_ascii_case("auto") {
        return Ok(255);
    }
    let val: u8 = s.parse().map_err(|_| "invalid number".to_string())?;
    if val <= 100 || val == 255 {
        Ok(val)
    } else {
        Err("speed must be between 0-100% (or 255 for auto)".to_string())
    }
}

fn profile_name(id: u32) -> &'static str {
    match id {
        1 => "NLXB (2-Fan)",
        2 => "NLZD (2-Fan)",
        3 => "NLZE (2-Fan)",
        4 => "NLY (3-Fan)",
        _ => "Standard",
    }
}

fn mode_name(mode: u8) -> &'static str {
    match mode {
        0 => "Auto (EC 自动托管)",
        1 => "Manual (手动模式)",
        _ => "Unknown",
    }
}

pub fn run(action: FanAction) -> Result<()> {
    if !sysfs::is_available() {
        bail!("thunderobot module not loaded");
    }

    match action {
        FanAction::Status => {
            let mode_str = sysfs::read_attr("fan/mode").unwrap_or_else(|_| "0".to_string());
            let mode_val: u8 = mode_str.parse().unwrap_or(0);
            let fans_count: usize = sysfs::read_attr("fan/fans_count")
                .and_then(|s| s.parse().map_err(Into::into))
                .unwrap_or(2);
            let profile_val: u32 = sysfs::read_attr("fan/profile")
                .and_then(|s| s.parse().map_err(Into::into))
                .unwrap_or(0);

            let cpu_temp = sysfs::read_attr("fan/cpu_temp").unwrap_or_else(|_| "-".to_string());
            let gpu_temp = sysfs::read_attr("fan/gpu_temp").unwrap_or_else(|_| "-".to_string());
            let cpu_rpm = sysfs::read_attr("fan/cpu_rpm").unwrap_or_else(|_| "0".to_string());
            let gpu_rpm = sysfs::read_attr("fan/gpu_rpm").unwrap_or_else(|_| "0".to_string());
            let cpu_duty = sysfs::read_attr("fan/cpu_speed").unwrap_or_else(|_| "0".to_string());
            let gpu_duty = sysfs::read_attr("fan/gpu_speed").unwrap_or_else(|_| "0".to_string());

            println!("Fan Control Status:");
            println!("  Mode:         {} ({})", mode_val, mode_name(mode_val));
            println!("  Fans Count:   {}", fans_count);
            println!("  Profile:      {} ({})", profile_val, profile_name(profile_val));
            println!();
            println!("  CPU Fan:      {} RPM | Temp: {} °C | Target Duty: {}%", cpu_rpm, cpu_temp, cpu_duty);
            println!("  GPU Fan:      {} RPM | Temp: {} °C | Target Duty: {}%", gpu_rpm, gpu_temp, gpu_duty);

            if fans_count >= 3 {
                let sys_temp = sysfs::read_attr("fan/sys_temp").unwrap_or_else(|_| "-".to_string());
                let sys_rpm = sysfs::read_attr("fan/sys_rpm").unwrap_or_else(|_| "0".to_string());
                let sys_duty = sysfs::read_attr("fan/sys_speed").unwrap_or_else(|_| "0".to_string());
                println!("  SYS Fan:      {} RPM | Temp: {} °C | Target Duty: {}%", sys_rpm, sys_temp, sys_duty);
            }

            Ok(())
        }
        FanAction::Mode { mode } => {
            sysfs::write_attr("fan/mode", &mode.to_string())?;
            println!("Fan mode set to {} ({})", mode, mode_name(mode));
            Ok(())
        }
        FanAction::Set { speed, cpu, gpu, sys } => {
            if speed.is_none() && cpu.is_none() && gpu.is_none() && sys.is_none() {
                bail!("specify a speed (0-100 or auto), or use --cpu/--gpu/--sys");
            }

            if let Some(s) = speed {
                sysfs::write_attr("fan/speed", &s.to_string())?;
                if s == 255 {
                    println!("All fans restored to Auto (EC control)");
                } else {
                    println!("All fans set to {}%", s);
                }
            } else {
                if let Some(c) = cpu {
                    sysfs::write_attr("fan/cpu_speed", &c.to_string())?;
                    println!("CPU fan set to {}%", if c == 255 { "Auto".to_string() } else { format!("{}%", c) });
                }
                if let Some(g) = gpu {
                    sysfs::write_attr("fan/gpu_speed", &g.to_string())?;
                    println!("GPU fan set to {}%", if g == 255 { "Auto".to_string() } else { format!("{}%", g) });
                }
                if let Some(s) = sys {
                    sysfs::write_attr("fan/sys_speed", &s.to_string())?;
                    println!("SYS fan set to {}%", if s == 255 { "Auto".to_string() } else { format!("{}%", s) });
                }
            }
            Ok(())
        }
        FanAction::Auto => {
            sysfs::write_attr("fan/mode", "0")?;
            println!("Fan control mode restored to Auto (EC controlled)");
            Ok(())
        }
        FanAction::Boost => {
            sysfs::write_attr("fan/speed", "100")?;
            println!("Fan boost enabled: all fans set to 100% full speed");
            Ok(())
        }
        FanAction::Curve { action: _ } => {
            print_preset_curves();
            Ok(())
        }
    }
}

fn print_preset_curves() {
    println!("Thunderobot Hardware Fan Curve Presets (8-Point Temperature Interpolation):");
    println!("-------------------------------------------------------------------------");
    println!("Temp (°C):       30   40   50   60   70   80   90   100");
    println!("-------------------------------------------------------------------------");
    println!("NLZD/NLZE/NLXB:");
    println!("  CPU Fan (%):    0    0    0    0   40   50   60    70");
    println!("  GPU Fan (%):    0    0    0    0   30   40   50    70");
    println!("-------------------------------------------------------------------------");
    println!("NLY (3-Fan):");
    println!("  CPU Fan (%):   30   30   30   30   50   60   70   100");
    println!("  GPU Fan (%):   30   30   30   30   50   60   70   100");
    println!("  SYS Fan (%):   30   30   30   30   50   60   70   100");
    println!("-------------------------------------------------------------------------");
    println!("Tip: Use 'thunderobot fan set <0-100>' for manual speed, or 'thunderobot fan auto' for EC curve.");
}
