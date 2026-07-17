#!/bin/bash
set -e # Exit immediately if a command exits with a non-zero status

echo "=========================================================="
echo " Starting fwupd Android Build Environment Setup"
echo "=========================================================="

# 1. Install Base Packages
echo "-> Installing base system packages..."
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    python3-pip \
    git \
    gcc \
    gettext \
    ninja-build \
    usb.ids \
    pkg-config \
    gnutls-bin \
    libglib2.0-dev \
    wget \
    unzip

# 2. Install Meson and Jinja2 via pip3
echo "-> Installing Meson and Jinja2..."
# Note: --break-system-packages is required on modern Debian/Ubuntu systems (PEP 668)
# to allow global pip installs outside of a virtual environment.
pip3 install --no-cache-dir --break-system-packages "meson>=1.3.0" "Jinja2"

# 3. Setup Android NDK
ENV_NDK_VERSION_NAME="r27d"
ENV_NDK_ZIP_FILE="android-ndk-${ENV_NDK_VERSION_NAME}-linux.zip"
ENV_NDK_DOWNLOAD_URL="https://dl.google.com/android/repository/${ENV_NDK_ZIP_FILE}"
ENV_NDK_INSTALL_PATH="/opt/android"
ENV_ANDROID_NDK_HOME="${ENV_NDK_INSTALL_PATH}/android-ndk-r27"

if [ -d "${ENV_ANDROID_NDK_HOME}" ]; then
    echo "-> Android NDK already installed at ${ENV_ANDROID_NDK_HOME}. Skipping."
else
    echo "-> Installing Android NDK..."
    sudo mkdir -p "${ENV_NDK_INSTALL_PATH}"
    
    # Run in a subshell to keep the current working directory clean
    (
        cd /tmp
        echo "   Downloading NDK..."
        wget -q "${ENV_NDK_DOWNLOAD_URL}" -O "${ENV_NDK_ZIP_FILE}"
        
        echo "   Extracting NDK..."
        unzip -q "${ENV_NDK_ZIP_FILE}"
        
        echo "   Moving to final location..."
        sudo mv "android-ndk-${ENV_NDK_VERSION_NAME}" "${ENV_ANDROID_NDK_HOME}"
        
        echo "   Cleaning up..."
        rm "${ENV_NDK_ZIP_FILE}"
    )
fi

# 4. Setup Java Runtime (Required by sdkmanager)
echo "-> Installing Java JRE..."
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends openjdk-17-jdk-headless

# 5. Setup Android SDK & AIDL (build-tools 35.0.0)
ENV_ANDROID_SDK_ROOT="/opt/android/sdk"

if [ -f "${ENV_ANDROID_SDK_ROOT}/build-tools/35.0.0/aidl" ]; then
    echo "-> Android SDK Build-Tools 35.0.0 (including AIDL) already installed. Skipping."
else
    echo "-> Installing Android SDK Command Line Tools & Build-Tools..."
    sudo mkdir -p "${ENV_ANDROID_SDK_ROOT}/cmdline-tools"
    
    (
        cd /tmp
        echo "   Downloading Command Line Tools..."
        wget -q "https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip" -O cmdline-tools.zip
        
        echo "   Extracting Command Line Tools..."
        unzip -q cmdline-tools.zip
        
        echo "   Moving to SDK folder..."
        sudo mv cmdline-tools "${ENV_ANDROID_SDK_ROOT}/cmdline-tools/cmdline-tools"
        
        echo "   Cleaning up..."
        rm cmdline-tools.zip
    )

    echo "-> Accepting licenses and installing Build-Tools 35.0.0..."
    # yes is piped to automatically accept all license prompts
    yes | sudo "${ENV_ANDROID_SDK_ROOT}/cmdline-tools/cmdline-tools/bin/sdkmanager" \
        --sdk_root="${ENV_ANDROID_SDK_ROOT}" "build-tools;35.0.0"
fi

# 6. Configure Environment Variables in ~/.bashrc
echo "-> Checking and writing environment variables to ~/.bashrc..."

# Check and append ANDROID_NDK_HOME
if ! grep -q "export ANDROID_NDK_HOME=" ~/.bashrc; then
    echo "   Appending ANDROID_NDK_HOME to ~/.bashrc"
    echo "export ANDROID_NDK_HOME=\"${ENV_ANDROID_NDK_HOME}\"" >> ~/.bashrc
else
    echo "   ANDROID_NDK_HOME already present in ~/.bashrc"
fi

# Check and append ANDROID_SDK_ROOT
if ! grep -q "export ANDROID_SDK_ROOT=" ~/.bashrc; then
    echo "   Appending ANDROID_SDK_ROOT to ~/.bashrc"
    echo "export ANDROID_SDK_ROOT=\"${ENV_ANDROID_SDK_ROOT}\"" >> ~/.bashrc
else
    echo "   ANDROID_SDK_ROOT already present in ~/.bashrc"
fi

# Check and append updated PATH for cmdline-tools and build-tools
if ! grep -q "build-tools/35.0.0" ~/.bashrc; then
    echo "   Appending Android tools to PATH in ~/.bashrc"
    echo "export PATH=\"\${PATH}:${ENV_ANDROID_SDK_ROOT}/cmdline-tools/cmdline-tools/bin:${ENV_ANDROID_SDK_ROOT}/build-tools/35.0.0\"" >> ~/.bashrc
else
    echo "   Android build-tools already present in PATH"
fi

echo "=========================================================="
echo " Setup Completed Successfully!"
echo " Please run:  source ~/.bashrc  to apply the changes."
echo "=========================================================="