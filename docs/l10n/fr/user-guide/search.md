# Trouver des fichiers et du texte

Trois outils trouvent des choses au-delà de l’arborescence visible. Ils
travaillent sur le **projet** : le dépôt git du dossier courant, ou le
dossier lui-même quand il n’est pas dans un dépôt. Un quatrième, la
[recherche approfondie](#recherche-approfondie--shiftf), cherche dans
n’importe quel dossier par nom, taille, date et contenu, sans limite.

## Aller à un fichier : `Ctrl+P`

`Ctrl+P` (Emacs : `C-x C-f`) liste tous les fichiers du projet. Tapez une
partie de son nom ou de son chemin : la liste se filtre au fil de la
frappe, les meilleures correspondances en premier, et les lettres n’ont pas
besoin d’être voisines (`edvim` trouve `editor_vim.c`). Sans rien taper,
les fichiers ouverts récemment viennent en premier.

| Touche | Action |
|---|---|
| `Enter` | Afficher le fichier dans l’explorateur et l’aperçu |
| `Ctrl+Enter` | L’ouvrir dans un onglet d’édition |
| `Esc` | Fermer la liste |

Les onglets d’édition Vim gardent `Ctrl+P` pour Vim : appuyez dessus depuis
l’explorateur ou l’aperçu.

## Dossiers et fichiers récents : `Alt+R`

`Alt+R` liste les dossiers visités, puis les fichiers ouverts, les plus
récents en premier, toutes sessions confondues. Tapez pour filtrer.

- `Enter` sur un dossier en fait le dossier de l’explorateur.
- `Enter` sur un fichier l’affiche ; `Ctrl+Enter` l’ouvre dans un onglet
  d’édition.

## Rechercher dans les fichiers : `Ctrl+Shift+F`

`Ctrl+Shift+F` demande un texte et le cherche dans tous les fichiers du
projet, en arrière-plan. La sélection de l’onglet d’édition actuel (ou
votre dernière recherche) est déjà remplie.

- La recherche ignore la casse. Les boutons à droite du texte changent la
  façon de chercher :

  | Bouton | Touche | Effet |
  |---|---|---|
  | `Aa` Respecter la casse | `Alt+M` | `Total` ne trouve plus `total` |
  | `ab` Mot entier | `Alt+W` | `foo` ne trouve plus `foobar` |
  | `.*` Expression régulière | `Alt+R` | Syntaxe PCRE (`get_\w+`, `^#include`), une ligne à la fois ; une expression invalide est signalée sous le texte |
  | `a…z` Lettres dans l’ordre | `Alt+L` | les lettres n’ont pas besoin d’être voisines (`edvim` trouve `editor_vim`) ; les espaces sont ignorées, la partie la plus courte de chaque ligne est affichée |

  Expression régulière et lettres dans l’ordre s’excluent ; mot entier ne
  s’applique pas aux lettres dans l’ordre.
- **Fichiers à inclure** (`Alt+N`) et **Fichiers à exclure** (`Alt+X`)
  prennent des motifs séparés par des virgules, comme dans `.gitignore` :
  `*.c` ou `tests` correspondent à un nom à toute profondeur, `src/ui` ou
  `/build` depuis la racine du projet, `build/` aux dossiers seulement ;
  `*` traverse aussi les dossiers. Vide : tous les fichiers.
- **Inclure les fichiers ignorés** (`Alt+I`) cherche aussi dans les
  fichiers que git ignore (hors git : les dossiers cachés et de
  compilation).
- Le texte, les options et les motifs sont gardés pour la recherche
  suivante de l’espace de travail : chaque espace de travail a sa propre
  dernière recherche et son propre onglet Recherche. Une recherche continue
  quand vous changez d’espace de travail ; ses résultats attendent dans
  l’onglet Recherche de l’espace qui l’a lancée.
- Limites : 100 000 fichiers, 512 Mo lus au total, 5 000 résultats ; les
  fichiers de plus de 4 Mo et les fichiers binaires sont ignorés. Quand une
  limite arrête la recherche, ou que des fichiers sont ignorés, l’en-tête
  de l’onglet Recherche le signale (dans la couleur d’avertissement) ;
  restreignez les fichiers à inclure pour chercher le reste.
- Les résultats vont dans un onglet **Recherche**, regroupés par fichier,
  avec les lignes trouvées. `Enter` ou un double-clic en ouvre un à sa
  ligne ; la croix ferme l’onglet.
- Les fichiers sont lus sur le disque : les modifications non enregistrées
  des onglets d’édition ne sont pas cherchées.

## Recherche approfondie : `Shift+F`

`Shift+F` dans l’explorateur (ou **Rechercher ici** dans le menu du clic
droit d’un dossier, ou le bouton **Rechercher** en haut à droite de
l’aperçu d’un dossier) ouvre l’onglet **Recherche approfondie** dans le
panneau d’aperçu, sur le dossier sélectionné (sur un fichier : son
dossier). Contrairement aux outils ci-dessus, elle n’est limitée ni au
projet, ni en nombre de fichiers listés ou lus : elle parcourt tout ce qui
se trouve sous le dossier.

Remplissez les critères voulus, puis appuyez sur `Enter` dans un champ (ou
sur le bouton **Rechercher**) ; un champ vide veut dire « n’importe
lequel ».

| Champ | Ce qu’il retient |
|---|---|
| **Dossier** | Où chercher, avec tout ce qu’il contient (`~` est votre dossier personnel) |
| **Nom** | Une partie du nom, sans tenir compte de la casse (`rapport`) ; avec `*` ou `?`, un motif du nom entier (`IMG_????.jpg`, `*.tar.gz`) |
| **Extensions** | Séparées par des virgules, sans tenir compte de la casse : `c, h, md`, `tar.gz` |
| **ou** une catégorie | Au lieu des extensions, un genre de fichier : documents, images, vidéos, audio, archives, texte et code, programmes (d’après l’extension, comme dans l’[Espace disque](preview-panel.md#espace-disque)) ; en choisir une vide et estompe les extensions ; un clic dedans (ou une saisie) remet **Toute catégorie** |
| **Contient** | Un texte dans le fichier, avec les mêmes boutons que la recherche dans les fichiers (`Aa`, `ab`, `.*`, `a…z` ; `Alt+M`, `Alt+W`, `Alt+R`, `Alt+L`) ; les fichiers binaires sont ignorés, les gros fichiers sont lus en entier |
| **Type** | Fichiers et dossiers, fichiers seulement (les liens comptent comme des fichiers) ou dossiers seulement |
| **Taille** | De / à, bornes comprises : `42`, `500k`, `10M`, `2G` (puissances de 1024) ; fichiers seulement |
| **Dernière modification** | De / à : un jour (`2026-11-01`, `2026-11-01 14:30`, `today`, `yesterday`) ou une durée écoulée (`30min`, `3h`, `7d`, `2w`, `6m` pour des mois, `1y`) ; un jour donné comme seconde date est compris ; le bouton calendrier à droite de chaque champ permet de choisir un jour, ou d’effacer la date |
| **Exclure** | Chemins écartés, séparés par des virgules, comme dans la recherche dans les fichiers : `node_modules`, `.git`, `build/` ; les dossiers écartés ne sont pas parcourus |
| **Profondeur** | Niveaux sous le dossier : `1` pour ses seules entrées |
| **Entrées cachées** | Aussi les noms qui commencent par un point (décoché : ils sont ignorés, avec ce qu’ils contiennent) |
| **Ce système de fichiers seulement** | Ne pas entrer dans les autres disques montés sous le dossier |

Les mots des dates (`today`, `yesterday`) et les unités (`min`, `h`, `d`,
`w`, `m`, `y`) s’écrivent en anglais.

La recherche tourne en arrière-plan : les résultats s’affichent au fur et
à mesure, avec les compteurs (entrées trouvées, entrées examinées,
dossiers lus, octets lus pour un contenu) et le dossier en cours de
lecture. `Escape` (ou le bouton **Arrêter**) l’arrête et garde ce qui a
été trouvé ; `F5` relance la recherche. Le bouton **Réinitialiser** vide
les critères (le dossier reste) et les résultats. Les liens symboliques sont listés
mais jamais suivis, et `/proc`, `/sys` et `/dev` sont ignorés. La
recherche s’arrête à 1 000 000 de résultats, pour ménager la mémoire.

Les résultats montrent le nom, le dossier (sous celui de la recherche),
la taille et la dernière modification ; avec un contenu, aussi le nombre
d’occurrences et la première ligne trouvée. Cliquez sur l’en-tête d’une
colonne pour trier. `Down` dans un champ passe aux résultats ; là, `Enter`
ou un double clic montre l’entrée dans l’explorateur, `Ctrl+Enter` ouvre
un fichier dans un onglet d’édition à sa première occurrence, et un clic
droit permet aussi de copier le chemin.

L’onglet garde ses critères tant qu’il est ouvert : l’ouvrir sur un autre
dossier ne change que le dossier. Chaque espace de travail a le sien.
`Ctrl+W`, un clic du milieu sur l’onglet ou sa croix le ferment et
arrêtent la recherche.

## Rechercher dans un panneau ou un onglet

| Où | Notepad | Vim | Emacs |
|---|---|---|---|
| Explorateur (noms des éléments) | `Ctrl+F` | `/` | `C-s` / `C-r` |
| Aperçu (contenu) | `Ctrl+F` | `/` | `C-s` / `C-r` |
| Onglet d’édition | `Ctrl+F`, `Ctrl+H` pour remplacer | `/`, `?`, `:s` | `C-s`, `C-r`, `M-%` pour remplacer |

Voir [l’explorateur](explorer.md#rechercher-par-nom) et
[l’éditeur](editor.md) pour les détails.
