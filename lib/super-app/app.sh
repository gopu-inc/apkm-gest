#!/bin/bash
# publish-to-github.sh

TOKEN="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"
REPO="gopu-inc/apkm-gest"
VERSION="v1.0.0"
PACKAGE="build/super-app-v1.0.0-r1.x86_64.tar.bool"
MANIFEST="build/super-app.manifest"
SHA256_FILE="build/super-app-v1.0.0-r1.x86_64.tar.bool.sha256"

echo "🚀 Publication de super-app sur GitHub..."
echo "📦 Dépôt: $REPO"
echo "🏷️  Version: $VERSION"

# 1. Créer la release
echo "📦 Création de la release $VERSION..."
RELEASE_DATA=$(curl -s -X POST \
  -H "Authorization: token $TOKEN" \
  -H "Accept: application/vnd.github.v3+json" \
  https://api.github.com/repos/$REPO/releases \
  -d "{
    \"tag_name\": \"$VERSION\",
    \"target_commitish\": \"main\",
    \"name\": \"super-app $VERSION\",
    \"body\": \"Release de super-app version $VERSION\n\nPackage: super-app\nVersion: 1.0.0\nArchitecture: x86_64\",
    \"draft\": false,
    \"prerelease\": false
  }")

# Extraire l'URL d'upload
UPLOAD_URL=$(echo "$RELEASE_DATA" | grep -o '"upload_url": "[^"]*' | cut -d'"' -f4 | sed 's/{?name,label}//')

if [ -z "$UPLOAD_URL" ]; then
    echo "❌ Échec de la création de la release"
    echo "$RELEASE_DATA"
    exit 1
fi

echo "✅ Release créée: $UPLOAD_URL"

# 2. Upload du package
echo "📤 Upload du package ($(du -h $PACKAGE | cut -f1))..."
curl -s -X POST \
  -H "Authorization: token $TOKEN" \
  -H "Content-Type: application/octet-stream" \
  --data-binary @"$PACKAGE" \
  "$UPLOAD_URL?name=$(basename $PACKAGE)" | grep -q "browser_download_url" && echo "✅ Package uploadé" || echo "⚠️  Vérification..."

# 3. Upload du manifeste
echo "📄 Upload du manifeste..."
curl -s -X POST \
  -H "Authorization: token $TOKEN" \
  -H "Content-Type: text/plain" \
  --data-binary @"$MANIFEST" \
  "$UPLOAD_URL?name=$(basename $MANIFEST)" > /dev/null && echo "✅ Manifeste uploadé"

# 4. Upload du fichier SHA256
echo "🔏 Upload de la signature SHA256..."
curl -s -X POST \
  -H "Authorization: token $TOKEN" \
  -H "Content-Type: text/plain" \
  --data-binary @"$SHA256_FILE" \
  "$UPLOAD_URL?name=$(basename $SHA256_FILE)" > /dev/null && echo "✅ Signature uploadée"

echo ""
echo "🎉 Publication terminée!"
echo "📦 Voir la release: https://github.com/$REPO/releases/tag/$VERSION"

