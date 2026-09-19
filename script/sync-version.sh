#!/usr/bin/env bash
#
# sync-version.sh - Sync project version from Cargo.toml / Cargo.yaml to kernel, dkms, and CLI
#
# Usage:
#   ./script/sync-version.sh          # Read version from Cargo.toml and sync to all other files
#   ./script/sync-version.sh 1.4.0    # Set new version in Cargo.toml and sync to all files
#   ./script/sync-version.sh --check  # Check if all files have consistent version
#

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CARGO_TOML="${REPO_ROOT}/cli/Cargo.toml"
CARGO_YAML="${REPO_ROOT}/cli/Cargo.yaml"
MAIN_RS="${REPO_ROOT}/cli/src/main.rs"
DKMS_CONF="${REPO_ROOT}/kernel/dkms.conf"
README_MD="${REPO_ROOT}/README.md"

# 1. Determine truth source file
if [ -f "${CARGO_YAML}" ]; then
    SOURCE_FILE="${CARGO_YAML}"
elif [ -f "${CARGO_TOML}" ]; then
    SOURCE_FILE="${CARGO_TOML}"
else
    echo "Error: Neither cli/Cargo.yaml nor cli/Cargo.toml found." >&2
    exit 1
fi

# Function to extract version from source file
get_source_version() {
    local ver
    ver=$(grep -E '^[[:space:]]*version[[:space:]]*=[[:space:]]*"[^"]+"' "${SOURCE_FILE}" | head -n 1 | sed -E 's/.*version[[:space:]]*=[[:space:]]*"([^"]+)".*/\1/')
    if [ -z "${ver}" ]; then
        # Check YAML format: version: "1.0.0" or version: 1.0.0
        ver=$(grep -E '^[[:space:]]*version:[[:space:]]*' "${SOURCE_FILE}" | head -n 1 | sed -E 's/^[[:space:]]*version:[[:space:]]*"?([^"[:space:]]+)"?.*/\1/')
    fi
    echo "${ver}"
}

# 2. Parse arguments
NEW_VERSION=""
CHECK_ONLY=0

if [ $# -ge 1 ]; then
    if [ "$1" = "--check" ] || [ "$1" = "-c" ]; then
        CHECK_ONLY=1
    elif [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
        echo "Usage: $0 [NEW_VERSION | --check]"
        echo "  Synchronizes version from cli/Cargo.toml (single source of truth)"
        echo "  to kernel/dkms.conf, cli/src/main.rs, and README.md."
        exit 0
    else
        NEW_VERSION="$1"
    fi
fi

# 3. If new version provided, update source file first
if [ -n "${NEW_VERSION}" ]; then
    echo "Setting new version '${NEW_VERSION}' in $(basename "${SOURCE_FILE}")..."
    if [ "${SOURCE_FILE}" = "${CARGO_TOML}" ]; then
        sed -i -E "s/^(version[[:space:]]*=[[:space:]]*\")[^\"]+(\".*)/\1${NEW_VERSION}\2/" "${CARGO_TOML}"
    else
        sed -i -E "s/^(version:[[:space:]]*\"?)[^\"]+(\"?.*)/\1${NEW_VERSION}\2/" "${CARGO_YAML}"
    fi
fi

TARGET_VERSION="$(get_source_version)"

if [ -z "${TARGET_VERSION}" ]; then
    echo "Error: Could not extract version from ${SOURCE_FILE}" >&2
    exit 1
fi

echo "Source of truth version: ${TARGET_VERSION} (from $(basename "${SOURCE_FILE}"))"

# 4. Check mode
if [ ${CHECK_ONLY} -eq 1 ]; then
    echo "Checking version consistency across codebase..."
    MISMATCH=0

    # Check dkms.conf
    DKMS_VER=$(grep -E '^[[:space:]]*PACKAGE_VERSION[[:space:]]*=' "${DKMS_CONF}" | sed -E 's/.*PACKAGE_VERSION="([^"]+)".*/\1/')
    if [ "${DKMS_VER}" != "${TARGET_VERSION}" ]; then
        echo "  [MISMATCH] kernel/dkms.conf: '${DKMS_VER}' != '${TARGET_VERSION}'"
        MISMATCH=1
    else
        echo "  [OK] kernel/dkms.conf (${DKMS_VER})"
    fi

    # Check main.rs
    MAIN_VER=$(grep -E '#\[command\(version[[:space:]]*=[[:space:]]*"[^"]+"\)' "${MAIN_RS}" | sed -E 's/.*version[[:space:]]*=[[:space:]]*"([^"]+)".*/\1/')
    if [ "${MAIN_VER}" != "${TARGET_VERSION}" ]; then
        echo "  [MISMATCH] cli/src/main.rs: '${MAIN_VER}' != '${TARGET_VERSION}'"
        MISMATCH=1
    else
        echo "  [OK] cli/src/main.rs (${MAIN_VER})"
    fi

    # Check README.md
    if grep -q "thunderobot/${TARGET_VERSION}" "${README_MD}"; then
        echo "  [OK] README.md (${TARGET_VERSION})"
    else
        echo "  [MISMATCH] README.md does not match '${TARGET_VERSION}'"
        MISMATCH=1
    fi

    if [ ${MISMATCH} -eq 0 ]; then
        echo "All files are synchronized with version ${TARGET_VERSION}."
        exit 0
    else
        echo "Version mismatch detected! Run '$0' to synchronize." >&2
        exit 1
    fi
fi

# 5. Sync to kernel/dkms.conf
if [ -f "${DKMS_CONF}" ]; then
    sed -i -E "s/^(PACKAGE_VERSION=\")[^\"]+(\")/\1${TARGET_VERSION}\2/" "${DKMS_CONF}"
    echo "  Updated kernel/dkms.conf -> ${TARGET_VERSION}"
fi

# 6. Sync to cli/src/main.rs
if [ -f "${MAIN_RS}" ]; then
    sed -i -E "s/(#\[command\(version[[:space:]]*=[[:space:]]*\")[^\"]+(\"\))/\1${TARGET_VERSION}\2/" "${MAIN_RS}"
    echo "  Updated cli/src/main.rs -> ${TARGET_VERSION}"
fi

# 7. Sync to README.md
if [ -f "${README_MD}" ]; then
    sed -i -E "s#(dkms (install|remove) thunderobot/)[^ /]+#\1${TARGET_VERSION}#g" "${README_MD}"
    echo "  Updated README.md -> ${TARGET_VERSION}"
fi

# 8. Update cli/Cargo.lock if cargo is installed
if command -v cargo >/dev/null 2>&1 && [ -f "${CARGO_TOML}" ]; then
    echo "  Updating cli/Cargo.lock..."
    (cd "${REPO_ROOT}/cli" && cargo check --quiet >/dev/null 2>&1 || true)
fi

echo "Version synchronization complete: all targets updated to ${TARGET_VERSION}."
