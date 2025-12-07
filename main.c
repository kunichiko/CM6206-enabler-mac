/*
 * CM6206 Enabler by Alexander Thomas, 2009/06 - 2011/01
 * This will activate sound output on some of the cheapest USB 5.1 adaptors
 *   available, more specifically the ones that use the C-Media CM6206 chip.
 *   This chip is also used in some products from major brands, e.g. the
 *   Zalman ZM-RS6F.
 *   The CM6206 is fully USB audio compliant and strictly spoken does not
 *   require drivers under any OS that supports USB audio like OS X, but for
 *   some reason it boots with its outputs disabled. All that's required are
 *   some initialisation commands, and that's exactly what this program does.
 *
 * This is genuine Frankenstein software, composed from lesser parts of Apple
 *   sample code, some previous USB camera thing I wrote, SleepWatcher, USB
 *   sniff logs, and the Linux ALSA drivers.
 * I'm not very experienced in writing software that deals with USB, so it is
 *   entirely possible that this program will cause kernel panics under
 *   special circumstances. Use at your own risk.
 * There's probably also a lot of opportunity to simplify and/or do things
 *   more efficiently.
 *
 * Versions: 1.0  2009/06: initial release
 *           2.0  2011/01: implemented 'daemon mode'
 *           2.1  2011/02: fixed the program causing a delay when entering sleep
 *
 * TODO:
 *   - figure out all the commands supported by the CM6206 and make a GUI
 *     that allows to change those settings (like S/PDIF on/off, channels,
 *     microphone stereo/mono/bias voltage...)
 *   - check if the CM6206 has a built-in 'virtual headphone surround' mode,
 *     and if so, allow to enable it.
 *   - make it work in OS X 10.4.* and 10.3.9. For some reason, interface 2
 *     cannot be opened in those OSs because it is 'in use' (error 2c5)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 *
 * Some useful links that helped to make this possible:
 * http://www.mail-archive.com/alsa-user@lists.sourceforge.net/msg25003.html
 * http://www.mail-archive.com/alsa-user@lists.sourceforge.net/msg25017.html
 * http://www.cs.fsu.edu/~baker/devices/lxr/http/source/linux/sound/usb/usbaudio.c#L3276
 *
 * Many thanks to Mark Hempelmann for donating enough to motivate me to
 *   implement daemon mode :))
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <mach/mach.h>
#include <string.h>
#include <sys/stat.h>
#include <pwd.h>
#include <limits.h>
#include <mach-o/dyld.h>

#include <CoreFoundation/CFNumber.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

#define CMVERSION "3.0.0"

// for debugging
//#define VERBOSE

#define kVendorID	0x0d8c
#define kProductID	0x0102

typedef struct MyPrivateData {
    io_object_t				notification;
    IOUSBDeviceInterface	**deviceInterface;
    CFStringRef				deviceName;
} MyPrivateData;


static IONotificationPortRef	gNotifyPort;
static io_iterator_t			gAddedIter;
static CFRunLoopRef				gRunLoop;
static int						gVerbose;


void printUsage( const char *progName )
{
	printf("Usage: %s [-s] [-d] [-v] [-V] [command]\n", progName );
	printf("  Activates sound outputs on CM6206 USB devices.\n\n");
	printf("Options:\n");
	printf("  -s: Silent mode (default in daemon mode)\n");
	printf("  -v: Verbose mode (default in non-daemon mode)\n");
	printf("  -d: Daemon mode: the program keeps running and automatically activates any\n");
	printf("      devices that are connected, or all devices upon wake-from-sleep.\n");
	printf("  -V: Print version number and exit.\n\n");
	printf("Commands:\n");
	printf("  install-agent      Install as LaunchAgent (auto-start on login, no sudo required)\n");
	printf("  uninstall-agent    Uninstall LaunchAgent\n");
	printf("  install-daemon     Install as LaunchDaemon (auto-start on boot, requires sudo)\n");
	printf("  uninstall-daemon   Uninstall LaunchDaemon\n");
}


//================================================================================================
// Get the path to the current executable
//
int getExecutablePath(char *buffer, size_t size) {
	uint32_t bufsize = size;
	if (_NSGetExecutablePath(buffer, &bufsize) != 0) {
		fprintf(stderr, "Error: executable path buffer too small\n");
		return -1;
	}

	// Resolve symlinks and relative paths
	char realPath[PATH_MAX];
	if (realpath(buffer, realPath) == NULL) {
		fprintf(stderr, "Error: could not resolve executable path\n");
		return -1;
	}

	strncpy(buffer, realPath, size);
	return 0;
}


//================================================================================================
// Install as LaunchAgent
//
int installLaunchAgent() {
	char execPath[PATH_MAX];
	char plistPath[PATH_MAX];
	char launchAgentsDir[PATH_MAX];
	struct passwd *pw = getpwuid(getuid());

	if (pw == NULL) {
		fprintf(stderr, "Error: could not get user home directory\n");
		return -1;
	}

	// Get executable path
	if (getExecutablePath(execPath, sizeof(execPath)) != 0) {
		return -1;
	}

	// Create LaunchAgents directory if it doesn't exist
	snprintf(launchAgentsDir, sizeof(launchAgentsDir), "%s/Library/LaunchAgents", pw->pw_dir);
	mkdir(launchAgentsDir, 0755);

	// Create plist path
	snprintf(plistPath, sizeof(plistPath), "%s/com.kunichiko.cm6206-enabler.plist", launchAgentsDir);

	// Create plist content
	FILE *fp = fopen(plistPath, "w");
	if (fp == NULL) {
		fprintf(stderr, "Error: could not create plist file at %s\n", plistPath);
		return -1;
	}

	fprintf(fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	fprintf(fp, "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n");
	fprintf(fp, "<plist version=\"1.0\">\n");
	fprintf(fp, "<dict>\n");
	fprintf(fp, "\t<key>Label</key>\n");
	fprintf(fp, "\t<string>com.kunichiko.cm6206-enabler</string>\n");
	fprintf(fp, "\t<key>ProgramArguments</key>\n");
	fprintf(fp, "\t<array>\n");
	fprintf(fp, "\t\t<string>%s</string>\n", execPath);
	fprintf(fp, "\t\t<string>-d</string>\n");
	fprintf(fp, "\t</array>\n");
	fprintf(fp, "\t<key>RunAtLoad</key>\n");
	fprintf(fp, "\t<true/>\n");
	fprintf(fp, "\t<key>KeepAlive</key>\n");
	fprintf(fp, "\t<true/>\n");
	fprintf(fp, "\t<key>StandardErrorPath</key>\n");
	fprintf(fp, "\t<string>%s/Library/Logs/cm6206-enabler.log</string>\n", pw->pw_dir);
	fprintf(fp, "\t<key>StandardOutPath</key>\n");
	fprintf(fp, "\t<string>%s/Library/Logs/cm6206-enabler.log</string>\n", pw->pw_dir);
	fprintf(fp, "</dict>\n");
	fprintf(fp, "</plist>\n");

	fclose(fp);

	// Load the plist
	char command[PATH_MAX * 2];
	snprintf(command, sizeof(command), "launchctl load '%s'", plistPath);
	int result = system(command);

	if (result == 0) {
		printf("✓ Successfully installed as LaunchAgent\n");
		printf("  Location: %s\n", plistPath);
		printf("  The program will automatically start on login.\n");
		printf("  Logs: ~/Library/Logs/cm6206-enabler.log\n");
	} else {
		fprintf(stderr, "Warning: plist created but launchctl load failed (exit code: %d)\n", result);
		printf("You may need to manually load it with:\n");
		printf("  launchctl load '%s'\n", plistPath);
	}

	return 0;
}


//================================================================================================
// Uninstall LaunchAgent
//
int uninstallLaunchAgent() {
	char plistPath[PATH_MAX];
	struct passwd *pw = getpwuid(getuid());

	if (pw == NULL) {
		fprintf(stderr, "Error: could not get user home directory\n");
		return -1;
	}

	snprintf(plistPath, sizeof(plistPath), "%s/Library/LaunchAgents/com.kunichiko.cm6206-enabler.plist", pw->pw_dir);

	// Unload the plist
	char command[PATH_MAX * 2];
	snprintf(command, sizeof(command), "launchctl unload '%s' 2>/dev/null", plistPath);
	system(command);

	// Remove the plist file
	if (unlink(plistPath) == 0) {
		printf("✓ Successfully uninstalled LaunchAgent\n");
		printf("  Removed: %s\n", plistPath);
	} else {
		fprintf(stderr, "Error: could not remove %s\n", plistPath);
		fprintf(stderr, "The LaunchAgent may not be installed.\n");
		return -1;
	}

	return 0;
}


//================================================================================================
// Install as LaunchDaemon
//
int installLaunchDaemon() {
	char execPath[PATH_MAX];
	const char *plistPath = "/Library/LaunchDaemons/com.kunichiko.cm6206-enabler.plist";
	const char *installDir = "/Library/Application Support/CM6206";
	const char *installedBinary = "/Library/Application Support/CM6206/cm6206-enabler";

	// Check if running as root
	if (getuid() != 0) {
		fprintf(stderr, "Error: Installing LaunchDaemon requires root privileges.\n");
		fprintf(stderr, "Please run with sudo:\n");
		fprintf(stderr, "  sudo %s install-daemon\n", getprogname());
		return -1;
	}

	// Get executable path
	if (getExecutablePath(execPath, sizeof(execPath)) != 0) {
		return -1;
	}

	// Create installation directory
	mkdir(installDir, 0755);

	// Copy binary to installation directory
	char cpCommand[PATH_MAX * 2];
	snprintf(cpCommand, sizeof(cpCommand), "cp '%s' '%s'", execPath, installedBinary);
	if (system(cpCommand) != 0) {
		fprintf(stderr, "Error: failed to copy binary to %s\n", installedBinary);
		return -1;
	}
	chmod(installedBinary, 0755);

	// Create plist file
	FILE *fp = fopen(plistPath, "w");
	if (fp == NULL) {
		fprintf(stderr, "Error: could not create plist file at %s\n", plistPath);
		return -1;
	}

	fprintf(fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	fprintf(fp, "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n");
	fprintf(fp, "<plist version=\"1.0\">\n");
	fprintf(fp, "<dict>\n");
	fprintf(fp, "\t<key>Label</key>\n");
	fprintf(fp, "\t<string>com.kunichiko.cm6206-enabler</string>\n");
	fprintf(fp, "\t<key>ProgramArguments</key>\n");
	fprintf(fp, "\t<array>\n");
	fprintf(fp, "\t\t<string>%s</string>\n", installedBinary);
	fprintf(fp, "\t\t<string>-d</string>\n");
	fprintf(fp, "\t</array>\n");
	fprintf(fp, "\t<key>RunAtLoad</key>\n");
	fprintf(fp, "\t<true/>\n");
	fprintf(fp, "\t<key>KeepAlive</key>\n");
	fprintf(fp, "\t<true/>\n");
	fprintf(fp, "\t<key>StandardErrorPath</key>\n");
	fprintf(fp, "\t<string>/var/log/cm6206-enabler.log</string>\n");
	fprintf(fp, "\t<key>StandardOutPath</key>\n");
	fprintf(fp, "\t<string>/var/log/cm6206-enabler.log</string>\n");
	fprintf(fp, "</dict>\n");
	fprintf(fp, "</plist>\n");

	fclose(fp);
	chmod(plistPath, 0644);

	// Load the plist
	char command[PATH_MAX * 2];
	snprintf(command, sizeof(command), "launchctl load '%s'", plistPath);
	int result = system(command);

	if (result == 0) {
		printf("✓ Successfully installed as LaunchDaemon\n");
		printf("  Binary: %s\n", installedBinary);
		printf("  Plist: %s\n", plistPath);
		printf("  The program will automatically start on system boot.\n");
		printf("  Logs: /var/log/cm6206-enabler.log\n");
	} else {
		fprintf(stderr, "Warning: plist created but launchctl load failed (exit code: %d)\n", result);
		printf("You may need to manually load it with:\n");
		printf("  sudo launchctl load '%s'\n", plistPath);
	}

	return 0;
}


//================================================================================================
// Uninstall LaunchDaemon
//
int uninstallLaunchDaemon() {
	const char *plistPath = "/Library/LaunchDaemons/com.kunichiko.cm6206-enabler.plist";
	const char *installedBinary = "/Library/Application Support/CM6206/cm6206-enabler";

	// Check if running as root
	if (getuid() != 0) {
		fprintf(stderr, "Error: Uninstalling LaunchDaemon requires root privileges.\n");
		fprintf(stderr, "Please run with sudo:\n");
		fprintf(stderr, "  sudo %s uninstall-daemon\n", getprogname());
		return -1;
	}

	// Unload the plist
	char command[PATH_MAX * 2];
	snprintf(command, sizeof(command), "launchctl unload '%s' 2>/dev/null", plistPath);
	system(command);

	// Remove the plist file
	if (unlink(plistPath) == 0) {
		printf("✓ Unloaded and removed plist: %s\n", plistPath);
	} else {
		fprintf(stderr, "Warning: could not remove %s (may not exist)\n", plistPath);
	}

	// Remove the binary
	if (unlink(installedBinary) == 0) {
		printf("✓ Removed binary: %s\n", installedBinary);
	} else {
		fprintf(stderr, "Warning: could not remove %s (may not exist)\n", installedBinary);
	}

	printf("✓ Successfully uninstalled LaunchDaemon\n");

	return 0;
}


/**** Error handlers ****/
/* Utter overkill, but copy&paste is so easy. */
int ErrorName (IOReturn err, char* out_buf) {
    int ok=true;
    switch (err) {
		case 0: sprintf(out_buf,"ok"); break;
		case kIOReturnError: sprintf(out_buf,"kIOReturnError - general error"); break;
		case kIOReturnNoMemory: sprintf(out_buf,"kIOReturnNoMemory - can't allocate memory");  break;
		case kIOReturnNoResources: sprintf(out_buf,"kIOReturnNoResources - resource shortage"); break;
		case kIOReturnIPCError: sprintf(out_buf,"kIOReturnIPCError - error during IPC"); break;
		case kIOReturnNoDevice: sprintf(out_buf,"kIOReturnNoDevice - no such device"); break;
		case kIOReturnNotPrivileged: sprintf(out_buf,"kIOReturnNotPrivileged - privilege violation"); break;
		case kIOReturnBadArgument: sprintf(out_buf,"kIOReturnBadArgument - invalid argument"); break;
		case kIOReturnLockedRead: sprintf(out_buf,"kIOReturnLockedRead - device read locked"); break;
		case kIOReturnLockedWrite: sprintf(out_buf,"kIOReturnLockedWrite - device write locked"); break;
		case kIOReturnExclusiveAccess: sprintf(out_buf,"kIOReturnExclusiveAccess - exclusive access and device already open"); break;
		case kIOReturnBadMessageID: sprintf(out_buf,"kIOReturnBadMessageID - sent/received messages had different msg_id"); break;
		case kIOReturnUnsupported: sprintf(out_buf,"kIOReturnUnsupported - unsupported function"); break;
		case kIOReturnVMError: sprintf(out_buf,"kIOReturnVMError - misc. VM failure"); break;
		case kIOReturnInternalError: sprintf(out_buf,"kIOReturnInternalError - internal error"); break;
		case kIOReturnIOError: sprintf(out_buf,"kIOReturnIOError - General I/O error"); break;
		case kIOReturnCannotLock: sprintf(out_buf,"kIOReturnCannotLock - can't acquire lock"); break;
		case kIOReturnNotOpen: sprintf(out_buf,"kIOReturnNotOpen - device not open"); break;
		case kIOReturnNotReadable: sprintf(out_buf,"kIOReturnNotReadable - read not supported"); break;
		case kIOReturnNotWritable: sprintf(out_buf,"kIOReturnNotWritable - write not supported"); break;
		case kIOReturnNotAligned: sprintf(out_buf,"kIOReturnNotAligned - alignment error"); break;
		case kIOReturnBadMedia: sprintf(out_buf,"kIOReturnBadMedia - Media Error"); break;
		case kIOReturnStillOpen: sprintf(out_buf,"kIOReturnStillOpen - device(s) still open"); break;
		case kIOReturnRLDError: sprintf(out_buf,"kIOReturnRLDError - rld failure"); break;
		case kIOReturnDMAError: sprintf(out_buf,"kIOReturnDMAError - DMA failure"); break;
		case kIOReturnBusy: sprintf(out_buf,"kIOReturnBusy - Device Busy"); break;
		case kIOReturnTimeout: sprintf(out_buf,"kIOReturnTimeout - I/O Timeout"); break;
		case kIOReturnOffline: sprintf(out_buf,"kIOReturnOffline - device offline"); break;
		case kIOReturnNotReady: sprintf(out_buf,"kIOReturnNotReady - not ready"); break;
		case kIOReturnNotAttached: sprintf(out_buf,"kIOReturnNotAttached - device not attached"); break;
		case kIOReturnNoChannels: sprintf(out_buf,"kIOReturnNoChannels - no DMA channels left"); break;
		case kIOReturnNoSpace: sprintf(out_buf,"kIOReturnNoSpace - no space for data"); break;
		case kIOReturnPortExists: sprintf(out_buf,"kIOReturnPortExists - port already exists"); break;
		case kIOReturnCannotWire: sprintf(out_buf,"kIOReturnCannotWire - can't wire down physical memory"); break;
		case kIOReturnNoInterrupt: sprintf(out_buf,"kIOReturnNoInterrupt - no interrupt attached"); break;
		case kIOReturnNoFrames: sprintf(out_buf,"kIOReturnNoFrames - no DMA frames enqueued"); break;
		case kIOReturnMessageTooLarge: sprintf(out_buf,"kIOReturnMessageTooLarge - oversized msg received on interrupt port"); break;
		case kIOReturnNotPermitted: sprintf(out_buf,"kIOReturnNotPermitted - not permitted"); break;
		case kIOReturnNoPower: sprintf(out_buf,"kIOReturnNoPower - no power to device"); break;
		case kIOReturnNoMedia: sprintf(out_buf,"kIOReturnNoMedia - media not present"); break;
		case kIOReturnUnformattedMedia: sprintf(out_buf,"kIOReturnUnformattedMedia - media not formatted"); break;
		case kIOReturnUnsupportedMode: sprintf(out_buf,"kIOReturnUnsupportedMode - no such mode"); break;
		case kIOReturnUnderrun: sprintf(out_buf,"kIOReturnUnderrun - data underrun"); break;
		case kIOReturnOverrun: sprintf(out_buf,"kIOReturnOverrun - data overrun"); break;
		case kIOReturnDeviceError: sprintf(out_buf,"kIOReturnDeviceError - the device is not working properly!"); break;
		case kIOReturnNoCompletion: sprintf(out_buf,"kIOReturnNoCompletion - a completion routine is required"); break;
		case kIOReturnAborted: sprintf(out_buf,"kIOReturnAborted - operation aborted"); break;
		case kIOReturnNoBandwidth: sprintf(out_buf,"kIOReturnNoBandwidth - bus bandwidth would be exceeded"); break;
		case kIOReturnNotResponding: sprintf(out_buf,"kIOReturnNotResponding - device not responding"); break;
		case kIOReturnIsoTooOld: sprintf(out_buf,"kIOReturnIsoTooOld - isochronous I/O request for distant past!"); break;
		case kIOReturnIsoTooNew: sprintf(out_buf,"kIOReturnIsoTooNew - isochronous I/O request for distant future"); break;
		case kIOReturnNotFound: sprintf(out_buf,"kIOReturnNotFound - data was not found"); break;
		case kIOReturnInvalid: sprintf(out_buf,"kIOReturnInvalid - should never be seen"); break;
		case kIOUSBUnknownPipeErr:sprintf(out_buf,"kIOUSBUnknownPipeErr - Pipe ref not recognised"); break;
		case kIOUSBTooManyPipesErr:sprintf(out_buf,"kIOUSBTooManyPipesErr - Too many pipes"); break;
		case kIOUSBNoAsyncPortErr:sprintf(out_buf,"kIOUSBNoAsyncPortErr - no async port"); break;
		case kIOUSBNotEnoughPipesErr:sprintf(out_buf,"kIOUSBNotEnoughPipesErr - not enough pipes in interface"); break;
		case kIOUSBNotEnoughPowerErr:sprintf(out_buf,"kIOUSBNotEnoughPowerErr - not enough power for selected configuration"); break;
		case kIOUSBEndpointNotFound:sprintf(out_buf,"kIOUSBEndpointNotFound - Not found"); break;
		case kIOUSBConfigNotFound:sprintf(out_buf,"kIOUSBConfigNotFound - Not found"); break;
		case kIOUSBTransactionTimeout:sprintf(out_buf,"kIOUSBTransactionTimeout - time out"); break;
		case kIOUSBTransactionReturned:sprintf(out_buf,"kIOUSBTransactionReturned - The transaction has been returned to the caller"); break;
		case kIOUSBPipeStalled:sprintf(out_buf,"kIOUSBPipeStalled - Pipe has stalled, error needs to be cleared"); break;
		case kIOUSBInterfaceNotFound:sprintf(out_buf,"kIOUSBInterfaceNotFound - Interface ref not recognised"); break;
		case kIOUSBLinkErr:sprintf(out_buf,"kIOUSBLinkErr - <no error description available>"); break;
		case kIOUSBNotSent2Err:sprintf(out_buf,"kIOUSBNotSent2Err - Transaction not sent"); break;
		case kIOUSBNotSent1Err:sprintf(out_buf,"kIOUSBNotSent1Err - Transaction not sent"); break;
		case kIOUSBBufferUnderrunErr:sprintf(out_buf,"kIOUSBBufferUnderrunErr - Buffer Underrun (Host hardware failure on data out, PCI busy?)"); break;
		case kIOUSBBufferOverrunErr:sprintf(out_buf,"kIOUSBBufferOverrunErr - Buffer Overrun (Host hardware failure on data out, PCI busy?)"); break;
		case kIOUSBReserved2Err:sprintf(out_buf,"kIOUSBReserved2Err - Reserved"); break;
		case kIOUSBReserved1Err:sprintf(out_buf,"kIOUSBReserved1Err - Reserved"); break;
		case kIOUSBWrongPIDErr:sprintf(out_buf,"kIOUSBWrongPIDErr - Pipe stall, Bad or wrong PID"); break;
		case kIOUSBPIDCheckErr:sprintf(out_buf,"kIOUSBPIDCheckErr - Pipe stall, PID CRC Err:or"); break;
		case kIOUSBDataToggleErr:sprintf(out_buf,"kIOUSBDataToggleErr - Pipe stall, Bad data toggle"); break;
		case kIOUSBBitstufErr:sprintf(out_buf,"kIOUSBBitstufErr - Pipe stall, bitstuffing"); break;
		case kIOUSBCRCErr:sprintf(out_buf,"kIOUSBCRCErr - Pipe stall, bad CRC"); break;
			
		default: sprintf(out_buf,"Unknown Error:%d Sub:%d System:%d",err_get_code(err),
						 err_get_sub(err),err_get_system(err)); ok=false; break;
    }
    return ok;
}

