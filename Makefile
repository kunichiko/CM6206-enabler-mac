prefix = /usr/local
bindir = $(prefix)/bin
sharedir = $(prefix)/share/cm6206-enabler

PROJECT = CM6206-enabler-mac.xcodeproj
SCHEME = CM6206-enabler-mac
CONFIGURATION = Release
BUILD_DIR = build

.PHONY: build install uninstall clean

build:
	xcodebuild -project "$(PROJECT)" \
		-scheme "$(SCHEME)" \
		-configuration "$(CONFIGURATION)" \
		SYMROOT="$(BUILD_DIR)" \
		clean build

install: build
	install -d "$(bindir)"
	install "$(BUILD_DIR)/$(CONFIGURATION)/cm6206-enabler" "$(bindir)"
	install -d "$(sharedir)"
	install -m 644 "Installer/be.dr-lex.cm6206init.plist" "$(sharedir)"

uninstall:
	rm -f "$(bindir)/cm6206-enabler"
	rm -rf "$(sharedir)"

clean:
	rm -rf "$(BUILD_DIR)"
