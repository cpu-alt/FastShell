# FastShell — notes pour Claude

Terminal SSH / Telnet / série / RDP / local (Windows d'abord), en C++20 + Qt 6. Crédits :
Nathan Witz & Patrizio Ventura. Dépôt public : https://github.com/cpu-alt/FastShell.

Quelques composants (core, pty, widgets) dérivent d'un projet Apache-2.0 : **garder** `LICENSE`,
`NOTICE` et les en-têtes « Copyright 2020 WindTerm » des fichiers concernés (obligation légale),
mais ne plus mentionner ce projet ailleurs (UI, README, noms). L'app s'est appelée « EMF Hexa » :
quelques identifiants internes gardent ce nom exprès (format d'export `emfhexa-sessions`, types
MIME glisser-déposer, organisation QSettings `EMF NET`) pour rester compatibles.

L'utilisateur parle français : répondre en français, simplement. Les textes de l'interface
sont en anglais (via `tr()`), le README et la doc sont en français.

**Ne jamais commiter, pousser, créer de tag ni publier de release** : l'utilisateur le fait.
Ne jamais travailler dans un dossier OneDrive.

## Compiler et tester

La chaîne d'outils est MSYS2 UCRT64 (`C:\msys64`), à mettre en tête du PATH dans Bash :

```bash
export PATH=/c/msys64/ucrt64/bin:$PATH
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # une seule fois
cmake --build build                                        # doit finir sans warning
./build/tests/fastshell_tests.exe            # terminal, telnet, série, login SSH, sécurité, benchmark
./build/tests/fastshell_session_tests.exe    # sessions, import/export, sécurité appli, mises à jour
```

- Le build doit rester **sans aucun warning** (`-Wall -Wextra -Wpedantic`, dont `-Wbidi-chars`),
  hors le message `lto-wrapper` de LTO.
- Le benchmark de l'analyseur doit rester vers ~100 Mio/s (le test exige seulement > 20).
- Paquets MSYS2 : gcc, cmake, ninja, qt6-base, qt6-serialport, qt6-activeqt (RDP dans les
  onglets, `FASTSHELL_HAVE_AXRDP`, lié en statique), libssh (0.12), openssl, gtest.
- Version : `VERSION` dans `CMakeLists.txt` (1.0.1), injectée dans l'exe (`assets/fastshell.rc.in`).

## Livrer dans `dist/`

`dist/` (hors git) est la version autonome que l'utilisateur lance. Après **chaque build réussi** :
fermer FastShell s'il tourne (ou un ancien EMFHexa.exe), puis :

```bash
bash tools/package.sh build "$SCRATCH/package"     # paquet complet (Qt + libssh + OpenSSL + runtime)
cp -r "$SCRATCH/package/FastShell/." dist/
```

Ne **jamais** créer de `*.old.exe`. Une instance lancée en administrateur ne peut pas être fermée :
demander à l'utilisateur de la fermer.

## Releases, installation, mises à jour

- `.github/workflows/build.yml` : build + tests à chaque push ; un tag `vX.Y.Z` (égal à `VERSION`)
  publie la release avec `FastShell-X.Y.Z-windows-x64.zip` + `.sha256` (via `tools/package.sh`).
- `install.ps1` : `irm https://raw.githubusercontent.com/cpu-alt/FastShell/main/install.ps1 | iex`.
  Options : `-Package zip` (local), `-InstallDir`, `-FilesOnly` (sans raccourci ni registre :
  **toujours** l'utiliser pour tester), `-NoStart`, `-Uninstall`.
- `src/app/UpdateCheck.*` (logique testée) + `Updater.*` (GUI) : vérif. quotidienne, HTTPS,
  liens `github.com/cpu-alt/FastShell/releases/download/` seulement, SHA-256, pas de downgrade.
- Procédure de publication pour l'utilisateur : `docs/development.md`.

## Organisation

```
src/core/      CircularBuffer, ScopeGuard, SpinMutex, Cryptographic (AES-256-GCM), Telnet, Backend
src/terminal/  VtParser (xterm), Screen (grille + historique + modes), CharWidth — sans GUI, testés
src/pty/       ConPty (Windows), UnixPty (écrit, jamais compilé ni testé)
src/net/       SshCommon (connexion+auth partagées), SshBackend, SftpClient, TelnetBackend, SerialBackend
src/widgets/   ScrollBar, Splitter, PopupWidget
src/app/       MainWindow, TabPage, TerminalView, SessionStore (JSON + DPAPI, import/export),
               SessionPanel, SessionDialog, ManageDialogs, FilerPanel, Panels, Logo.h, Icons.h,
               Style (QSS, icônes, chevrons générés), DropPreview, RdpView, TextSafety.h
               (titres, collage, noms de fichiers SFTP), UpdateCheck, Updater, main.cpp
tests/         GoogleTest (dont test_security, test_app_security, test_update, test_ssh_login)
tools/         package.sh (paquet de release), make_icon.cpp (assets/fastshell.ico + docs/logo.png)
docs/          guide.md, development.md, security-audit.md, screenshot.png
```

Données utilisateur : `%APPDATA%\EMF NET\FastShell\sessions.json` (+ QSettings sous
`HKCU\Software\EMF NET\FastShell`). `main.cpp` reprend une fois les données d'`EMF NET\EMFHexa`.
**Mode portable** : `FastShell.exe --data-dir DOSSIER` (sessions + `.ini` dans ce dossier).

## Choix voulus par l'utilisateur

- **Login SSH comme MobaXterm / PuTTY** : jamais de clé automatique (pas d'agent, pas de
  `~/.ssh`, pas de `~/.ssh/config`) ; seule la clé choisie dans la session ou l'utilisateur
  partagé sert. Sans utilisateur/mot de passe, `login as:` puis `user@host's password:` **dans le
  terminal** (`SshBackend::readLine`), 3 essais, « Access denied ». Pas de dialogue pour le login.
  Le SFTP (Filer) attend la fin du login (`SshBackend::isLoggedIn`) et réutilise ce login.
- **Vieux équipements** : repli SHA-1/CBC automatique, info dans l'infobulle de l'onglet et la
  barre d'état (pas de ligne jaune dans le terminal) ; Filer sans SFTP : « Pas de SFTP sur cet appareil ».
- **Copier-coller** : sélectionner copie, Ctrl+V / clic droit collent, Ctrl+C copie s'il y a une
  sélection. Collage multi-lignes : confirmation (désactivable, menu Edit).
- **Icône du système** par session (`SessionInfo::os`, `icons::systems()`), bouton à côté du nom.
- **Fenêtre de session compacte** : champs liés sur une même ligne (`SessionDialog::line`).
- **Profils d'équipement** : Cisco = 9600 8N1, FS = 115200 8N1, sans contrôle de flux ; ils ne
  touchent qu'au **série**. Retour arrière = DEL ; bascule ^H par onglet (clic droit).
- **Mots de passe** : jamais en clair (DPAPI) ; export AES-256-GCM, PBKDF2 600 000 itérations.
- **Logo** : hexagone en tuile 3D avec `>` et curseur. Après modification de `Logo.h`, relancer
  `tools/make_icon assets/fastshell.ico docs/logo.png`.

## Pièges rencontrés

- **Échappements** : `sed`/`perl` sur du C++ ont cassé `\n`, `\x1b`, `${PROJECT_SOURCE_DIR}` ; et
  l'outil Write a transformé `\u202E` en vrai caractère invisible (→ warning `-Wbidi-chars`).
  Pour les caractères spéciaux, écrire `QChar(0x202E)` ; vérifier avec `grep`/`cat -A`.
- **ConPty et tailles transitoires** : `TerminalView` ignore les tailles < 10×3 et n'envoie la
  taille au shell qu'après 60 ms. Les nouveaux panneaux démarrent via `QTimer::singleShot(0, ...)`.
- **Focus après reparentage** dans un QSplitter : redonner le focus après la mise en page.
- **Glisser-déposer des panneaux** : c'est `TabPage` qui reçoit les dépôts ; toujours accepter
  le DragEnter. **QTreeWidget en InternalMove** : dépôt externe en `CopyAction`.
- **QSS** : les sous-contrôles (flèches de combo/spinbox, défilement des onglets) doivent être
  stylés explicitement, sinon Qt dessine des blocs carrés qui dépassent des coins arrondis.
- **Historique** : anneau arrondi à la puissance de 2 supérieure (100 → 128 lignes).
- **Windows PowerShell 5.1** ignore les touches tapées avant la fin de son chargement.

## Tester l'interface sans gêner l'utilisateur

**Ne pas simuler de touches** (elles partent vers la fenêtre au premier plan). Préférer :

- sa propre instance en **mode portable** (`--data-dir` dans le scratchpad) pour ne jamais toucher
  aux sessions et réglages de l'utilisateur, avec des cibles en ligne de commande
  (`--side-by-side local local`, `admin@10.0.0.1`, `"rdp hôte"`) ;
- une capture par `PrintWindow` de **sa propre** instance (`SetProcessDPIAware()`), puis la fermer
  par son PID. `QDir::homePath()` ignore `USERPROFILE` : le Filer montre le vrai dossier perso
  (le recadrer avant toute capture publique) ;
- pour une boîte de dialogue seule : un petit programme dans le scratchpad qui la crée en
  `QStandardPaths::setTestModeEnabled(true)` et l'enregistre avec `grab()`.

## Non testé à ce jour

Bureau à distance sur un vrai serveur RDP (le contrôle se charge), SFTP complet sur un vrai
serveur, port série réel, UnixPty, la mise à jour automatique de bout en bout (aucune release
publiée), le workflow GitHub Actions (jamais exécuté), et plusieurs interactions souris. Le dire
quand c'est pertinent.
