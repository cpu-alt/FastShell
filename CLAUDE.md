# EMF Hexa — notes pour Claude

Terminal SSH / Telnet / série / local pour EMF NET (Windows d'abord), en C++20 + Qt 6.
Construit sur une refonte des composants open source de WindTerm (Apache-2.0). L'app
s'appelait « WindTerm NG » au début : le dossier, la cible CMake `windterm-ng` et les
macros `WTNG_*` gardent ce nom, c'est voulu. Le nom affiché et l'exe sont **EMF Hexa** /
`EMFHexa.exe`.

L'utilisateur parle français : répondre en français, simplement. Les textes de l'interface
sont en anglais (via `tr()`), le README est en français.

## Compiler et tester

La chaîne d'outils est MSYS2 UCRT64 (`C:\msys64`), à mettre en tête du PATH dans Bash :

```bash
export PATH=/c/msys64/ucrt64/bin:$PATH
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # une seule fois
cmake --build build                                        # doit finir sans warning
./build/tests/wtng_tests.exe            # cœur, terminal, telnet, série, benchmark
./build/tests/wtng_session_tests.exe    # sessions, import/export, connexion rapide
```

- Le build doit rester **sans aucun warning** (`-Wall -Wextra -Wpedantic`), hors le message
  `lto-wrapper` de LTO.
- Le benchmark de l'analyseur doit rester vers ~100 Mio/s. Mesurer avant de garder une
  « optimisation » : recycler la mémoire des lignes s'est révélé plus lent (cache froid).
- Paquets MSYS2 utilisés : gcc, cmake, ninja, qt6-base, qt6-serialport, libssh (0.12),
  gtest, openssl.

## Livrer dans `dist/`

`dist/` est la version autonome que l'utilisateur lance (`dist\EMFHexa.exe` + DLL Qt).

```bash
cp build/src/EMFHexa.exe dist/
windeployqt6 --release --no-translations --no-system-d3d-compiler --no-opengl-sw dist/EMFHexa.exe
```

Après un nouveau module Qt ou une nouvelle bibliothèque, recopier les DLL manquantes
(`ldd dist/... | grep ucrt64`). Si l'exe est verrouillé parce que l'utilisateur a l'app
ouverte, **ne jamais tuer son processus** : Windows permet de renommer un exe en cours
d'exécution (`EMFHexa.old.exe`) puis de copier le nouveau à côté.

## Organisation

```
src/core/      CircularBuffer, ScopeGuard, SpinMutex, Cryptographic (AES-256-GCM), Telnet, Backend
src/terminal/  VtParser (xterm), Screen (grille + historique + modes), CharWidth — sans GUI, testés
src/pty/       ConPty (Windows), UnixPty (écrit, jamais compilé ni testé)
src/net/       SshCommon (connexion+auth partagées), SshBackend, SftpClient (thread dédié,
               I/O async pipelinées), TelnetBackend, SerialBackend
src/widgets/   ScrollBar, Splitter, PopupWidget (portés de WindTerm)
src/app/       MainWindow, TabPage (splits/grille/broadcast), TerminalView (rendu, clavier,
               souris, en-tête de panneau, glisser-déposer), SessionStore (JSON + DPAPI,
               import/export, connexion rapide), SessionPanel, SessionDialog, ManageDialogs
               (utilisateurs partagés, profils/étiquettes), FilerPanel, Panels (Outline,
               Transfer, commandes rapides), Logo.h (logo dessiné en code), Icons.h, main.cpp
tests/         GoogleTest
tools/         make_icon.cpp : régénère assets/emfhexa.ico et docs/logo.png depuis Logo.h
../WindTerm/   clone d'origine de kingToolbox/WindTerm, à ne pas modifier (référence)
```

Données utilisateur : `%APPDATA%\EMF NET\EMFHexa\sessions.json` (+ QSettings sous
`HKCU\Software\EMF NET\EMFHexa`). Au démarrage, `main.cpp` reprend les données de l'ancien
nom (`WindTermNG`) tant qu'EMF Hexa n'a aucune session.

## Choix voulus par l'utilisateur

- **SSH comme PuTTY** : aucun réglage spécial imposé. Si un vieil équipement refuse les
  algorithmes modernes, `sshConnect` réessaie seul avec SHA-1/CBC ajoutés (préfixe `+`) ;
  `diffie-hellman-group1-sha1` seulement si l'option « Compatibility with old devices » est cochée.
- **Profils d'équipement / étiquettes** : Cisco = 9600 8N1, FS = 115200 8N1 (doc FS
  S3900/S5850), sans contrôle de flux ; ils ne touchent qu'au **série**, jamais au SSH.
  Retour arrière = DEL par défaut (comme PuTTY) ; bascule ^H par onglet (clic droit).
- **Mots de passe** : jamais en clair. DPAPI sur la machine ; à l'export, ré-chiffrés en
  AES-256-GCM avec un mot de passe d'export (3 niveaux : adresses seules / + utilisateurs /
  + mots de passe).
- **Logo** : hexagone EMF NET (pointe en haut, deux barres fines) en tuile 3D avec `>` et
  curseur. Après modification de `Logo.h`, relancer `tools/make_icon` puis `touch assets/emfhexa.rc`.

## Pièges rencontrés

- **Échappements via le shell** : les `perl -pi`/`sed` sur du C++ ont plusieurs fois
  transformé `\n`, `\x1b` ou `\u00B7` dans des chaînes en vrais caractères ou en texte cassé,
  et mangé `${PROJECT_SOURCE_DIR}`. Pour toute chaîne avec échappements, utiliser l'outil
  Edit, puis vérifier avec `grep`/`cat -A`.
- **ConPty et tailles transitoires** : pendant un split, un panneau passe par une taille
  minuscule et ConPty efface alors son contenu. `TerminalView` ignore les tailles < 10×3 et
  n'envoie la taille au shell qu'après 60 ms (`m_resizeTimer`). Les nouveaux panneaux
  démarrent via `QTimer::singleShot(0, ...)` une fois mis en page.
- **Focus après reparentage** : déplacer un widget focalisé dans un nouveau QSplitter lui
  fait perdre le focus ; donner le focus au nouveau panneau après la mise en page.
- **QTreeWidget en InternalMove** : un dépôt externe doit être accepté en `CopyAction`,
  sinon l'arbre supprime ses éléments.
- **Windows PowerShell 5.1** ignore les touches tapées avant la fin de son chargement :
  attendre ~5 s avant de taper dans un nouveau panneau pendant un test.

## Tester l'interface sans gêner l'utilisateur

L'utilisateur travaille sur ce PC. **Ne pas simuler de touches** (SendKeys/keybd_event) : elles
partent vers la fenêtre au premier plan, qui peut être la sienne (c'est déjà arrivé avec les
Paramètres Windows). Préférer :

- la ligne de commande de l'app : `EMFHexa.exe --side-by-side local local` ou
  `EMFHexa.exe admin@10.0.0.1` ;
- une capture par `PrintWindow` sur le handle de **sa propre** instance (avec
  `SetProcessDPIAware()` pour ne pas tronquer), puis fermer cette instance par son PID.

L'outil computer-use ne reconnaît pas EMFHexa.exe (pas dans le menu Démarrer).

## Non testé à ce jour

Connexion SSH/SFTP complète sur un vrai serveur (seule la négociation de clé a été validée
sur le switch A41-SW-01), port série réel, UnixPty, et plusieurs interactions souris
(glisser-déposer, boutons □/× des panneaux, menus contextuels, dialogues). Le dire quand
c'est pertinent plutôt que de supposer que ça marche.