void ShowError(IOReturn err, char* where) {
    char buf[256];
    if (where) {
		fprintf(stderr, "%s: ", where);
    }
    if (err==0) {
		fprintf(stderr, "ok");
    } else {
		ErrorName(err,buf);
		fprintf(stderr, "Error: %s ", buf);
    }
    fprintf(stderr, "\n");
}

void CheckError(IOReturn err, char* where) {
    if (err) {
		ShowError(err,where);
    }
}


//================================================================================================
//
// "interface" handlers
//
//================================================================================================

int writeCM6206Registers( IOUSBInterfaceInterface183 **intf, UInt8 regNo, UInt16 value )
{
    UInt8 buf[8];
    IOReturn err;
    IOUSBDevRequest req;
    UInt8 pipeNo = 0; // 0 is the default pipe (and the only one that works here)
    
    buf[0] = 0x20;
    buf[1] = value & 0xFF;          // Low byte (DATAL)
    buf[2] = (value >> 8) & 0xFF;   // High byte (DATAH)
    buf[3] = regNo;
    
    req.bmRequestType=USBmakebmRequestType(kUSBOut, kUSBClass, kUSBInterface );
    req.bRequest=0x09; // these values are taken from the SPDIF enable log
    req.wValue=0x0200;
    req.wIndex=0x03;
    req.wLength=4;
    req.pData=buf;
    err=(*intf)->ControlRequest(intf,pipeNo,&req);
    CheckError(err,"usbWriteCmdWithBRequest");
    if (err==kIOUSBPipeStalled) (*intf)->ClearPipeStall(intf,pipeNo);
    
    return (err != 0);
}

