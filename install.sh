#!/bin/sh
# install.sh for apkm package

echo "📦 Installing APKM package manager..."

# Create directories
mkdir -p /usr/local/bin
mkdir -p /usr/local/share/apkm
mkdir -p /usr/local/share/doc/apkm
mkdir -p /etc/apkm

# Copy binaries
if [ -f "usr/local/bin/apkm" ]; then
    cp usr/local/bin/apkm /usr/local/bin/
    chmod 755 /usr/local/bin/apkm
    echo "✅ apkm installed"
fi

if [ -f "usr/local/bin/apsm" ]; then
    cp usr/local/bin/apsm /usr/local/bin/
    chmod 755 /usr/local/bin/apsm
    echo "✅ apsm installed"
fi

if [ -f "usr/local/bin/bool" ]; then
    cp usr/local/bin/bool /usr/local/bin/
    chmod 755 /usr/local/bin/bool
    echo "✅ bool installed"
fi

# Copy documentation
if [ -d "usr/local/share/doc/apkm" ]; then
    cp -r usr/local/share/doc/apkm/* /usr/local/share/doc/apkm/ 2>/dev/null || true
fi

# Copy includes if any
if [ -d "usr/local/include/apkm" ]; then
    mkdir -p /usr/local/include/apkm
    cp -r usr/local/include/apkm/* /usr/local/include/apkm/ 2>/dev/null || true
fi

if [ -d "docs" ]; then
    mkdir /usr/local/include/apkm/docs
    cp README.md DOCTEXT.txt /usr/local/include/apkm/docs
fi
echo ""
echo "✅ APKM installation complete!"
echo ""
echo "📋 Commands available:"
echo "   apkm  - package manager"
echo "   apsm  - GitHub publisher"
echo "   bool  - package builder"
echo ""
echo "👉 Try: apkm --help"
