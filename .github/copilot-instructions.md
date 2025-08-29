# fwupd Firmware Update Daemon

Always reference these instructions first and fallback to search or bash commands only when you encounter unexpected information that does not match the info here.

fwupd is a Linux firmware update daemon built with Meson, written in C with GLib, and includes 130+ plugins for various hardware vendors. It provides both a daemon (`fwupd`) and command-line tools (`fwupdmgr`, `fwupdtool`) for firmware management.

## Working Effectively

### CRITICAL: Setup and Build Process
- **CONTAINER REQUIRED**: Use pre-built container `ghcr.io/fwupd/fwupd/fwupd-ubuntu-x86_64:latest` for consistent development environment
- Setup script: `./contrib/ci/fwupd_setup_helpers.py install-dependencies -o ubuntu --yes` (inside container) -- takes 30+ minutes for dependency installation. NEVER CANCEL.
- Build command: `build-fwupd` (in venv) -- takes 45+ minutes. NEVER CANCEL. Set timeout to 60+ minutes.
- Test command: `test-fwupd` (in venv) -- takes 15+ minutes. NEVER CANCEL. Set timeout to 30+ minutes.

### Bootstrap the Development Environment

**Container-based development (REQUIRED for automation)**
```bash
# Use the pre-built fwupd container for consistent Ubuntu environment
# Container: https://github.com/fwupd/fwupd/pkgs/container/fwupd%2Ffwupd-ubuntu-x86_64
docker pull ghcr.io/fwupd/fwupd/fwupd-ubuntu-x86_64:latest

# Run container with source mounted
docker run -it --privileged -v $(pwd):/workspace ghcr.io/fwupd/fwupd/fwupd-ubuntu-x86_64:latest

# Inside container - install dependencies first
cd /workspace
./contrib/ci/fwupd_setup_helpers.py install-dependencies -o ubuntu --yes

# Create virtual environment
python3 -m venv venv --system-site-packages --prompt fwupd
source venv/bin/activate

# Setup development wrappers
BASE=../contrib/
BIN=venv/bin/
TEMPLATE=${BASE}/launch-venv.sh
for F in fwupdtool fwupdmgr fwupd; do
    rm -f ${BIN}/${F}
    ln -s $TEMPLATE ${BIN}/${F}
done
rm -f ${BIN}/build-fwupd
ln -s ${BASE}/build-venv.sh ${BIN}/build-fwupd
rm -f ${BIN}/test-fwupd
ln -s ${BASE}/test-venv.sh ${BIN}/test-fwupd

# Build the project - takes 45+ minutes, NEVER CANCEL, set timeout 60+ minutes
build-fwupd

# Run tests - takes 15+ minutes, NEVER CANCEL, set timeout 30+ minutes
test-fwupd
```
## Running Applications

### Development Environment Tools
All tools run with elevated privileges automatically in the venv:
- `fwupdtool` - Debugging tool for developers, runs standalone without daemon
- `fwupdmgr` - End-user client tool, requires daemon running
- `fwupd` - Background daemon, run manually in development

### Basic Usage Examples
```bash
# Get devices from specific plugin (fastest for development)
fwupdtool --plugins vli get-devices --verbose

# Get all devices (comprehensive)
fwupdtool get-devices

# Parse firmware blob
fwupdtool firmware-parse /path/to/firmware.bin

# Install firmware blob to device
fwupdtool --verbose --plugins PLUGIN install-blob /path/to/firmware.bin DEVICE_ID
```

### End-to-End Testing (Two Terminals Required)
Terminal 1 - Run daemon:
```bash
source venv/bin/activate
fwupd --verbose
```

Terminal 2 - Run client:
```bash
source venv/bin/activate
fwupdmgr install ~/firmware.cab
```

## Validation

### ALWAYS run these validation steps after making changes:
```bash
# Format code
./contrib/reformat-code.py

# Run linting
pre-commit run --all-files

# Build and test (with proper timeouts)
build-fwupd  # 45+ minutes, NEVER CANCEL
test-fwupd   # 15+ minutes, NEVER CANCEL
```

### Manual Testing Scenarios
Always test at least one complete end-to-end scenario:
1. **Plugin Development**: Test with `fwupdtool --plugins YOURPLUGIN get-devices --verbose`
2. **Daemon Changes**: Run daemon in terminal 1, use fwupdmgr in terminal 2
3. **Firmware Installation**: Use `fwupdtool install-blob` for quick testing

## Repository Structure

