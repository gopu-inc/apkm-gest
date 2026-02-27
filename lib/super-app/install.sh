#!/bin/sh
echo "📦 Installation de super-app..."

# Copier le binaire
mkdir -p /usr/local/bin
cp super-app /usr/local/bin/
chmod 755 /usr/local/bin/super-app

# Créer la documentation
mkdir -p /usr/local/share/doc/super-app
cat > /usr/local/share/doc/super-app/README << 'DOC'
super-app - The Ultimate Application
Version: 2.1.0
License: GPL-3.0

Pour plus d'informations: super-app --help
DOC

echo "✅ Installation terminée!"
echo "👉 Tapez 'super-app' pour lancer l'application"
