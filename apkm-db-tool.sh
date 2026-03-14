#!/bin/sh
DB="/usr/local/share/apkm/database/packages.db"

case "$1" in
    list)
        sqlite3 -column -header "$DB" \
            "SELECT name, version || '-' || release as version, 
                    size/1024.0 || ' KB' as size,
                    datetime(install_date,'unixepoch') as installed 
             FROM installed_packages ORDER BY name;"
        ;;
    size)
        sqlite3 "$DB" "SELECT printf('Total: %.2f MB', SUM(size)/1024.0/1024.0) FROM installed_packages;"
        ;;
    search)
        if [ -z "$2" ]; then
            echo "Usage: $0 search <package>"
            exit 1
        fi
        sqlite3 -column -header "$DB" \
            "SELECT * FROM installed_packages WHERE name LIKE '%$2%';"
        ;;
    info)
        if [ -z "$2" ]; then
            echo "Usage: $0 info <package>"
            exit 1
        fi
        sqlite3 "$DB" "SELECT * FROM installed_packages WHERE name = '$2';"
        ;;
    recent)
        sqlite3 -column -header "$DB" \
            "SELECT name, version, datetime(install_date,'unixepoch') as installed 
             FROM installed_packages 
             ORDER BY install_date DESC LIMIT 5;"
        ;;
    stats)
        echo "📊 Database Statistics"
        echo "====================="
        echo "Total packages: $(sqlite3 "$DB" "SELECT COUNT(*) FROM installed_packages;")"
        echo "Total size: $(sqlite3 "$DB" "SELECT printf('%.2f MB', SUM(size)/1024.0/1024.0) FROM installed_packages;")"
        echo "Unique repositories: $(sqlite3 "$DB" "SELECT COUNT(DISTINCT repository) FROM installed_packages;")"
        ;;
    tables)
        sqlite3 "$DB" ".tables"
        ;;
    schema)
        sqlite3 "$DB" ".schema $2"
        ;;
    *)
        echo "Usage: $0 {list|size|search|info|recent|stats|tables|schema}"
        exit 1
        ;;
esac