### Key Directories
- `src/` - Main daemon and tool source code
- `libfwupd/` - Client library source
- `libfwupdplugin/` - Plugin framework library
- `plugins/` - 130+ hardware vendor plugins
- `contrib/` - Build scripts and CI helpers
- `data/` - Configuration files and resources
- `docs/` - Documentation and building guides

### Important Files
- `meson.build` - Main build configuration
- `meson_options.txt` - Build options (117 configurable options)
- `contrib/setup` - Development environment setup script
- `contrib/ci/fwupd_setup_helpers.py` - Dependency installation helper
- `.github/workflows/matrix.yml` - CI build matrix (Fedora, Debian, Arch, etc.)

### Build Dependencies (Ubuntu)
Over 80 packages including: meson, libglib2.0-dev, libxmlb-dev, libjcat-dev, libarchive-dev, libcbor-dev, libcurl4-gnutls-dev, valgrind, clang-tools, python3-gi-cairo, and many more.

## Common Tasks

### Plugin Development
```bash
# Test specific plugin only
fwupdtool --plugins YOUR_PLUGIN get-devices --verbose

# Parse plugin-specific firmware
fwupdtool firmware-parse firmware.bin
# Choose the appropriate parser from the list

# Install plugin firmware
fwupdtool --verbose --plugins YOUR_PLUGIN install-blob firmware.bin DEVICE_ID
```

### Code Quality
```bash
# Auto-format current patch
./contrib/reformat-code.py

# Format specific commits
./contrib/reformat-code.py HEAD~5

# Run all pre-commit hooks
pre-commit run --all-files

# Check specific files
shellcheck contrib/ci/*.sh
```

### CI Validation (runs on multiple platforms)
The CI tests on: Fedora, CentOS, Debian x86_64, Debian i386, Ubuntu x86_64, Arch Linux
- All builds include AddressSanitizer and -Werror
- Full test suite with LVFS integration testing
- Package installation and removal testing

## Debugging with Visual Studio Code

### Available Tasks
- Build task: `Ctrl+Shift+B` - runs `build-fwupd`
- Test task: `Ctrl+Shift+P` -> "Run test task"
- Debug task: `gdbserver-fwupd` for daemon debugging

### Debugging Tools
```bash
# Debug any tool with gdbserver
DEBUG=1 fwupdtool get-devices
DEBUG=1 fwupdmgr get-devices
```

## Build Options

### Common Meson Options
- `-Dbuild=all|standalone|library` - What to build
- `-Dtests=true|false` - Enable/disable tests
- `-Dplugin_*=enabled|disabled|auto` - Individual plugin control
- `-Dsystemd=enabled|disabled|auto` - systemd integration
- `-Dintrospection=enabled|disabled|auto` - GObject introspection

### Quick Build Variants
```bash
# Library only (faster)
meson setup build -Dbuild=library

# Without tests (faster)
meson setup build -Dtests=false

# Specific plugins only
meson setup build -Dplugin_uefi_capsule=enabled # enable specific plugin
```

## Time Expectations
- **Container setup**: 5-10 minutes (container pull and start)
- **Dependency installation**: 30+ minutes (NEVER CANCEL) - inside container
- **Full build**: 45-60 minutes (NEVER CANCEL)
- **Test suite**: 15-20 minutes (NEVER CANCEL)
- **Individual plugin test**: 1-5 minutes
- **Code formatting**: 1-2 minutes

## Troubleshooting

### Build Issues
- **CONTAINER REQUIRED**: Use `ghcr.io/fwupd/fwupd/fwupd-ubuntu-x86_64:latest` for consistent Ubuntu environment
- **Dependency Installation**: Run `./contrib/ci/fwupd_setup_helpers.py install-dependencies -o ubuntu --yes` inside container
- **APT Lock Issues**: If you see "Could not get lock /var/lib/dpkg/lock-frontend", wait for other apt processes to complete
- Check Meson version: `./contrib/ci/fwupd_setup_helpers.py test-meson`
- Verify venv setup: `source venv/bin/activate` should show `(fwupd)` prompt
- **Python venv**: Use `python3 -m venv venv --system-site-packages` after dependency installation

### Runtime Issues
- For daemon testing: ensure polkit/dbus running
- For plugin testing: use `fwupdtool` instead of full daemon
- For firmware parsing: check plugin supports your hardware format

### Common Validation Failures
- **Pre-commit failures**: Run `./contrib/reformat-code.py` first
- **Test failures**: Check if related to your changes or pre-existing
- **Build failures**: Verify dependencies and increase timeout


