# Signature du code

Retour à la [présentation](../README.md).

## Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by
[SignPath Foundation](https://signpath.org).

- Seuls les programmes construits par **GitHub Actions** à partir de ce dépôt public
  ([`.github/workflows/build.yml`](../.github/workflows/build.yml)) sont signés, et seulement
  pour les versions publiées (tags `vX.Y.Z`). Aucun fichier compilé sur un poste personnel
  n'est envoyé à la signature.
- Seul `FastShell.exe` est signé. Les bibliothèques tierces livrées sans modification
  (Qt, libssh, OpenSSL, runtime MinGW-w64) gardent leur propre signature ou n'en ont pas :
  voir [NOTICE](../NOTICE).
- Chaque demande de signature est **approuvée à la main** sur SignPath par un approbateur.

### Rôles

| Rôle | Membres |
| --- | --- |
| Auteurs (committers) et relecteurs (reviewers) | [Patrizio Ventura (@Pati8099)](https://github.com/Pati8099), Nathan Witz |
| Approbateurs (approvers) | [Patrizio Ventura (@Pati8099)](https://github.com/Pati8099) |

Les comptes GitHub et SignPath des membres utilisent l'authentification à deux facteurs.

## Privacy policy

This program will not transfer any information to other networked systems unless specifically
requested by the user or the person installing or operating it.

En pratique :

- FastShell ne se connecte qu'aux équipements que l'utilisateur ouvre (SSH, Telnet, série,
  bureau à distance).
- **Mises à jour** : une fois par jour, FastShell demande à GitHub
  (`api.github.com/repos/cpu-alt/FastShell/releases/latest`) le numéro de la dernière version.
  Aucune donnée personnelle n'est envoyée. Désactivable dans Help → Check for updates
  automatically.
- **Pont Claude Code** (menu Claude) : désactivé par défaut ; il n'écoute que sur
  `127.0.0.1`, protégé par une clé.
- Aucune télémétrie, aucune statistique d'usage.

## Mise en place (mainteneurs)

Une fois le projet accepté par la SignPath Foundation :

1. Sur [app.signpath.io](https://app.signpath.io), dans le projet `FastShell` :
   - relier le dépôt GitHub (*Trusted build system* GitHub.com) ;
   - configuration d'artefact par défaut (le programme arrive dans un zip GitHub) :

     ```xml
     <?xml version="1.0" encoding="utf-8"?>
     <artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
       <zip-file>
         <pe-file path="FastShell.exe">
           <authenticode-sign/>
         </pe-file>
       </zip-file>
     </artifact-configuration>
     ```

   - stratégie de signature `release-signing` (fournie par la fondation).
2. Créer un utilisateur CI sur SignPath et copier son jeton d'API.
3. Dans GitHub, *Settings → Secrets and variables → Actions* :
   - secret `SIGNPATH_API_TOKEN` = le jeton d'API ;
   - variable `SIGNPATH_ORGANIZATION_ID` = l'identifiant de l'organisation SignPath.
4. Publier une version comme d'habitude ([development.md](development.md)) : le build attend
   l'approbation sur SignPath (6 h au plus), remet `FastShell.exe` signé dans le paquet,
   recalcule le zip et son `.sha256`, puis publie la release.

Tant que le secret n'existe pas, le build publie la release sans signature, comme avant.
