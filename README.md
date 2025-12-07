# CM6206 Enabler for macOS

[日本語版はこちら (Japanese version)](README.ja.md)

An initialization tool to enable all features of USB audio interfaces using the CM6206 chip on macOS.

## Overview

The CM6206 is a multi-channel sound chip manufactured by C-Media, widely used in affordable USB audio devices supporting 5.1 or 7.1 surround sound. This chip is found in products such as:

- Various no-brand USB 5.1/7.1 sound adapters
- Zalman ZM-RS6F headphones
- Diamond XS71U
- Many other USB audio products

### The Problem

The CM6206 chip is a fully USB Audio compliant device that is recognized without drivers on modern operating systems including macOS. However, it has a peculiar specification: **audio output is disabled by default at the factory**. As a result, even though it's recognized as an audio device, no sound is produced.

Additionally, **optical digital input/output (S/PDIF) is also disabled by default**.

### The Solution

This program sends initialization commands via USB to enable the following features:

- Analog audio output (all channels)
- S/PDIF optical digital input/output
- Stereo microphone input

## Key Features

- **One-shot mode**: Detects and initializes CM6206 devices once
- **Daemon mode**: Runs persistently and automatically initializes devices on connection or wake from sleep
- **Sleep compatible**: Automatically reinitializes upon macOS wake

## Technical Details

### USB Control Commands

This program sends the following three USB control requests to interface 2 of the CM6206:

1. **REG0 Configuration** (Register 0x00 = 0xa004)
   - DMA_Master=1 (Set S/PDIF input as master clock)
   - Sampling rate=48kHz
   - Copyright=not asserted (Disable copy protection flag for S/PDIF output)

2. **REG1 Configuration** (Register 0x01 = 0x2000)
   - PLLBINen=1 (Enable PLL binary search)
   - Enable S/PDIF and clock generation

3. **REG2 Configuration** (Register 0x02 = 0x8004)
   - DRIVERON=1 (Enable line-out driver)
   - EN_BTL=1 (Enable stereo microphone)
   - Enable all analog output channels

### Device Identification

- **Vendor ID**: 0x0d8c (C-Media Electronics Inc.)
- **Product ID**: 0x0102

## Installation

Install using Homebrew:

```bash
brew install kunichiko/tap/cm6206-enabler
```

## Update

```bash
# Update Homebrew package information
brew update

# Update cm6206-enabler to the latest version
brew upgrade cm6206-enabler
```

Or update all Homebrew packages at once:

```bash
brew upgrade
```

Check the current version:

```bash
cm6206-enabler -V
```

## Usage

### Command Line Options

```
cm6206-enabler [-s] [-d] [-v] [-V]

Options:
  -v  Verbose mode: Display detailed initialization messages
  -s  Silent mode (same as default, explicitly disable verbose output)
  -d  Daemon mode: Keep the program running and automatically initialize
      devices on connection or wake from sleep
  -V  Display version number and exit
```

### Basic Usage Examples

```bash
# Initialize connected CM6206 devices once
./cm6206-enabler

# Start in daemon mode (recommended)
./cm6206-enabler -d
```

### Auto-Start Configuration

To automatically initialize devices on system startup or wake from sleep, register as a LaunchAgent or LaunchDaemon.

#### LaunchAgent (Recommended - Easy)

Auto-starts on user login. Can be configured easily **without sudo**.

```bash
# Install
cm6206-enabler install-agent

# Uninstall
cm6206-enabler uninstall-agent
```

**Features**:
- ✅ No sudo required
- ✅ Can be configured per user
- ✅ Recognized as a "Login Item" on macOS Ventura and later
- ✅ Install with a single command
- Log: `~/Library/Logs/cm6206-enabler.log`

#### LaunchDaemon (Advanced Use)

Runs immediately after system startup. **Requires sudo**.

```bash
# Install (requires sudo)
sudo cm6206-enabler install-daemon

# Uninstall (requires sudo)
sudo cm6206-enabler uninstall-daemon
```

**Features**:
- Active immediately after system startup
- More reliable operation on wake from sleep
- Shared across all users
- Log: `/var/log/cm6206-enabler.log`

## System Requirements

- Tested on macOS Tahoe (26.0) and later
- **Note**: Not tested on other macOS versions, but may work

## Limitations

### Program Limitations

