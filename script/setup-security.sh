#!/bin/bash
# setup-security.sh

echo "🔐 Configuration de la sécurité APKM..."

# Créer la structure de répertoires (un par un, sans {})
mkdir -p /usr/local/share/apkm
mkdir -p /usr/local/share/apkm/PROTOCOLE
mkdir -p /usr/local/share/apkm/PROTOCOLE/security
mkdir -p /usr/local/share/apkm/PROTOCOLE/security/keys
mkdir -p /usr/local/share/apkm/PROTOCOLE/security/tokens
mkdir -p /usr/local/share/apkm/PROTOCOLE/security/signatures
mkdir -p /usr/local/share/apkm/PROTOCOLE/security/cache
mkdir -p /usr/local/share/apkm/PROTOCOLE/repository
mkdir -p /usr/local/share/apkm/PROTOCOLE/metadata

# Créer le fichier de configuration de sécurité
cat > /usr/local/share/apkm/PROTOCOLE/security/APKM.apkm << 'EOF'
# APKM Security Configuration
# Format: key = value
# DO NOT EDIT MANUALLY

[security]
version = 1.0
last_update = $(date -Iseconds)
algorithm = sha256

[paths]
token_cache = /usr/local/share/apkm/PROTOCOLE/security/tokens/.config.cfg
keyring = /usr/local/share/apkm/PROTOCOLE/security/keys
signatures = /usr/local/share/apkm/PROTOCOLE/security/signatures

[repository]
url = https://github.com/gopu-inc/apkm-gest
branch = master
metadata = DATA.db
EOF

echo "✅ Structure de sécurité créée"
