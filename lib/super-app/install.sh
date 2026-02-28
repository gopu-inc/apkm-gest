#!/bin/sh
set -e

# Le nom du module est fixé ou récupéré du contexte
MODULE_NAME="super-app"
echo "📦 Installation du module: $MODULE_NAME"
echo "📂 Répertoire courant: $(pwd)"

# Lister les fichiers pour debug
echo "📋 Fichiers disponibles:"
ls -la

# Chercher le binaire à différents endroits
BINARY_FOUND=0

# 1. Chercher dans le répertoire courant
if [ -f "$MODULE_NAME" ]; then
    echo "  • Installation depuis le répertoire courant"
    cp "$MODULE_NAME" /usr/local/bin/
    chmod 755 "/usr/local/bin/$MODULE_NAME"
    BINARY_FOUND=1
fi

# 2. Chercher dans usr/bin/
if [ -f "usr/bin/$MODULE_NAME" ]; then
    echo "  • Installation depuis usr/bin/"
    cp "usr/bin/$MODULE_NAME" /usr/local/bin/
    chmod 755 "/usr/local/bin/$MODULE_NAME"
    BINARY_FOUND=1
fi

# 3. Chercher dans usr/local/bin/
if [ -f "usr/local/bin/$MODULE_NAME" ]; then
    echo "  • Installation depuis usr/local/bin/"
    cp "usr/local/bin/$MODULE_NAME" /usr/local/bin/
    chmod 755 "/usr/local/bin/$MODULE_NAME"
    BINARY_FOUND=1
fi

# 4. Chercher dans bin/
if [ -f "bin/$MODULE_NAME" ]; then
    echo "  • Installation depuis bin/"
    cp "bin/$MODULE_NAME" /usr/local/bin/
    chmod 755 "/usr/local/bin/$MODULE_NAME"
    BINARY_FOUND=1
fi

# 5. Chercher n'importe quel exécutable qui ressemble
if [ $BINARY_FOUND -eq 0 ]; then
    echo "🔍 Recherche d'exécutables..."
    for file in *; do
        if [ -f "$file" ] && [ -x "$file" ] && [ "$file" != "install.sh" ]; then
            echo "  • Trouvé: $file"
            cp "$file" "/usr/local/bin/$MODULE_NAME"
            chmod 755 "/usr/local/bin/$MODULE_NAME"
            BINARY_FOUND=1
            break
        fi
    done
fi

if [ $BINARY_FOUND -eq 1 ]; then
    echo "✅ Binaire installé dans /usr/local/bin/$MODULE_NAME"
    
    # Vérification
    if [ -f "/usr/local/bin/$MODULE_NAME" ]; then
        echo "✅ Vérification: /usr/local/bin/$MODULE_NAME est présent"
        ls -la "/usr/local/bin/$MODULE_NAME"
        echo ""
        echo "👉 Pour tester: $MODULE_NAME --version"
    fi
else
    echo "❌ Échec de l'installation du binaire"
    exit 1
fi

# Installation des includes s'ils existent
if [ -d "usr/include" ]; then
    echo "  • Installation des headers"
    mkdir -p /usr/local/include
    cp -r usr/include/* /usr/local/include/ 2>/dev/null || true
fi

# Installation des libs si elles existent
if [ -d "usr/lib" ]; then
    echo "  • Installation des librairies"
    mkdir -p /usr/local/lib
    cp -r usr/lib/*.so* /usr/local/lib/ 2>/dev/null || true
    cp -r usr/lib/*.a /usr/local/lib/ 2>/dev/null || true
    ldconfig 2>/dev/null || true
fi

echo ""
echo "✅ Module $MODULE_NAME installé avec succès!"
