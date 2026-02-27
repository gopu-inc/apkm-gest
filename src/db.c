#include <sqlite3.h>
#include <stdio.h>
#include "apkm.h"

int apkm_db_init() {
    sqlite3 *db;
    int rc = sqlite3_open("/var/lib/apk/packages.db", &db);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Impossible d'ouvrir la DB APKM\n");
        return -1;
    }

    // Création de la table des paquets avec support Rollback
    const char *sql = "CREATE TABLE IF NOT EXISTS installed_pkgs ("
                      "id INTEGER PRIMARY KEY,"
                      "name TEXT UNIQUE,"
                      "version TEXT,"
                      "install_date DATETIME DEFAULT CURRENT_TIMESTAMP);";
    
    sqlite3_exec(db, sql, NULL, 0, NULL);
    sqlite3_close(db);
    return 0;
}

