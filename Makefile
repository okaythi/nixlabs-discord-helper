APP_NAME := nixlabs-discord-helper
VERSION := 1.0.14
ARCH := amd64
DEB_NAME := $(APP_NAME)_$(VERSION)_$(ARCH).deb

.PHONY: all frontend backend icons deb windows clean install

all: frontend backend icons

frontend:
	@echo "==> Building React frontend with Vite..."
	cd frontend && npm run build

backend: frontend
	@echo "==> Syncing frontend dist to backend..."
	rm -rf backend/cmd/nixlabs-discord-helper/dist
	cp -r frontend/dist backend/cmd/nixlabs-discord-helper/dist
	@echo "==> Compiling Go binary..."
	cd backend && CGO_ENABLED=0 go build -ldflags="-s -w" -o nixlabs-discord-helper-bin ./cmd/nixlabs-discord-helper

icons:
	@echo "==> Checking raster icons for tray & desktop..."
	@which gjs >/dev/null 2>&1 && gjs assets/generate_icons.js || echo "gjs not found; using existing raster icons"

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

windows: frontend
	@echo "==> Building Windows backend binary..."
	rm -rf backend/cmd/nixlabs-discord-helper/dist
	cp -r frontend/dist backend/cmd/nixlabs-discord-helper/dist
	cd backend && CGO_ENABLED=0 GOOS=windows GOARCH=amd64 go build -ldflags="-s -w -H=windowsgui" -o ../nixlabs-discord-helper-bin.exe ./cmd/nixlabs-discord-helper
	@echo "==> Generating multi-resolution Windows 11 .ico and compiling resources..."
	python3 windows/generate_ico.py
	x86_64-w64-mingw32-windres windows/resource.rc -O coff -o windows/resource.o
	@echo "==> Building native Windows 11 Direct2D app..."
	x86_64-w64-mingw32-g++-posix -O2 -s -std=c++17 -fno-rtti -ffunction-sections -fdata-sections -municode \
		windows/main.cpp windows/api_client.cpp windows/quests_engine.cpp windows/ui_renderer.cpp windows/ui_views.cpp windows/shell_integration.cpp windows/image_cache.cpp windows/resource.o \
		-o nixlabs-discord-helper.exe \
		-Wl,--gc-sections -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic \
		-ld2d1 -ldwrite -ldwmapi -lwinhttp -lgdi32 -lshell32 -lole32 -luuid -lpropsys -lwindowscodecs -mwindows

install: deb
	@echo "==> Installing $(DEB_NAME)..."
	sudo dpkg -i $(DEB_NAME)

clean:
	rm -rf frontend/dist backend/nixlabs-discord-helper-bin backend/cmd/nixlabs-discord-helper/dist build $(DEB_NAME) *.exe
