# Dépannage

## Une fonction manque

Certaines fonctions ont besoin d’une bibliothèque facultative à la
compilation. Installez-la, puis relancez `make` :

| Symptôme | À installer (Debian / Ubuntu) |
|---|---|
| « Terminal panel disabled » | `libvte-2.91-gtk4-dev` |
| Pas de couleurs syntaxiques dans le code | `libgtksourceview-5-dev` |
| « Aperçu PDF désactivé » | `libpoppler-glib-dev` |
| Les bases SQLite n’affichent que les détails du fichier | `libsqlite3-dev` |
| Les fichiers 7z, RAR, xz, zstd, ISO ou RPM ne sont pas listés | `libarchive-dev` |

Un programme sert à l’exécution, sans recompiler :

| Symptôme | À installer |
|---|---|
| Les machines distantes ne se connectent pas (« sshfs requis ») | `sshfs` |

## « cannot open display »

LookinFiles est un programme graphique : il a besoin d’une session de
bureau (X11 ou Wayland). Par SSH, utilisez `ssh -X` ou lancez-le sur
l’écran de la machine elle-même.

## Une machine distante ne se connecte pas

- Vérifiez que `ssh utilisateur@hôte` marche dans un terminal **sans
  demander de mot de passe** : LookinFiles ne peut pas en taper. Utilisez
  une clé SSH, et `ssh-agent` si la clé a une phrase de passe.
- Vérifiez l’hôte, l’utilisateur, le port et le dossier distant avec `F2`
  dans la vue distante.
- Une machine de `~/.ssh/config` utilise les réglages écrits dans ce
  fichier ; appuyez sur `F5` après l’avoir modifié.

## Aller à la définition ne trouve rien, ou la mauvaise chose

- Regardez la barre de l’onglet d’édition : elle nomme la source utilisée
  (un serveur de langage, `ctags` ou l’index intégré). L’index intégré ne
  fait que deviner.
- Installez un serveur de langage pour le langage (`clangd`, `pylsp`,
  `gopls`…) ou Universal Ctags ; voir
  [la navigation dans le code](code-navigation.md).
- Pour C et C++, donnez à `clangd` un `compile_commands.json` à la racine
  du projet.

## Une touche ne fait pas ce que j’attends

- Regardez le mode global dans la barre du haut : les touches de
  navigation en dépendent (`j` / `k` ne marchent qu’en mode Vim).
- Un onglet d’édition peut avoir son propre mode : faites un clic droit sur
  son titre pour le voir.
- Appuyez sur `?` pour voir les touches du mode actuel.
- Votre bureau peut garder certaines touches pour lui (`Alt+1`…`Alt+8`,
  `Ctrl+Alt+flèches`) ; changez alors le raccourci du bureau.

## Les boutons n’ont pas d’icônes

Les icônes sont intégrées au programme : elles devraient toujours
s’afficher. Pour voir quel fichier d’icône utilise chaque bouton, lancez
LookinFiles avec `LOOKIN_DEBUG_ICONS=1 ./lookinfiles`.

## Mon thème n’est pas appliqué

Un fichier de thème illisible est ignoré, et la raison est affichée dans le
terminal depuis lequel LookinFiles a été lancé. Quand vous modifiez un
thème dans un onglet d’édition (`Alt+T` → **Modifier**), l’enregistrer
affiche le problème dans la fenêtre. Voir [les thèmes](themes.md).

## Repartir de zéro

Les réglages et les données sont dans `~/.config/lookinfiles/` (voir
[Configuration](../configuration.md), en anglais). Pour oublier seulement
la dernière session, supprimez `session.ini` ; pour revenir au thème par
défaut, supprimez `theme.ini`. Gardez `notes/` et `bookmarks.xml` : ils
contiennent vos notes et vos signets.
