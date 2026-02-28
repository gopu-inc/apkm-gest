
# APKM - Advanced Package Manager

APKM is a powerful package manager for Alpine Linux with GitHub integration, secure token management, and easy package publishing.

## Features

- 📦 Install packages from local `.tar.bool` files
- 🌐 Install packages directly from GitHub Releases
- 🔐 Secure token storage with BTSCRYPT encryption
- 📤 Publish packages to GitHub Releases with `apsm`
- 🏗️ Build packages with `bool` from APKMBUILD
- 📚 Automatic documentation inclusion in releases
- 🔏 SHA256 signature verification
- 📊 Package database management

## Installation

```bash
# Install apkm itself
apkm install apkm@2.0.0

# Or build from source
git clone https://github.com/gopu-inc/apkm-gest
cd apkm-gest
make
sudo make install
```

## Usage

```bash
# Install a local package
apkm install package-v1.0.0-r1.x86_64.tar.bool

# Install from GitHub (auto-detects architecture)
apkm install package@1.0.0

# List installed packages
apkm list

# Sync Alpine database
apkm sync

# Security audit
apkm audit
```

## **Building Packages**

**Create an APKMBUILD file:**

```bash
$APKNAME::myapp
$APKMVERSION::1.0.0
$APKMRELEASE::r1
$APKMARCH::x86_64
$APKMMAINT::Your Name <email@example.com>
$APKMDESC::My awesome application
$APKMLICENSE::MIT
$APKMDEP:: gcc; make
$APKMPATH::install.sh
$APKMMAKE:: make
$APKMINSTALL:: make install DESTDIR="$DESTDIR"
```

## **Then build:**

```bash
bool --build
```

## **Publishing Packages**

**1. Authenticate with GitHub:**

```bash
apsm auth ghp_your_token_here
```

**1. Publish your package:**

```bash
apsm push build/myapp-v1.0.0-r1.x86_64.tar.bool
```

**1. Your package is now available at:**

```
https://github.com/gopu-inc/apkm-gest/releases/tag/v1.0.0-r1
```

## License

MIT License - Copyright (c) 2026 Gopu.inc