//================================================================================================
// This sends the actual activation commands
void initCM6206(IOUSBInterfaceInterface183 **intf)
{
    int err = 0;
    int successCount = 0;
    const int totalCommands = 3;

    // This should reset the registers
    // REG0:
    // bit15      DMA_Master       R/W 1: SPDIFOUT as Master 0: DACs as Master
    // bit14-12   Sampling_rate    R/W SPDIF out sample rate (48K: 3'b010; 96K: 3'b110)
    // bit11-4    Category_code    R/W SPDIF out category code depends on the equipment type
    // bit3       Emphasis         R/W SPDIFOUT emphasis. 1: emphasis-CD_type 0: Emphasis is not indicated
    // bit2       Copyright        R/W 1: not asserted; 0: asserted
    // bit1       Non_audio        R/W 1: non-PCM audio data (like AC3) 0: PCM-data
    // bit0       Pro_con          R/W 1: professional format 0: consumer
    if( writeCM6206Registers(intf, 0x00, 0xa004) == 0 ) {  // REG0 = 0xa004 (S/PDIF Master, 48kHz, Copyright=not asserted)
        successCount++;
        if(gVerbose)
            fprintf(stderr, "  [1/3] REG0 configuration (S/PDIF, sampling rate): OK\n");
    } else {
    	fprintf(stderr, "  [1/3] REG0 configuration (S/PDIF, sampling rate): FAILED\n");
		err = 1;
    }

    // This enables SPDIF, values copied from SniffUSB log (this one was easy)
    // I'm not sure if the SPDIF outputs surround data, as I don't have the means to test it.
    // REG1:
    // bit15      Rsvd             R/W Reserved
    // bit14      SEL_CLK          R/W For test. Select 44.1k source for DACs 1=from 22.58M 0=from 24.576M
    // bit13      PLLBINen         R/W PLL binary search enable
    // bit12      SOFTMUTEen       R/W Soft mute enable
    // bit11      GPIO4_o          R/W Gpio4 signal
    // bit10      GPIO4_OEN        R/W Gpio4 output enable
    // bit9       GPIO3_o          R/W Gpio3 signal
    // bit8       GPIO3_OEN        R/W Gpio3 output enable
    // bit7       GPIO2_o          R/W Gpio2 signal
    // bit6       GPIO2_OEN        R/W Gpio2 output enable
    // bit5       GPIO1_o          R/W Gpio1 signal
    // bit4       GPIO1_OEN        R/W Gpio1 output enable
    // bit3       VALID            R/W SPDIFOUT Valid Signal 1=un-valid
    // bit2       SPDIFLOOP        R/W SPDIF loop-back enable
    // bit1       DIS_SPDIFO       R/W SPDIF out disable
    // bit0       SPDIFMIX         R/W SPDIF in mix enable
    /* 🍎 */
    if( writeCM6206Registers(intf, 0x01, 0x2000) == 0 ) {  // REG1 = 0x2000 (PLLBINen=1)
        successCount++;
        if(gVerbose)
            fprintf(stderr, "  [2/3] REG1 configuration (PLL binary search): OK\n");
    } else {
    	fprintf(stderr, "  [2/3] REG1 configuration (PLL binary search): FAILED\n");
		err = 1;
    }

    // This enables sound output. Why on earth it's disabled upon power-on,
    // nobody knows (except maybe some Taiwanese engineer).
    // These values were taken from the ALSA USB driver: "Enable line-out driver mode,
    // set headphone source to front channels, enable stereo mic."
    // That's for the CM106, however. On the CM6206 they appear to enable everything.
    // REG2:
    // bit15      DRIVERON         R/W Line-out driver mode enable (1=enable)
    // bit14-3    (various)        R/W Headphone source, channel config, etc.
    // bit2       EN_BTL           R/W BTL mode enable (2-channel mode only) / Stereo mic enable
    // bit1-0     (reserved)       R/W
    if( writeCM6206Registers(intf, 0x02, 0x8004) == 0 ) {  // REG2 = 0x8004 (DRIVERON=1, EN_BTL=1)
        successCount++;
        if(gVerbose)
            fprintf(stderr, "  [3/3] REG2 configuration (analog output, stereo mic): OK\n");
    } else {
    	fprintf(stderr, "  [3/3] REG2 configuration (analog output, stereo mic): FAILED\n");
    	err = 1;
    }

    // Extra stuff, taken from the Alsa-user mailinglist.
    // The above works for me, so I didn't bother testing the following.
    // It may be completely redundant or make your Mac explode. Try at your own risk.

    // "Enable DACx2, PLL binary, Soft Mute, and SPDIF-out"
    //writeCM6206Registers(intf, 0x01, 0xb000);
    // "Enable all channels and select 48-pin chipset"
    //writeCM6206Registers(intf, 0x03, 0x007e);

    // Print summary
    if(successCount == totalCommands) {
        if(gVerbose)
            fprintf(stderr, "Successfully sent all CM6206 activation commands (%d/%d)\n",
                    successCount, totalCommands);
        else
            fprintf(stderr, "Successfully sent CM6206 activation commands!\n");
    } else {
        fprintf(stderr, "Warning: Only %d/%d commands succeeded\n",
                successCount, totalCommands);
    }
}


