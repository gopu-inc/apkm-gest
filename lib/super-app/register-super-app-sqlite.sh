#!/bin/sh
echo "📝 Enregistrement de super-app avec SQLite..."

# Installer sqlite si nécessaire
which sqlite3 >/dev/null || apk add sqlite

DB_PATH="/var/lib/apkm/packages.db"

# Créer la table et enregistrer
sqlite3 "$DB_PATH" << SQL
CREATE TABLE IF NOT EXISTS packages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT UNIQUE,
    version TEXT,
    install_date INTEGER,
    binary_path TEXT,
    manifest TEXT
);

INSERT OR REPLACE INTO packages (name, version, install_date, binary_path)
VALUES ('super-app', '2.1.0', strftime('%s', 'now'), '/usr/local/bin/super-app');

SELECT * FROM packages;
.quit
SQL

echo "✅ super-app enregistré dans la base SQLite"
