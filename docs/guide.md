# Guide d'utilisation de FastShell

Retour à la [présentation](../README.md).


## Utilisateurs partagés et profils d'équipement

- **Utilisateurs partagés** (Session → Manage users…, ou « Manage… » dans la boîte de session) :
  on enregistre une fois un compte (nom d'utilisateur, mot de passe chiffré, clé privée), puis
  on le choisit dans « Saved user » pour chaque session SSH. Changer le mot de passe à un seul
  endroit met à jour toutes les sessions qui l'utilisent.
- **Profils d'équipement** (Session → Manage device profiles…) : **Cisco** et **FS** sont fournis,
  et on peut en créer d'autres. Le profil choisi dans « Device » :
  - affiche une étiquette colorée (CISCO, FS…) à côté de la session dans l'arbre, et donne sa
    couleur à l'onglet ;
  - en **série**, remplit les réglages de la console : Cisco 9600 8N1, FS 115200 8N1, sans
    contrôle de flux. Le profil peut aussi envoyer CR LF pour Entrée et ^H pour Retour arrière ;
  - en **SSH**, ne change rien, comme PuTTY.

  Le profil FS suit la documentation FS (S3900, S5850) : 115200 bauds, 8N1, sans contrôle de
  flux. Retour arrière envoie DEL comme PuTTY ; si un équipement ne l'accepte pas, clic droit
  sur l'onglet série → « Backspace sends ^H » pour basculer sans modifier la session.

  Les profils fournis sont modifiables, et vos réglages sont conservés.

## Organiser, partager, ouvrir à plusieurs

- **Couleurs** : clic droit sur un dossier → « Folder color… » ; sur une session → « Icon color… »
  (icône et onglet). « Default … color » revient à la couleur par défaut.
- **Étiquettes personnalisées** : clic droit sur une ou plusieurs sessions → « Tag » pour en
  choisir une, ou « New tag… » pour créer la vôtre (nom, texte de l'étiquette, couleur, réglages
  série facultatifs). CISCO et FS sont fournies.
- **Export / import** : clic droit sur un dossier → « Export this folder… » ou « Import into this
  folder… » ; clic droit dans le vide → tout exporter ou importer. Trois niveaux d'export :
  1. noms et adresses seulement (IP / DNS), sans utilisateur ;
  2. avec les utilisateurs et chemins de clé, sans mot de passe ;
  3. avec les mots de passe, chiffrés en AES-256-GCM par un mot de passe d'export (ils
     restent lisibles sur un autre PC, contrairement au chiffrement Windows).

  L'import n'écrase jamais rien : il ajoute les sessions dans le dossier choisi et demande le mot
  de passe d'export si besoin. Par sécurité, il ne réutilise un de vos utilisateurs partagés que
  si le fichier contient le même mot de passe, et il n'importe jamais de session « shell local »
  (elle lancerait un programme sur votre PC).