void dealWithInterface(io_service_t usbInterfaceRef)
{
    IOReturn					err;
    IOCFPlugInInterface 		**iodev;	// requires <IOKit/IOCFPlugIn.h>
    IOUSBInterfaceInterface183	**intf;
    SInt32						score;
    int							interfaceOpened = 0;


    err = IOCreatePlugInInterfaceForService(usbInterfaceRef, kIOUSBInterfaceUserClientTypeID,
											kIOCFPlugInInterfaceID, &iodev, &score);
    if (err || !iodev) {
		fprintf(stderr, "dealWithInterface: unable to create plugin. ret = %08x, iodev = %p\n", err, iodev);
		return;
    }
    err = (*iodev)->QueryInterface(iodev, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID183), (LPVOID)&intf);
    (*iodev)->Release(iodev);				// done with this
    if (err || !intf) {
		fprintf(stderr, "dealWithInterface: unable to create a device interface. ret = %08x, intf = %p\n", err, intf);
		return;
    }
    err = (*intf)->USBInterfaceOpen(intf);
    if (err) {
		// Try to seize the interface if open fails
		// Alas, this doesn't solve the problem in OS X 10.4.*
		err = (*intf)->USBInterfaceOpenSeize(intf);
		if (err) {
			// On modern macOS, the interface may be held by the system's USB audio driver.
			// However, we can still send USB control requests even without exclusive access.
			// This is particularly important for kIOReturnExclusiveAccess (0xe00002c5).
			if (err == kIOReturnExclusiveAccess) {
				// This is expected on modern macOS - only show in verbose mode
				if (gVerbose) {
					fprintf(stderr, "dealWithInterface: interface held by system driver (expected)\n");
					fprintf(stderr, "  Continuing without exclusive access (USB control requests will still work)\n");
				}
				// Continue to initCM6206() without setting interfaceOpened flag
			} else {
				// For other unexpected errors, show the error and abort
				fprintf(stderr, "dealWithInterface: unable to open/seize interface. ret = %08x\n", err);
				return;
			}
		} else {
			interfaceOpened = 1;
		}
    } else {
		interfaceOpened = 1;
	}
