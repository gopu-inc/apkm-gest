#!/bin/bash
# install-security.sh

echo "🔧 Installation du système de sécurité APKM..."

# Créer la structure (un dossier à la fois)
echo "📁 Création des répertoires..."
mkdir -p /usr/local/share/apkm
mkdir -p /usr/local/share/apkm/PROTOCOLE
mkdir -p /usr/local/share/apkm/PROTOCOLE/security
mkdir -p /usr/local/share/apkm/PROTOCOLE/security/keys
mkdir -p /usr/local/share/apkm/PROTOCOLE/security/tokens
mkdir -p /usr/local/share/apkm/PROTOCOLE/security/signatures
mkdir -p /usr/local/share/apkm/PROTOCOLE/security/cache
mkdir -p /usr/local/share/apkm/PROTOCOLE/repository
mkdir -p /usr/local/share/apkm/PROTOCOLE/metadata

# Configurer les permissions
chmod 755 /usr/local/share/apkm
chmod 755 /usr/local/share/apkm/PROTOCOLE
chmod 755 /usr/local/share/apkm/PROTOCOLE/security
chmod 700 /usr/local/share/apkm/PROTOCOLE/security/tokens
chmod 700 /usr/local/share/apkm/PROTOCOLE/security/keys

# Télécharger le token initial
echo "📥 Téléchargement du token de sécurité..."
wget -q -O /usr/local/share/apkm/PROTOCOLE/security/tokens/.config.cfg \
    https://raw.githubusercontent.com/gopu-inc/apkm-gest/master/.config.cfg 2>/dev/null || \
curl -s -o /usr/local/share/apkm/PROTOCOLE/security/tokens/.config.cfg \
    https://raw.githubusercontent.com/gopu-inc/apkm-gest/master/.config.cfg

chmod 600 /usr/local/share/apkm/PROTOCOLE/security/tokens/.config.cfg

echo "✅ Sécurité installée!"
echo ""
echo "📋 Vérification:"
ls -la /usr/local/share/apkm/PROTOCOLE/security/tokens/
