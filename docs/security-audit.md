# Audit de sécurité de FastShell

Date : 4 novembre 2026 — version 1.0.0. Revue du code source, corrections et tests automatiques.
Retour à la [politique de sécurité](../SECURITY.md).

## Résumé

| # | Domaine | Constat | Gravité | État |
| --- | --- | --- | --- | --- |
| 1 | Import de sessions | Un fichier importé pouvait relier ses propres serveurs à un de **vos** utilisateurs partagés (même nom) : à l'ouverture, votre mot de passe enregistré partait vers le serveur choisi par l'auteur du fichier. | Élevée | Corrigé |
| 2 | Import de sessions | Un fichier pouvait contenir des sessions « shell local » avec un programme et des arguments : un double-clic lançait ce programme sur le PC. | Élevée | Corrigé |
| 3 | Import de sessions | Le nombre d'itérations PBKDF2 était lu dans le fichier sans limite : une valeur énorme bloquait l'application. | Moyenne | Corrigé |
| 4 | Téléchargement SFTP | Le nom de fichier venait du serveur : un serveur malveillant pouvait viser `C:fichier`, un flux NTFS caché (`nom:flux`), un nom réservé (`CON`, `NUL`), et un téléchargement écrasait sans prévenir un fichier du même nom. | Moyenne | Corrigé |
| 5 | Coller dans le terminal | Le texte collé (page web, mail) pouvait contenir des caractères de contrôle invisibles (Échap, Ctrl+C…) et plusieurs lignes exécutées d'un coup. | Moyenne | Corrigé |
| 6 | Titre de fenêtre | Le titre envoyé par le programme distant (OSC 0/2) pouvait contenir des caractères de direction (U+202E) pour tromper l'affichage. | Faible | Corrigé |
| 7 | Export chiffré | PBKDF2-SHA256 à 300 000 itérations, sous la recommandation OWASP 2023. | Faible | Corrigé (600 000) |
| 8 | Chiffrement hérité | L'ancien format du code d'origine (AES-CBC sans authentification, IV dérivé du mot de passe) restait dans le code. | Faible | Supprimé |
| 9 | Bureau à distance | Le fichier `.rdp` temporaire (hôte et utilisateur) restait dans `%TEMP%`. | Faible | Corrigé |
| 10 | Images du thème | Écrites dans `%TEMP%` sous un nom fixe. | Info | Déplacées dans le cache de l'application |
| 11 | Login SSH | Des clés de `~/.ssh` et l'agent étaient essayés automatiquement, et `~/.ssh/config` pouvait imposer un utilisateur. | Moyenne | Corrigé (seule la clé choisie sert) |

## Ce qui a été vérifié et qui est sain

- **Mots de passe enregistrés** : chiffrés avec DPAPI (compte Windows), jamais en clair dans
  `sessions.json` ; aucun journal (`qDebug`, fichiers de log) dans l'application.
- **Exports** : AES-256-GCM (authentifié), nonce aléatoire de 96 bits par message, sel de 16 octets,
  PBKDF2-HMAC-SHA256 ; un fichier modifié ou un mauvais mot de passe est refusé.
- **Clés d'hôte SSH** : vérifiées avec `known_hosts`. Clé inconnue : empreinte SHA-256 affichée et
  confirmation demandée. Clé **changée** : connexion refusée (attaque de l'homme du milieu possible).
- **libssh 0.12** : protégée contre Terrapin (CVE-2023-48795) par le « strict KEX ».
- **Repli SHA-1/CBC** pour les vieux équipements : il ne fait qu'**ajouter** ces algorithmes après
  les modernes ; la clé d'hôte reste vérifiée. `diffie-hellman-group1-sha1` n'est jamais activé
  sans l'option de la session. L'information est visible au survol de l'onglet.
- **Séquences d'échappement** : le terminal ne renvoie que des réponses fixes (position du curseur,
  identification). Il ne renvoie **jamais** le titre (CSI 21 t) ni le presse-papiers (OSC 52), ce
  qui ferme les attaques classiques où le serveur « tape » du texte dans le shell. Les paramètres
  numériques et les chaînes OSC sont bornés (64 Kio).
- **Telnet** : sous-négociations bornées à 4 Kio, pas de boucle de négociation (RFC 1143).
- **Mises à jour** : uniquement en HTTPS, uniquement depuis
  `https://github.com/cpu-alt/FastShell/releases/download/`, jamais de retour à une version plus
  ancienne, empreinte SHA-256 vérifiée avant toute installation, arguments passés séparément à
  PowerShell (pas d'injection de commande).
- **Installation** (`install.ps1`) : mêmes vérifications (lien, SHA-256), installation pour
  l'utilisateur courant sans droits administrateur.

## Tests ajoutés

`fastshell_tests` :

- `Security.TerminalSurvivesHostileOutput` — 20 000 morceaux de sortie hostile (séquences
  tronquées, nombres énormes, UTF-8 invalide, redimensionnements) : aucun plantage, curseur
  toujours dans l'écran, réponses toujours fixes.
- `Security.TerminalNeverReportsItsTitleOrClipboard`, `Security.TerminalBoundsHugeRequests`.
- `Security.TelnetSurvivesHostileServer`, `Security.TelnetCapsSubnegotiation`.
- `SshLogin.*` — login dans le terminal, 3 essais, aucune clé essayée, Ctrl+C.

`fastshell_session_tests` :

- `TextSafety.*` — titres, collage (caractères cachés, fin de collage entre crochets), noms de
  fichiers SFTP, pas d'écrasement.
- `AppSecurity.*` — import : shells locaux refusés, mots de passe partagés non empruntables,
  itérations absurdes refusées, fichiers corrompus ; export : 600 000 itérations.
- `Update.*` — versions, liens non fiables refusés, empreinte falsifiée refusée.

## Limites connues (risques acceptés)

- **DPAPI** protège contre la lecture du fichier par un autre compte ou sur un autre PC, pas
  contre un programme malveillant qui tourne déjà sous **votre** compte Windows.
- **Le mot de passe** reste en mémoire pendant la session (réutilisé pour le SFTP).
- **`irm … | iex`** fait confiance à GitHub (HTTPS) pour le script lui-même ; pour une confiance
  maximale, télécharger le zip de la release et comparer son SHA-256.
- **Les releases ne sont pas signées** (Authenticode) : Windows SmartScreen peut afficher un
  avertissement au premier lancement. Une signature de code serait la prochaine étape.
- **Pas d'analyse automatique de mémoire** (sanitizers) : MinGW ne fournit pas ASan sous Windows.