#ifdef VERBOSE
	{
		UInt8 numPipes;
		err = (*intf)->GetNumEndpoints(intf, &numPipes);
		if (err) {
			fprintf(stderr, "dealWithInterface: unable to get number of endpoints. ret = %08x\n", err);
			(*intf)->USBInterfaceClose(intf);
			(*intf)->Release(intf);
			return;
		}
		fprintf(stderr, "numPipes = %d\n", numPipes);
    }
#endif

	initCM6206(intf);

    // Only try to close the interface if we successfully opened it
    if (interfaceOpened) {
		err = (*intf)->USBInterfaceClose(intf);
		if (err) {
			fprintf(stderr, "dealWithInterface: unable to close interface. ret = %08x\n", err);
			// Don't return here, still try to release
		}
	}
    err = (*intf)->Release(intf);
    if (err) {
		fprintf(stderr, "dealWithInterface: unable to release interface. ret = %08x\n", err);
		return;
    }
}


void dealWithDevice(io_service_t usbDeviceRef)
{
    IOReturn					err;
    IOCFPlugInInterface			**iodev;	// requires <IOKit/IOCFPlugIn.h>
    IOUSBDeviceInterface		**dev;
    SInt32						score;
    UInt8						numConf;
    IOUSBConfigurationDescriptorPtr	confDesc;
    IOUSBFindInterfaceRequest		interfaceRequest;
    io_iterator_t				iterator;
    io_service_t				usbInterfaceRef;
    int nCount;
    int nAttempts = 20;
    
    err = IOCreatePlugInInterfaceForService(usbDeviceRef, kIOUSBDeviceUserClientTypeID,
											kIOCFPlugInInterfaceID, &iodev, &score);
    if (err || !iodev) {
		fprintf(stderr, "dealWithDevice: unable to create plugin. ret = %08x, iodev = %p\n", err, iodev);
		return;
    }
    err = (*iodev)->QueryInterface(iodev, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID197), (LPVOID)&dev);
    (*iodev)->Release(iodev);	// done with this
    if (err || !dev) {
		fprintf(stderr, "dealWithDevice: unable to create a device interface. ret = %08x, dev = %p\n", err, dev);
		return;
    }
	
    // This is from another USB program I wrote where the device was sometimes slow.
	// It doesn't hurt to leave it in.
    do {
		err = (*dev)->USBDeviceOpen(dev);
		if(err) {
			fprintf(stderr, "Trying to open device, %d seconds left...\n",nAttempts);
			if( nAttempts > 1 )
				sleep(1); // wait a second
		}
		else
			nAttempts = 1;
    }
    while( --nAttempts > 0 );
    if (err) {
		fprintf(stderr, "dealWithDevice: unable to open device. ret = %08x\n", err);
		return;
    }
    
    err = (*dev)->GetNumberOfConfigurations(dev, &numConf);
    if (err || !numConf) {
		fprintf(stderr, "dealWithDevice: unable to obtain the number of configurations. ret = %08x\n", err);
        (*dev)->USBDeviceClose(dev);
        (*dev)->Release(dev);
		return;
    }
