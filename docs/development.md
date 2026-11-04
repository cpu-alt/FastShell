# Développer FastShell

Retour à la [présentation](../README.md).

## Compiler sous Windows (MSYS2)

Installer [MSYS2](https://www.msys2.org), puis dans le terminal **UCRT64** :

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,qt6-base,qt6-serialport,qt6-activeqt,libssh,openssl,gtest}
```

`qt6-activeqt` sert au bureau à distance dans les onglets ; sans lui, la compilation passe et
les sessions RDP s'ouvrent dans mstsc.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

Le build doit finir **sans aucun warning** (`-Wall -Wextra -Wpedantic`).

## Tests

```bash
./build/tests/fastshell_tests.exe
```

```bash
./build/tests/fastshell_session_tests.exe
```

- `fastshell_tests` : cœur, analyseur de terminal (dont des entrées aléatoires et hostiles),
  Telnet, série, login SSH contre un petit serveur libssh local, benchmark de l'analyseur
  (~100 Mio/s attendus).
- `fastshell_session_tests` : sessions, chiffrement des exports, import de fichiers hostiles,
  connexion rapide, vérification des mises à jour.

## Paquet Windows

```bash
bash tools/package.sh build package
```

Crée `package/FastShell-<version>-windows-x64.zip` (Qt, libssh, OpenSSL et les DLL d'exécution
inclus) et son fichier `.sha256`. Pour l'essayer sans toucher à son installation :

```powershell
.\install.ps1 -Package .\package\FastShell-1.0.2-windows-x64.zip -InstallDir $env:TEMP\fs-test -FilesOnly
```

## Publier une version

Tout est automatique avec GitHub Actions (`.github/workflows/build.yml`) : chaque push est
compilé et testé, et un tag `vX.Y.Z` publie la release avec le paquet et son empreinte.

1. Mettre le nouveau numéro dans `CMakeLists.txt` (`VERSION 1.1.0`).
2. Commiter et pousser sur `main`, attendre que le build soit vert.
3. Créer et pousser le tag (il doit être identique à la version, sinon le build refuse) :

   ```bash
   git tag v1.1.0
   ```

   ```bash
   git push origin v1.1.0
   ```

4. La release apparaît dans l'onglet *Releases* du dépôt. La commande d'installation et le
   bouton « Check for updates » de l'application la proposent aussitôt.

## Organisation du code

```
src/core/      buffer circulaire, ScopeGuard, SpinMutex, chiffrement (AES-256-GCM), protocole Telnet
src/terminal/  analyseur VT/xterm, modèle d'écran, largeur des caractères (sans interface)
src/pty/       ConPty (Windows) et pty POSIX
src/net/       SSH (libssh), SFTP pipeliné, Telnet, série
src/widgets/   ScrollBar, Splitter, PopupWidget (Qt 6)
src/app/       fenêtre, panneaux, rendu, clavier, sessions, mises à jour
tests/         tests GoogleTest et benchmark
tools/         package.sh (paquet de release), make_icon.cpp (icône depuis Logo.h)
install.ps1    installation / mise à jour / désinstallation en une ligne
```

## Performances

L'analyseur de séquences traite environ **100 Mio/s** de sortie de terminal mixte (texte,
couleurs, UTF-8). Le texte ASCII est copié par blocs, le défilement déplace les lignes sans les
copier, l'affichage suit la fréquence de l'écran et les gros volumes sont traités par tranches
de 1 Mio pour que l'interface reste réactive. Les versions Release utilisent la LTO.

## Linux / macOS

Le code pty POSIX existe mais n'a jamais été compilé ni testé : seul Windows est pris en charge
pour l'instant.
