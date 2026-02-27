#!/bin/sh
echo "📦 Installation de super-app..."

# Vérifier si on a les permissions
if [ "$(id -u)" != "0" ]; then
    echo "❌ Ce script doit être exécuté en tant que root"
    echo "👉 Utilisez: sudo apkm install ..."
    exit 1
fi

# Installer le binaire
echo "  • Copie de super-app vers /usr/local/bin/"
cp usr/local/bin/super-app /usr/local/bin/
chmod 755 /usr/local/bin/super-app

# Installer la documentation
echo "  • Installation de la documentation"
mkdir -p /usr/local/share/doc/super-app
cp -r usr/local/share/doc/super-app/* /usr/local/share/doc/super-app/ 2>/dev/null || true

echo ""
echo "✅ super-app installé avec succès!"
echo ""
echo "📋 Pour utiliser super-app:"
echo "   super-app --help"
echo "   super-app --version"
echo "   super-app --test"
echo ""
echo "📚 Documentation: /usr/local/share/doc/super-app/README"