#ifdef VERBOSE
    fprintf(stderr, "found %d configurations\n", numConf);
#endif
    
    err = (*dev)->GetConfigurationDescriptorPtr(dev, 0, &confDesc);	// get the first config desc (index 0)
    if (err) {
		fprintf(stderr, "dealWithDevice:unable to get config descriptor for index 0\n");
        (*dev)->USBDeviceClose(dev);
        (*dev)->Release(dev);
		return;
    }
    err = (*dev)->SetConfiguration(dev, confDesc->bConfigurationValue);
    if (err) {
		fprintf(stderr, "dealWithDevice: unable to set the configuration\n");
        (*dev)->USBDeviceClose(dev);
        (*dev)->Release(dev);
		return;
    }
    
	// It's probably possible to get the identifiers of the interface we want and
	// directly query that interface, but this works too.
    interfaceRequest.bInterfaceClass = kIOUSBFindInterfaceDontCare;	// requested class
    interfaceRequest.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;	// requested subclass
    interfaceRequest.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;	// requested protocol
    interfaceRequest.bAlternateSetting = kIOUSBFindInterfaceDontCare;	// requested alt setting
    
    err = (*dev)->CreateInterfaceIterator(dev, &interfaceRequest, &iterator);
    if (err) {
		fprintf(stderr, "dealWithDevice: unable to create interface iterator\n");
        (*dev)->USBDeviceClose(dev);
        (*dev)->Release(dev);
		return;
    }
	
    nCount = 0;
    while( (usbInterfaceRef = IOIteratorNext(iterator)) ) {
#ifdef VERBOSE
		fprintf(stderr, "found interface: %p\n", (void*)(size_t)usbInterfaceRef);
#endif
		if( nCount == 1 ) // The second interface is the one we need
			dealWithInterface(usbInterfaceRef); // Here the actual interesting stuff happens!!!
		IOObjectRelease(usbInterfaceRef);
		nCount++;
    }
    
    IOObjectRelease(iterator);
    iterator = 0;
	
    err = (*dev)->USBDeviceClose(dev);
    if (err) {
		fprintf(stderr, "dealWithDevice: error closing device - %08x\n", err);
		(*dev)->Release(dev);
		return;
    }
    err = (*dev)->Release(dev);
    if (err) {
		fprintf(stderr, "dealWithDevice: error releasing device - %08x\n", err);
		return;
    }
}


