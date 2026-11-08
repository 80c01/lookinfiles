# Trouver des fichiers et du texte

Trois outils trouvent des choses au-delà de l’arborescence visible. Ils
travaillent sur le **projet** : le dépôt git du dossier courant, ou le
dossier lui-même quand il n’est pas dans un dépôt.

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

## Rechercher dans un panneau ou un onglet

| Où | Notepad | Vim | Emacs |
|---|---|---|---|
| Explorateur (noms des éléments) | `Ctrl+F` | `/` | `C-s` / `C-r` |
| Aperçu (contenu) | `Ctrl+F` | `/` | `C-s` / `C-r` |
| Onglet d’édition | `Ctrl+F`, `Ctrl+H` pour remplacer | `/`, `?`, `:s` | `C-s`, `C-r`, `M-%` pour remplacer |

Voir [l’explorateur](explorer.md#rechercher-par-nom) et
[l’éditeur](editor.md) pour les détails.