- CM6206 cannot independently decode AC3 or DTS streams
- No upmixing functionality from stereo sources to surround
- S/PDIF output only mirrors front channels
- On macOS 10.7 Lion and later, devices must be connected at startup or wake from sleep

#### Important Note About S/PDIF Input

This program enables S/PDIF input, but **cannot capture signals with SCMS copy protection (Copyright bit) enabled**.

- CM6206 detects the Copyright bit in S/PDIF signals and blocks recording of copy-protected signals (Copyright=asserted)
- This program sets the Copyright bit of CM6206's own S/PDIF output signal to "not asserted", but we have not found a way to disable copy protection checks for signals from external devices
- Some commercial audio devices (CD players, game consoles, etc.) output with the Copyright bit enabled by default, so recording from these devices may not be possible
- **Currently, no method has been found to capture signals with copy protection enabled on CM6206**

S/PDIF input from DIY devices or devices without copy protection can be captured without issues.

### Compatibility Issues

- On macOS 10.4 and earlier, there is an issue where interface 2 cannot be opened due to "in use" error (0x2c5)

## License

GNU General Public License v3.0 or later

This program is provided without warranty. Use at your own risk.

## Authors and Credits

**Original Author**: Alexander Thomas
**Development Period**: June 2009 - February 2011

**Modification & Maintenance**: Kunihiko Ohnaka
**Modification Period**: December 2025 -

### Version History

- **1.0** (2009/06): Initial release
- **2.0** (2011/01): Daemon mode implementation
- **2.1** (2011/02): Fixed sleep delay issues
- **3.0.0** (2025/12): Major update for macOS Tahoe compatibility
  - Latest macOS support (Tahoe 26.0 and later)
  - S/PDIF input support with optimized register configuration
  - LaunchAgent/LaunchDaemon easy installation
  - Homebrew support with Makefile
  - Changed default to silent mode, improved message accuracy
  - Documentation improvements (bilingual README, developer guide)

## References

### Original Project

This project is based on the following original project by Alexander Thomas:

- [https://www.dr-lex.be/software/cm6206.html](https://www.dr-lex.be/software/cm6206.html)

### Useful Links

- [ALSA mailing list discussion 1](http://www.mail-archive.com/alsa-user@lists.sourceforge.net/msg25003.html)
- [ALSA mailing list discussion 2](http://www.mail-archive.com/alsa-user@lists.sourceforge.net/msg25017.html)
- [Linux USB audio driver source](http://www.cs.fsu.edu/~baker/devices/lxr/http/source/linux/sound/usb/usbaudio.c#L3276)

## Troubleshooting

### Device Not Recognized

You can check if the CM6206 device is connected with the following commands:

```bash
# Search by device name (easiest)
system_profiler SPUSBHostDataType | grep -B 5 -A 10 "USB Sound Device"

# Or search by Vendor ID (also shows device name)
system_profiler SPUSBHostDataType | grep -B 6 -A 5 "USB Vendor ID: 0x0d8c"

# Using ioreg command
ioreg -p IOUSB -l -r -n "USB Sound Device"
```

CM6206 is typically recognized with the name "**USB Sound Device**". It has USB Vendor ID `0x0d8c` (C-Media Electronics Inc.) and Product ID `0x0102`.

### No Sound Output

1. Check if "**USB Sound Device**" is selected in macOS Sound settings
   - CM6206 is displayed as "USB Sound Device" on macOS
2. Run the program in verbose mode to check for error messages:
   ```bash
   cm6206-enabler -v
   ```

## Contributing

Please report bugs and feature requests on GitHub Issues.

## Notice

As the original author himself stated, "I wouldn't recommend this kind of sound card for hassle-free surround sound," this device has many limitations. If you want a better surround sound experience, consider using a dedicated surround decoder.

---

## Developer Information

### Building from Source

An Xcode project is included:

```bash
# Build with Xcode
xcodebuild -project CM6206-enabler-mac.xcodeproj -configuration Release

# Or use Makefile
make build
make install  # Install to /usr/local/bin
```

Alternatively, you can open `CM6206-enabler-mac.xcodeproj` with Xcode and build.

### Updating When Built from Source

```bash
# Update repository to latest version
cd /path/to/CM6206-enabler-mac
git pull origin main

# Rebuild and reinstall
make clean
make build
sudo make install
```
