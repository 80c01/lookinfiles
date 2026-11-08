# L’explorateur

Le panneau Explorateur, à gauche, affiche une arborescence. Il a quatre
vues, choisies avec les boutons en haut :

| Bouton | Vue | Touche |
|---|---|---|
| Dossier | Fichiers : le dossier courant et ses sous-dossiers | `Ctrl+A`, `Ctrl+U`, `Ctrl+Shift+R` y reviennent |
| Signet | [Signets](bookmarks.md) | `Ctrl+A` |
| Page | [Notes](notes.md) | `Ctrl+U` |
| Serveur | [Machines distantes](remote.md) | `Ctrl+Shift+R` |

Chaque touche bascule entre la vue des fichiers et sa propre vue. Cette
page parle de la vue des fichiers ; les touches pour se déplacer sont les
mêmes dans les quatre.

## Se déplacer

La sélection suit les flèches dans tous les modes, plus les touches du
mode global (voir [Prise en main](getting-started.md#le-mode-global--notepad-emacs-ou-vim)) :

| Action | Tous les modes | Vim | Emacs |
|---|---|---|---|
| Élément suivant / précédent | `Down` / `Up` | `j` / `k` | `C-n` / `C-p` |
| Ouvrir un dossier, ou y entrer | `Right` | `l` | `C-f` |
| Fermer un dossier, ou aller à son parent | `Left` | `h` | `C-b` |
| Premier / dernier élément | `Home` / `End` | `gg` / `G` | `M-<` / `M->` |
| Une page plus bas / plus haut | `Page Down` / `Page Up` | `Ctrl+F` / `Ctrl+B` | `C-v` / `M-v` |

`Space` ouvre ou ferme le dossier sélectionné. Le bouton aux flèches de la
barre d’outils ferme tous les dossiers, ou les rouvre tous.

Le panneau d’aperçu affiche l’élément dès qu’il est sélectionné : le
contenu du fichier, ou une vue d’ensemble d’un dossier.

## Changer de dossier

- `Ctrl+Enter` sur un dossier en fait la racine de l’arborescence.
- `Backspace` ou `Alt+Up` va au dossier parent.
- Un clic sur une partie du fil d’Ariane de la barre du haut va à ce
  dossier.
- `Shift+H` / `Shift+L` (ou les boutons précédent et suivant de la souris)
  reviennent en arrière et vont en avant dans les dossiers affichés par cet
  espace de travail, comme un navigateur web.
- `Alt+R` liste vos dossiers et fichiers récents (voir
  [la recherche](search.md)).

Chaque [espace de travail](workspaces.md) a son propre dossier et son
propre historique.

## Ouvrir des fichiers

- `Enter` ou un double-clic ouvre un fichier texte dans un onglet
  d’édition, à côté de l’onglet **Aperçu** (voir [l’éditeur](editor.md)).
- `t` (Vim) ou `C-o` (Emacs) l’ouvre dans le panneau d’édition dédié, un
  second panneau à côté de l’aperçu. En Vim l’éditeur prend le focus ; en
  Emacs l’explorateur le garde.
- Le menu du clic droit propose aussi **Ouvrir avec l’application par
  défaut** pour les fichiers que LookinFiles n’édite pas (images, PDF,
  documents bureautiques…).

## Rechercher par nom

Cherchez parmi les éléments visibles par leur nom avec la barre de
recherche en bas du panneau :

| Mode | Ouvrir la recherche | Occurrence suivante / précédente | Fermer |
|---|---|---|---|
| Notepad | `Ctrl+F` | `Enter` ou `F3` / `Shift+F3` | `Esc` |
| Vim | `/` | `n` / `N` | `Esc` |
| Emacs | `C-s` / `C-r` | `C-s` / `C-r` à nouveau | `RET` garde l’occurrence, `C-g` revient en arrière |

Pour trouver un fichier n’importe où dans le projet, utilisez plutôt
`Ctrl+P` (voir [la recherche](search.md)).

## Créer, renommer et supprimer

| Touche | Action |
|---|---|
| `a` | Nouveau fichier dans le dossier sélectionné (il s’ouvre dans l’éditeur) |
| `Shift+A` | Nouveau dossier |
| `F2`, `r` ou `e` | Renommer le fichier ou le dossier sélectionné |
| `d` ou `Delete` | Le mettre à la corbeille (demande d’abord) |
| `u` | Annuler la dernière mise à la corbeille ou le dernier renommage |

Après un renommage ou une mise à la corbeille, la barre en bas de
l’explorateur (la barre du haut quand l’explorateur est masqué) affiche un
message avec un bouton **Annuler** pendant quelques secondes ; `u` fait la
même chose à tout moment, pour la dernière modification seulement. Les
onglets d’édition suivent un fichier renommé.

Les fichiers vont dans la corbeille du bureau (celle que montre votre
gestionnaire de fichiers) ; on peut donc aussi les restaurer depuis
celle-ci.

## Tri et fichiers cachés

- `o` fait tourner le tri : nom, date, taille, extension. Les dossiers
  restent en premier.
- `i` (ou `I`) affiche ou masque les fichiers cachés (noms commençant par
  `.`).

Le tri et l’affichage des fichiers cachés appartiennent à l’espace de
travail et sont restaurés au lancement suivant.

## État git

Dans un dépôt git, les fichiers affichent une lettre colorée après leur
nom :

| Badge | Signification |
|---|---|
| **M** | Modifié |
| **A** | Ajouté à l’index |
| **D** | Supprimé |
| **R** | Renommé |
| **U** | Non suivi (nouveau fichier pas encore ajouté) |
| **!** | Conflit de fusion |

Un dossier affiche **M** quand il contient des fichiers modifiés, **U**
quand il ne contient que des nouveaux. Voir [Git](git.md) pour le reste des
fonctions git.

## Le menu du clic droit

Sur un fichier ou un dossier : ouvrir, ouvrir dans le panneau latéral
(fichiers texte) ou avec l’application par défaut (autres fichiers), ouvrir
un terminal dans un dossier, renommer, mettre à la corbeille, copier le
chemin, ajouter un signet, annuler. Sur une zone vide : nouveau fichier,
nouveau dossier, terminal.

## Autres touches

| Touche | Action |
|---|---|
| `Ctrl+D` | Ajouter un signet sur l’élément sélectionné (ou le dossier courant) |
| `m` | Basculer l’aperçu entre rendu et brut |
| `+` / `-` / `0` | Zoomer sur l’image ou le PDF affiché, dézoomer, revenir à l’ajustement |

L’arborescence se met à jour toute seule quand des fichiers changent sur le
disque, que vous les changiez dans LookinFiles, dans le terminal ou dans un
autre programme.
