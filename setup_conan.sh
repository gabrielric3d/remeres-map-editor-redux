#!/bin/bash
# ========================================
# RME Linux Dependency Setup Script
# Optimized for Jules AI (Low Resource)
# ========================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Using local build_conan directory (gitignored)
BUILD_DIR="$SCRIPT_DIR/build_conan"
LOG_FILE="$BUILD_DIR/setup_conan.log"

# Ensure build directory exists for the log file
mkdir -p "$BUILD_DIR"

# Function to run command and only show errors
run_quiet() {
    if ! "$@" >> "$LOG_FILE" 2>&1; then
        echo "Error executing: $*"
        echo "Check $LOG_FILE for details."
        exit 1
    fi
}

{
    echo "========================================"
    echo " RME Linux Dependency Setup"
    echo " Started: $(date)"
    echo "========================================"
} > "$LOG_FILE"

# Step 1: Install system dependencies via apt (fast, pre-compiled)
echo "[1/4] Installing system dependencies (Quiet)..."
sudo apt update -y > /dev/null 2>&1
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    python3 \
    python3-pip \
    libwxgtk3.2-dev \
    libgtk-3-dev \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    libboost-all-dev \
    libarchive-dev \
    zlib1g-dev \
    libglm-dev \
    libspdlog-dev \
    libfmt-dev \
    nlohmann-json3-dev \
    libasio-dev > /dev/null 2>&1

echo "  ✅ System packages installed"

# Step 2: Setup Conan profile
echo "[2/4] Setting up Conan profile..."
if ! conan profile show &> /dev/null; then
    run_quiet conan profile detect --force
fi

# Step 3: Install dependencies via Conan (No Building)
echo "[3/4] Installing Conan dependencies (Binaries only)..."
# Using --build=never to ensure Jules only downloads precompiled libraries
# If dependencies are missing, this will fail fast instead of wasting time/RAM
run_quiet conan install "$SCRIPT_DIR" \
    -of "$BUILD_DIR" \
    --build=never \
    -s build_type=Release

# Step 4: Verification
echo "[4/4] Verifying setup..."
if [ -d "$BUILD_DIR" ]; then
    echo "  ✅ Conan environment initialized in $BUILD_DIR"
else
    echo "  ⚠️  Setup may have failed, check $LOG_FILE"
    exit 1
fi

echo "========================================"
echo " SETUP COMPLETE!"
echo " All temporary files are in $BUILD_DIR"
echo " Your git directory is CLEAN ✨"
echo "========================================"