- **Plusieurs sessions côte à côte**, plusieurs façons :
  1. **glisser-déposer** une session de l'arbre, **un onglet** (tiré vers le bas hors de la barre
     d'onglets) ou **un panneau** (par sa barre de titre) sur un terminal : un aperçu bleu animé,
     comme l'aimantation des fenêtres Windows, montre où il va se placer (moitié gauche, droite,
     haut ou bas du panneau visé ; au centre d'un panneau du même onglet, les deux s'échangent ;
     contre un bord de l'onglet, il prend tout ce côté). Pendant le glisser, rester une demi-seconde
     sur un autre onglet l'affiche ; lâcher un panneau sur la barre d'onglets le remet dans son
     propre onglet. Le terminal déplacé reste connecté ;
  2. sélectionner plusieurs sessions (Ctrl+clic) → « Open N sessions side by side », ou clic droit
     sur un dossier → « Open all side by side » : grille automatique (2 → côte à côte, 4 → 2×2…) ;
  3. en ligne de commande : `FastShell.exe --side-by-side admin@10.0.0.1 admin@10.0.0.2`
     (accepte aussi `telnet hôte`, `rdp hôte`, `"COM3 115200"` et `local`).

  Chaque panneau a sa barre de titre (couleur et nom de la session, □ pour l'agrandir seul dans
  l'onglet, double-clic aussi, × pour le fermer). L'onglet affiche les noms : « sw1 · sw2 (+1) ».
- **Taper dans tous les panneaux** (Window → Type in all panes, Ctrl+Shift+I) : ce que vous tapez
  part vers toutes les sessions de l'onglet, pratique pour envoyer la même commande à plusieurs
  switchs. Un cadre orange et un bandeau dans la barre d'état le signalent.
- **Connexion rapide** : champ en haut du panneau Session. Tapez `admin@10.0.0.1`,
  `switch01:2222`, `telnet 10.0.0.5`, `rdp admin@10.0.0.20` ou `COM3 115200` puis Entrée, sans
  enregistrer de session.
- **Bureau à distance (RDP)** : type de session « Remote desktop (RDP) » (Session → New remote
  desktop, Ctrl+Shift+R, ou le bouton écran de la barre d'outils). Le bureau s'affiche dans un
  onglet, avec le moteur de Bureau à distance de Windows (contrôle `mstscax.dll`, le même que
  mstsc) : utilisateur `DOMAINE\utilisateur` accepté, presse-papiers partagé, le bureau suit la
  taille de l'onglet. Connexion rapide : compression, cache d'images persistant et qualité du
  réseau détectée automatiquement. Clic droit sur l'onglet : se déconnecter / reconnecter, ouvrir
  la même session dans mstsc, ou **Move to a panel** : le bureau quitte l'onglet pour un panneau
  qu'on ancre sur n'importe quel côté ou qu'on détache en fenêtre, comme le Filer (clic droit sur
  le titre du panneau : Move back to a tab). Il faut Qt ActiveQt à la compilation (voir plus bas) ; sans lui, l'onglet
  propose d'ouvrir la session dans mstsc.
- **Icône du système** : chaque session peut afficher le logo de son système (Windows Server,
  Windows, Ubuntu, Debian, Red Hat, CentOS/Rocky/Alma, Linux, serveur, Cisco, FS, Fortinet,
  MikroTik, switch, routeur, pare-feu), dans l'arbre des sessions et sur l'onglet. Choix dans la
  fenêtre de la session (bouton icône à côté du nom) ou par clic droit → System, aussi sur plusieurs sessions à
  la fois. Un petit point de la couleur de la session reste en coin.

## Interface

12 thèmes au choix (View → Theme, changement immédiat) : FastShell Dark (par défaut),
FastShell Violet, FastShell Light, et des classiques de VS Code (Dracula, One Dark Pro, Monokai,
Nord, Tokyo Night, Catppuccin Mocha, GitHub Dark et Light, Solarized Dark). Chacun colore
l'interface et le terminal. **Glowing edges** (même menu) ajoute un fin liseré de la couleur
d'accent sur les panneaux, onglets, menus et le panneau actif. Onglets arrondis reliés au terminal, barre d'outils à icônes (nouvelle
session, onglet local, bureau à distance, coupures, taper partout, puis la vue : zoom −/+, police,
heure et numéros de ligne, panneaux Session / Filer / Outline, plein écran), menus et champs
arrondis, barres de défilement fines, police Segoe UI Variable, et barre de titre Windows
aux couleurs du thème (coins arrondis sous Windows 11).

- **Session** (à droite) : sessions enregistrées rangées dans des dossiers et sous-dossiers.
  Clic droit pour créer, modifier, dupliquer, renommer ou supprimer ; glisser-déposer pour
  ranger ; filtre de recherche. Les shells détectés (PowerShell 7, Windows PowerShell, cmd,
  Git Bash, distributions WSL) apparaissent automatiquement dans « Shell sessions ». Chaque
  session peut avoir une couleur d'onglet. Les mots de passe enregistrés sont chiffrés pour
  votre compte Windows (DPAPI), jamais écrits en clair.
- **Outline** (à droite) : liste des commandes tapées dans l'onglet courant. Clic pour revenir
  à la commande dans l'historique, double-clic pour la relancer.
- **Filer** (à gauche) : explorateur de fichiers. Sur un onglet local, il montre le disque ;
  sur un onglet SSH, il montre le serveur en **SFTP** : téléchargement (double-clic), envoi
  (bouton ou glisser-déposer depuis l'Explorateur), renommer, supprimer, nouveau dossier,
  « cd ici » dans le terminal.
- **Transfer** (en bas) : progression des transferts SFTP, qui apparaît automatiquement.
- **Barre de commandes rapides** (en bas) : boutons qui envoient une commande à l'onglet
  courant. Clic droit pour ajouter, modifier, déplacer ou supprimer.
- **Écran partagé (split)** : chaque onglet peut être coupé en plusieurs terminaux, côte à côte
  ou l'un sous l'autre, autant de fois que voulu (coupures imbriquées). Un trait bleu marque le
  panneau actif ; l'Outline, le Filer et la barre d'état le suivent. « Open in split right / down » (clic droit dans
  l'arbre des sessions) ouvre une session enregistrée à côté de l'onglet courant.
- **Marge** du terminal : heure d'écriture de chaque ligne et numéros de ligne, avec les lignes
  de commande mises en évidence. Les boutons « Time » et « Ln# » en bas à droite (ou le menu
  View) les affichent ou les masquent d'un clic, et le choix est mémorisé.
- Barre d'état : état de la connexion, type de session, position du curseur, taille, heure.
- Tous les panneaux sont déplaçables, détachables et masquables ; la disposition est mémorisée.

## Terminal

- Onglets de terminal local : PowerShell 7, Windows PowerShell ou cmd sous Windows (ConPty),
  `$SHELL` sous Linux et macOS.
- Sessions **SSH**, comme PuTTY : sans utilisateur enregistré, l'onglet s'ouvre et demande
  `login as:` dans le terminal, puis le mot de passe (non affiché), avec 3 essais (« Access
  denied » entre deux). Un utilisateur ou mot de passe enregistré est utilisé tel quel. Aucune
  clé n'est essayée toute seule (ni agent, ni clés de `~/.ssh`, ni `~/.ssh/config`) : seule la clé
  privée choisie dans la session ou l'utilisateur partagé sert. Mot de passe et
  keyboard-interactive, clés d'hôte dans `known_hosts` : un nouvel équipement est accepté sans question
  et sa clé est retenue, une clé qui change ensuite est refusée. Ctrl+C annule la saisie.
- Compatibilité avec les vieux équipements (switchs, routeurs) : si le serveur ne propose que des
  algorithmes SHA-1/CBC, la connexion est retentée automatiquement avec eux, comme PuTTY ou
  MobaXterm ; l'info est au survol de l'onglet et dans la barre d'état. L'option « Old devices » de la
  session autorise en plus `diffie-hellman-group1-sha1` (faible, jamais activé automatiquement).
- Sessions **Telnet** avec une vraie négociation des options (NAWS, TTYPE, ECHO, SGA, BINARY).
- Sessions **série** (port COM, adaptateur USB-série) pour les consoles de switchs et routeurs :
  vitesse, bits de données, parité, bits de stop, contrôle de flux, option CR LF. Les ports
  présents sont listés dans la boîte de session (bouton « Refresh »). Clic droit sur l'onglet →
  « Send BREAK » (récupération de mot de passe, ROMMON). Un adaptateur débranché est signalé et
  Entrée relance la connexion.
- Émulation xterm : 256 couleurs et true color, gras, italique, souligné, barré, écran
  alternatif (vim, htop, less), zones de défilement, caractères de dessin DEC (tmux, mc),
  caractères larges (CJK, emoji), souris (modes 1000/1002/1003/1006), collage sécurisé
  (bracketed paste), titres de fenêtre, rapports de focus.
- Historique de 20 000 lignes. Copier-coller comme MobaXterm : sélectionner copie tout de suite
  (double-clic = un mot), Ctrl+V ou clic droit colle.

## Mode portable

`FastShell.exe --data-dir D:\FastShell-data` garde les sessions et les réglages dans ce dossier
(fichier `.ini` et `sessions.json`) au lieu de `%APPDATA%` et du registre. Pratique sur une clé
USB, ou pour tester sans toucher à ses sessions. Les mots de passe enregistrés restent chiffrés
par Windows (DPAPI) : ils ne se relisent que sur le même compte Windows.

## Mises à jour

Help → Check for updates… compare votre version à la dernière release GitHub. Si une nouvelle
version existe, FastShell la télécharge (HTTPS, seulement depuis les releases du projet), vérifie
son empreinte SHA-256, se ferme, se met à jour et redémarre. La vérification se fait aussi
automatiquement une fois par jour au démarrage (Help → Check for updates automatically pour la
couper). Relancer la commande d'installation fait aussi la mise à jour.

## Raccourcis

| Action | Raccourci |
| --- | --- |
| Nouvel onglet local | Ctrl+Shift+T |
| Nouvelle session (enregistrée ou non) | Ctrl+Shift+N |
| Nouveau bureau à distance (RDP) | Ctrl+Shift+R |
| Dupliquer l'onglet | Ctrl+Shift+D |
| Couper à droite / en bas | Ctrl+Shift+E / Ctrl+Shift+O |
| Aller au panneau voisin | Ctrl+Alt+flèches |
| Fermer le panneau actif (ou l'onglet s'il est seul) | Ctrl+Shift+W |
| Taper dans tous les panneaux de l'onglet | Ctrl+Shift+I |
| Onglet suivant / précédent | Ctrl+Tab / Ctrl+Shift+Tab |
| Copier | sélectionner à la souris (copie tout de suite, double-clic = un mot), ou Ctrl+C quand du texte est sélectionné, Ctrl+Shift+C, Ctrl+Inser |
| Coller | Ctrl+V, clic droit, Ctrl+Shift+V ou Shift+Inser |
| Interrompre la commande | Ctrl+C sans sélection |
| Remonter dans l'historique | Shift+PgUp / Shift+PgDn / molette |
| Zoom | Ctrl+= / Ctrl+- |
| Plein écran | Alt+Entrée |

