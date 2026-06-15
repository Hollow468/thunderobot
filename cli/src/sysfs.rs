use anyhow::{Context, Result};
use std::fs;
use std::path::Path;

const SYSFS_BASE: &str = "/sys/kernel/thunderobot";

/// Read a sysfs attribute as string
pub fn read_attr(subpath: &str) -> Result<String> {
    let path = format!("{}/{}", SYSFS_BASE, subpath);
    fs::read_to_string(&path)
        .with_context(|| format!("failed to read {}", path))
        .map(|s| s.trim().to_string())
}

/// Write a value to a sysfs attribute
pub fn write_attr(subpath: &str, value: &str) -> Result<()> {
    let path = format!("{}/{}", SYSFS_BASE, subpath);
    fs::write(&path, value)
        .with_context(|| format!("failed to write {} to {}", value, path))
}

/// Check if the thunderobot sysfs directory exists
pub fn is_available() -> bool {
    Path::new(SYSFS_BASE).exists()
}
