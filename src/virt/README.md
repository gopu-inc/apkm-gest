# ANV - Advanced Namespace Virtualization

ANV crée des environnements virtuels ultra-sécurisés pour exécuter APKM, BOOL et APSM en isolation complète.

## Caractéristiques

- 🔒 **Isolation totale** via namespaces Linux
- 🛡️ **Détection root** avec message [track backsh >_< root no secure]
- 🏷️ **NamesBar** - Shell sécurisé avec prompt modifié [::nom_env::]user@host:~$
- 📦 **Support APKM/BOOL/APSM** intégré
- 🚫 **Pas de root** - Fonctionne uniquement avec utilisateur standard
- 🏗️ **Niveaux de sécurité** (0-4) configurables
- 🔐 **No new privs** et drop de capabilities
- 🌐 **Isolation réseau** optionnelle

## Installation

```bash
make
sudo make install
```

Utilisation

```bash
# Créer un environnement pour APKM (niveau sécurité 3 - recommandé)
anv create apkm-dev 0 3

# Démarrer l'environnement
anv start apkm-dev

# Entrer dans l'environnement (prompt change en [::apkm-dev::]user@host:~$)
anv enter apkm-dev

# Tester APKM dans l'environnement sécurisé
apkm --version
apkm search nginx
apkm install nginx

# Sortir
exit

# Arrêter l'environnement
anv stop apkm-dev

# Lister les environnements
anv list

# Supprimer
anv delete apkm-dev
```

Niveaux de sécurité

· 0 - None: Aucune isolation
· 1 - Low: Mount namespace uniquement
· 2 - Medium: PID, UTS, IPC namespaces
· 3 - High: NET, USER, CGROUP namespaces + no_new_privs
· 4 - Paranoid: TIME namespace + seccomp

NamesBar

Le shell spécial __namesbar remplace automatiquement le prompt par:

```
[::nom_env::]user@host:~$
```

Sécurité

ANV refuse de s'exécuter en root pour éviter les escalades de privilèges:

```
[track backsh >_< root no secure]
```

Structure des fichiers

```
~/.anv/
├── apkm-dev/
│   ├── config      # Configuration
│   ├── pid         # PID du processus (si running)
│   └── rootfs/     # Root filesystem isolé
│       ├── bin/
│       ├── usr/bin/
│       ├── usr/bin/__namesbar
│       └── ...
```

Exemple complet

```bash
$ anv create secure-apkm 0 3
🔐 Création de l'environnement ANV: secure-apkm
✅ Environnement créé: /home/user/.anv/secure-apkm/rootfs
   Sécurité: niveau 3
   NamesBar: __namesbar

$ anv start secure-apkm
🚀 Démarrage de l'environnement: secure-apkm
✅ Environnement démarré avec PID: 12345

$ anv enter secure-apkm
🔐 Entrée dans l'environnement secure-apkm
[::secure-apkm::]user@host:~$ apkm search nginx
[::secure-apkm::]user@host:~$ exit
```

Licence

GPL-3.0

```

Ce système ANV (Advanced Namespace Virtualization) offre :

1. **Isolation complète** via namespaces Linux
2. **Détection root** avec message [track backsh >_< root no secure]
3. **NamesBar** - shell personnalisé avec prompt modifié
4. **Niveaux de sécurité** configurables (0-4)
5. **Support APKM/BOOL/APSM** intégré
6. **Structure de fichiers** propre dans `~/.anv/`
