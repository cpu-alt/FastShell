# Sécurité

## Signaler une faille

Merci de **ne pas** ouvrir de ticket public. Utilisez le signalement privé de GitHub :
onglet **Security → Report a vulnerability** du dépôt
[cpu-alt/FastShell](https://github.com/cpu-alt/FastShell/security/advisories/new).

Indiquez la version de FastShell (Help → About), les étapes pour reproduire et l'impact. Nous
répondons dès que possible et publions un correctif dans une nouvelle release ; FastShell propose
alors la mise à jour à tous les utilisateurs.

## Versions prises en charge

Seule la **dernière release** reçoit les correctifs de sécurité. FastShell vérifie lui-même les
mises à jour (Help → Check for updates).

## Comment FastShell protège vos données

- Mots de passe enregistrés chiffrés par Windows (DPAPI), jamais écrits en clair.
- Exports de sessions avec mots de passe chiffrés en AES-256-GCM (PBKDF2-SHA256, 600 000 itérations).
- Clés d'hôte SSH vérifiées ; connexion refusée si la clé d'un serveur change.
- Aucune clé SSH utilisée à votre insu : seule celle choisie dans la session.
- Mises à jour et installation téléchargées en HTTPS depuis les releases du projet uniquement,
  avec vérification de l'empreinte SHA-256.
- Texte collé et titres distants nettoyés des caractères de contrôle invisibles ; confirmation
  avant de coller plusieurs lignes.

Le détail, les points corrigés et les limites connues sont dans
[docs/security-audit.md](docs/security-audit.md).