//================================================================================================
//
//	DeviceNotification
//
//	This routine will get called whenever any kIOGeneralInterest notification happens.  We are
//	interested in the kIOMessageServiceIsTerminated message so that's what we look for.  Other
//	messages are defined in IOMessage.h.
//
//================================================================================================
void DeviceNotification(void *refCon, io_service_t service, natural_t messageType, void *messageArgument)
{
    kern_return_t	kr;
    MyPrivateData	*privateDataRef = (MyPrivateData *) refCon;
    
    if (messageType == kIOMessageServiceIsTerminated) {
		if(gVerbose) {
			fprintf(stderr, "CM6206 device removed.\n");
			// Dump our private data just to see what it looks like.
			fprintf(stderr, "privateDataRef->deviceName: ");
			CFShow(privateDataRef->deviceName);
		}
		
        // Free the data we're no longer using now that the device is going away
        CFRelease(privateDataRef->deviceName);
        
        if (privateDataRef->deviceInterface) {
            kr = (*privateDataRef->deviceInterface)->Release(privateDataRef->deviceInterface);
        }
        
        kr = IOObjectRelease(privateDataRef->notification);
        
        free(privateDataRef);
    }
}


//================================================================================================
//
//	DeviceAdded
//
//	This routine is the callback for our IOServiceAddMatchingNotification.  When we get called
//	we will look at all the devices that were added and we will:
//
//	1.  Create some private data to relate to each device
//	2.  Submit an IOServiceAddInterestNotification of type kIOGeneralInterest for this device,
//	    using the refCon field to store a pointer to our private data.  When we get called with
//	    this interest notification, we can grab the refCon and access our private data.
//  3.  Run the CM6206 activation routine.
//
//================================================================================================
void DeviceAdded(void *refCon, io_iterator_t iterator)
{
    kern_return_t		kr;
    io_service_t		usbDevice;
    
    while ((usbDevice = IOIteratorNext(iterator))) {
        io_name_t		deviceName;
        CFStringRef		deviceNameAsCFString;	
        MyPrivateData	*privateDataRef = NULL;
        
        fprintf(stderr, "CM6206 device added.\n");
        
        // Add some app-specific information about this device.
        // Create a buffer to hold the data.
        privateDataRef = malloc(sizeof(MyPrivateData));
        bzero(privateDataRef, sizeof(MyPrivateData));
		
        // Get the USB device's name.
        kr = IORegistryEntryGetName(usbDevice, deviceName);
		if (KERN_SUCCESS != kr) {
            deviceName[0] = '\0';
        }
        
        deviceNameAsCFString = CFStringCreateWithCString(kCFAllocatorDefault, deviceName, 
                                                         kCFStringEncodingASCII);
        
        // Dump our data to stderr just to see what it looks like.
		if(gVerbose) {
			fprintf(stderr, "deviceName: ");
			CFShow(deviceNameAsCFString);
		}
        
        // Save the device's name to our private data.        
        privateDataRef->deviceName = deviceNameAsCFString;
		
        // Register for an interest notification of this device being removed. Use a reference to our
        // private data as the refCon which will be passed to the notification callback.
        kr = IOServiceAddInterestNotification(gNotifyPort,						// notifyPort
											  usbDevice,						// service
											  kIOGeneralInterest,				// interestType
											  DeviceNotification,				// callback
											  privateDataRef,					// refCon
											  &(privateDataRef->notification)	// notification
											  );
		
        if (KERN_SUCCESS != kr) {
            fprintf(stderr, "IOServiceAddInterestNotification returned 0x%08x.\n", kr);
        }
		
		// This is not strictly necessary but it seems to avoid kernel panics when some
		// third-party audio enhancers are active.
		sleep(1);
		
		dealWithDevice(usbDevice);  // here the important stuff happens
		
        // Done with this USB device; release the reference added by IOIteratorNext
        kr = IOObjectRelease(usbDevice);
    }
}

//================================================================================================
//
//	SignalHandler
//
//	This routine will get called when we interrupt the program (usually with a Ctrl-C from the
//	command line).
//
//================================================================================================
void SignalHandler( int sigraised )
{
	if(gVerbose)
		fprintf(stderr, "cm6206-enabler caught signal %d, exiting\n", sigraised);
	
    exit(0);
}


//================================================================================================
// Make a matching dictionary to find all devices with the given vendor & product ID
//
int makeDictionary( CFMutableDictionaryRef *matchingDictionary, SInt32 idVendor, SInt32 idProduct )
{
    CFNumberRef			numberRef = 0;
	
    *matchingDictionary = IOServiceMatching(kIOUSBDeviceClassName);	// requires <IOKit/usb/IOUSBLib.h>
    if (!*matchingDictionary) {
        fprintf(stderr, "Error: Could not create matching dictionary\n");
        return -1;
    }
    numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &idVendor);
    if (!numberRef) {
        fprintf(stderr, "Error: Could not create CFNumberRef for vendor\n");
        return -1;
    }
    CFDictionaryAddValue(*matchingDictionary, CFSTR(kUSBVendorID), numberRef);
    CFRelease(numberRef);
    numberRef = 0;
    numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &idProduct);
    if (!numberRef) {
        fprintf(stderr, "Error: Could not create CFNumberRef for product\n");
        return -1;
    }
    CFDictionaryAddValue(*matchingDictionary, CFSTR(kUSBProductID), numberRef);
    CFRelease(numberRef);
    numberRef = 0;
	
	return 0;
}


