APP_NAME := nixlabs-discord-helper
VERSION := 1.0.5
ARCH := amd64
DEB_NAME := $(APP_NAME)_$(VERSION)_$(ARCH).deb

VITE := /home/thy/Projects/myaccounts/node_modules/.bin/vite

.PHONY: all frontend backend icons deb clean install

all: frontend backend icons

frontend:
	@echo "==> Building React frontend with Vite..."
	cd frontend && $(VITE) build

backend: frontend
	@echo "==> Syncing frontend dist to backend..."
	rm -rf backend/cmd/nixlabs-discord-helper/dist
	cp -r frontend/dist backend/cmd/nixlabs-discord-helper/dist
	@echo "==> Compiling Go binary..."
	cd backend && CGO_ENABLED=0 go build -ldflags="-s -w" -o nixlabs-discord-helper-bin ./cmd/nixlabs-discord-helper

icons:
	@echo "==> Generating pristine raster icons for tray & desktop..."
	gjs assets/generate_icons.js

deb: backend icons
	@echo "==> Assembling Debian package structure..."
	rm -rf build/deb
	mkdir -p build/deb/DEBIAN
	mkdir -p build/deb/usr/bin
	mkdir -p build/deb/usr/lib/$(APP_NAME)
	mkdir -p build/deb/usr/share/applications
	mkdir -p build/deb/usr/share/icons/hicolor/scalable/apps

	# Copy Debian control files
	cp debian/control build/deb/DEBIAN/
	cp debian/postinst build/deb/DEBIAN/
	cp debian/prerm build/deb/DEBIAN/
	chmod 0755 build/deb/DEBIAN/postinst build/deb/DEBIAN/prerm

	# Copy application executables and libraries
	cp launcher/nixlabs-discord-helper build/deb/usr/bin/$(APP_NAME)
	chmod 0755 build/deb/usr/bin/$(APP_NAME)

	cp backend/nixlabs-discord-helper-bin build/deb/usr/lib/$(APP_NAME)/
	chmod 0755 build/deb/usr/lib/$(APP_NAME)/nixlabs-discord-helper-bin

	cp launcher/nixlabs-discord-helper.js build/deb/usr/lib/$(APP_NAME)/launcher.js
	chmod 0755 build/deb/usr/lib/$(APP_NAME)/launcher.js

	# Copy Desktop entry
	cp assets/$(APP_NAME).desktop build/deb/usr/share/applications/
	chmod 0644 build/deb/usr/share/applications/$(APP_NAME).desktop

	# Copy Scalable SVG icon
	cp assets/$(APP_NAME).svg build/deb/usr/share/icons/hicolor/scalable/apps/
	chmod 0644 build/deb/usr/share/icons/hicolor/scalable/apps/$(APP_NAME).svg

	# Copy Pristine multi-resolution PNG icons (16, 24, 32, 48, 64, 128, 256, 512)
	for s in 16 24 32 48 64 128 256 512; do \
		mkdir -p build/deb/usr/share/icons/hicolor/$${s}x$${s}/apps; \
		cp assets/icons/$${s}x$${s}/$(APP_NAME).png build/deb/usr/share/icons/hicolor/$${s}x$${s}/apps/$(APP_NAME).png; \
		chmod 0644 build/deb/usr/share/icons/hicolor/$${s}x$${s}/apps/$(APP_NAME).png; \
	done

	@echo "==> Building $(DEB_NAME)..."
	dpkg-deb --build --root-owner-group build/deb $(DEB_NAME)
	@echo "==> Successfully created $(DEB_NAME)"

install: deb
	@echo "==> Installing $(DEB_NAME)..."
	sudo dpkg -i $(DEB_NAME)

clean:
	rm -rf frontend/dist backend/nixlabs-discord-helper-bin backend/cmd/nixlabs-discord-helper/dist build $(DEB_NAME)