//================================================================================================
// Look for all matching devices and deal with them once.
//
int ActivateDevices()
{
    kern_return_t		kr;
	mach_port_t			masterPort = 0;	// requires <mach/mach.h>
    CFMutableDictionaryRef 	matchingDictionary = 0;	// requires <IOKit/IOKitLib.h>
    io_iterator_t 		iterator = 0;
    io_service_t		usbDeviceRef;
    int					nRet, foundDevice = 0;
	
	kr = IOMainPort(MACH_PORT_NULL, &masterPort);
	if (kr) {
		fprintf(stderr, "Error: Could not create main port, err = %08x\n", kr);
		return kr;
	}
	
	nRet = makeDictionary( &matchingDictionary, kVendorID, kProductID );
	if (nRet)
		return nRet;
	
	kr = IOServiceGetMatchingServices(masterPort, matchingDictionary, &iterator);
	matchingDictionary = 0;		// this was consumed by the above call
	
	while ( (usbDeviceRef = IOIteratorNext(iterator)) ) {
		foundDevice = 1;
		if(gVerbose)
			fprintf(stderr, "CM6206 found (device %p)\n", (void*)(size_t)usbDeviceRef);
		dealWithDevice(usbDeviceRef);  // here the important stuff happens
		IOObjectRelease(usbDeviceRef);	// no longer need this reference
	}
	if(! foundDevice && gVerbose)
		fprintf(stderr, "No CM6206 device found on the USB bus.\n");
	
	IOObjectRelease(iterator);
	iterator = 0;
	
	mach_port_deallocate(mach_task_self(), masterPort);
	
	return 0;
}	


//================================================================================================
// Callback for power events (sleep, wake).
//
void powerCallback(void *rootPort, io_service_t y, natural_t msgType, void *msgArgument)
{	
	if( msgType == kIOMessageSystemHasPoweredOn ) {
		if(gVerbose)
			fprintf(stderr, "Waking from sleep, re-activating any CM6206 devices...\n");
		sleep(1);
		ActivateDevices();
	}
	else if( msgType == kIOMessageCanSystemSleep ||
	         msgType == kIOMessageSystemWillSleep ) {
		// This case must be treated, otherwise the system will wait in vain for the program
		// to allow sleep, and only sleep after a timeout.
		IOAllowPowerChange(* (io_connect_t *) rootPort, (long) msgArgument);
	}
}


//================================================================================================
//
int main(int argc, const char * argv[])
{
	int					bDaemon = 0;
    sig_t				oldHandler;
	gVerbose = 0;  // Default to silent mode (use -v for verbose output)

	for( int a=1; a<argc; a++ ) {
		if( strcmp( argv[a], "-d" ) == 0 ) {
			bDaemon = 1;
			gVerbose = 0;
		}
		else if( strcmp( argv[a], "-v" ) == 0 )
			gVerbose = 1;
		else if( strcmp( argv[a], "-s" ) == 0 )
			gVerbose = 0;
		else if( strcmp( argv[a], "-V" ) == 0 ) {
			printf( "cm6206-enabler version %s\n", CMVERSION );
			return 0;
		}
		else if( strcmp( argv[a], "-h" ) == 0 || strcmp( argv[a], "--help" ) == 0 ) {
			printUsage(argv[0]);
			return 0;
		}
		else if( strcmp( argv[a], "install-agent" ) == 0 ) {
			return installLaunchAgent();
		}
		else if( strcmp( argv[a], "uninstall-agent" ) == 0 ) {
			return uninstallLaunchAgent();
		}
		else if( strcmp( argv[a], "install-daemon" ) == 0 ) {
			return installLaunchDaemon();
		}
		else if( strcmp( argv[a], "uninstall-daemon" ) == 0 ) {
			return uninstallLaunchDaemon();
		}
		else {
			fprintf(stderr, "Ignoring unknown argument `%s'\n", (argv[a]));
		}
	}
	
	
	// Set up a signal handler so we can clean up when we're interrupted from the command line
    // Otherwise we stay in our run loop forever.
    oldHandler = signal(SIGINT, SignalHandler);
    if (oldHandler == SIG_ERR) {
        fprintf(stderr, "Could not establish new signal handler.");
	}
	signal(SIGHUP, (void*)ActivateDevices);
	
	
	if(bDaemon) {
		kern_return_t			kr;
		CFMutableDictionaryRef 	matchingDictionary = 0;	// requires <IOKit/IOKitLib.h>
		CFRunLoopSourceRef		runLoopSource;
		static io_connect_t		rootPort;
		IONotificationPortRef	notificationPort;
		io_object_t				notifier;		
		int						nRet;
		
		// Start run loop:
		// if a device is found, send activation commands
		// if a wake-from-sleep is detected, resend activation commands to all devices
		// if a device disconnects, remove its reference
		nRet = makeDictionary( &matchingDictionary, kVendorID, kProductID );
		if (nRet)
			return nRet;

		gNotifyPort = IONotificationPortCreate(kIOMainPortDefault);
		runLoopSource = IONotificationPortGetRunLoopSource(gNotifyPort);
		
		gRunLoop = CFRunLoopGetCurrent();
		CFRunLoopAddSource(gRunLoop, runLoopSource, kCFRunLoopDefaultMode);
		
		// Now set up a notification to be called when a device is first matched by I/O Kit.
		kr = IOServiceAddMatchingNotification(gNotifyPort,					// notifyPort
											  kIOFirstMatchNotification,	// notificationType
											  matchingDictionary,			// matching
											  DeviceAdded,					// callback
											  NULL,							// refCon
											  &gAddedIter					// notification
											  );
		
		// Set up callback for when system wakes from sleep
		rootPort = IORegisterForSystemPower(&rootPort, &notificationPort, powerCallback, &notifier);
		if (! rootPort) {
			fprintf(stderr, "IORegisterForSystemPower failed\n");
			return -1;
		}
		CFRunLoopAddSource(gRunLoop, IONotificationPortGetRunLoopSource(notificationPort), kCFRunLoopDefaultMode);		
		
		// Iterate once to get already-present devices and arm the notification    
		DeviceAdded(NULL, gAddedIter);	
		
		// Start the run loop. Now we'll receive notifications.
		if(gVerbose)
			printf("Starting run loop.\n\n");
		CFRunLoopRun();
        
		// We should never get here
		fprintf(stderr, "Unexpectedly back from CFRunLoopRun()!\n");
		return -1;
	}
	else {
		// Check for CM6206 once
		return ActivateDevices();
	}
}
