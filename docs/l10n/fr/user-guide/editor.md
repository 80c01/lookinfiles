# L’éditeur

LookinFiles édite les fichiers texte dans des onglets. Chaque onglet a son
propre mode clavier : **Notepad** (les touches habituelles du bureau),
**Emacs** ou **Vim** (voir [Mode Vim](vim.md)). Les fichiers `.org`
s’ouvrent en **Org-mode**, qui utilise les touches d’Emacs (voir
[Org-mode](org-mode.md)).

## Ouvrir un fichier

- `Enter` ou un double-clic sur un fichier de l’explorateur l’ouvre dans un
  onglet à côté de l’onglet **Aperçu**.
- `e` dans l’aperçu ouvre le fichier affiché.
- `t` (Vim) ou `C-o` (Emacs) dans l’explorateur ou l’aperçu l’ouvre dans le
  **panneau d’édition dédié** (plus bas).
- `Ctrl+Enter` dans `Ctrl+P` ou `Alt+R` ouvre le fichier choisi dans un
  onglet (voir [la recherche](search.md)).
- `lookinfiles chemin/du/fichier` ouvre le fichier dans le panneau
  d’édition dédié.

Un fichier déjà ouvert ne s’ouvre pas deux fois : son onglet est affiché.

Une puce (●) dans le titre de l’onglet signale des modifications non
enregistrées. Fermer un onglet ou quitter avec des modifications non
enregistrées demande d’abord.

### Notebooks Jupyter

Un notebook Jupyter (`.ipynb`) s’ouvre plutôt dans un **onglet notebook** :
un cadre par cellule, avec son type (**Code**, **Markdown** ou **Brut**, à
changer dans son menu), sa source colorée dans le langage du noyau ou en
Markdown et, sous une cellule de code, les sorties enregistrées dans le
fichier (texte, erreurs, images). Les cellules ne sont pas exécutées : il
n’y a pas de noyau.

- `Ctrl+Up` / `Ctrl+Down` passent à la cellule précédente / suivante,
  `Alt+Up` / `Alt+Down` déplacent la cellule, `Alt+Enter` ajoute une
  cellule de code en dessous, `Alt+Delete` supprime la cellule
  (**Annuler la suppression** dans la barre la remet). Les boutons de
  chaque cellule font de même.
- La barre ajoute des cellules de code ou Markdown, efface toutes les
  sorties (**Effacer les sorties**) et ouvre le fichier en texte JSON dans
  un onglet d’édition (**Éditer en JSON**).
- Le notebook s’enregistre tout seul peu après chaque modification, à la
  fermeture de l’onglet et en quittant (`Ctrl+S` enregistre tout de suite).
  Il est écrit comme Jupyter l’écrit : une cellule inchangée ne donne aucune
  différence dans git.

Un notebook nbformat 3 ou endommagé s’ouvre en texte.

## Onglets et panneaux

| Touche | Action |
|---|---|
| `Ctrl+S` | Enregistrer (Emacs `C-x C-s`, Vim `:w`) |
| `Ctrl+Shift+S` | Enregistrer sous (Emacs `C-x C-w`, Vim `:saveas`) |
| `Ctrl+W` | Fermer l’onglet (Vim `Ctrl+W c` ; les onglets Emacs et Org-mode gardent `C-w` pour « kill region ») |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Onglet suivant / précédent (Vim `gt` / `gT`, Emacs `C-x <right>` / `C-x <left>`) |
| `Ctrl+R` / `F2` | Renommer le fichier de l’onglet (seulement `F2` dans les onglets Emacs et Org-mode) |
| `Ctrl+Alt+N` | Afficher ou masquer les numéros de ligne |
| Clic du milieu sur le titre | Fermer l’onglet |
| Clic droit sur le titre | Changer le mode, les numéros de ligne, afficher la barre de symboles (code et Markdown), afficher la minicarte (cet onglet seulement), déplacer, diviser ou fermer l’onglet |

### Le panneau d’édition dédié

Par défaut, les onglets d’édition partagent le panneau d’aperçu. Le bouton
au début de la barre d’onglets (**Séparer l’onglet Aperçu du panneau
principal**, affiché dès qu’un onglet est ouvert) les déplace dans un
panneau à eux, à côté de l’aperçu : vous pouvez ainsi lire un fichier tout
en en éditant un autre. Les onglets canvas, notebook, terminal et vidéo suivent. Le
même bouton (**Fusionner les onglets dans le panneau principal**) les
remet tous en place.

### Diviser

`Ctrl+Alt+flèches` (ou `Ctrl+Alt+H` / `J` / `K` / `L`) déplace l’onglet
actuel dans un nouveau groupe de ce côté, en divisant le panneau. Depuis le
panneau principal, **droite** ouvre le panneau d’édition dédié ; depuis
celui-ci, **gauche** remet l’onglet en place.

Vous pouvez aussi faire glisser un onglet : déposez-le sur une autre barre
d’onglets pour l’y déplacer, ou près du bord d’un éditeur pour diviser ce
côté.

Pour voir deux endroits d’un même fichier à la fois, faites un clic droit
sur le titre de son onglet et choisissez **Diviser à droite** ou **Diviser
en bas** (Emacs `C-x 3` / `C-x 2`, Vim `Ctrl+W v` / `Ctrl+W s`, `:vs` /
`:sp`) : un second onglet montre le même fichier à côté ou en dessous. Les
deux montrent le même texte pendant la frappe et partagent l’historique
d’annulation ; chacun garde son curseur et sa sélection. En fermer un
(Emacs `C-x 0`, Vim `Ctrl+W q` / `:clo`) garde l’autre ouvert, et les
modifications non enregistrées avec ; Emacs `C-x 1`, Vim `Ctrl+W o` /
`:only` ferment les autres vues du fichier. En mode Vim, les vues partagent
un seul Vim : mode, registres et historique d’annulation.

## Choisir le mode d’un onglet

Un nouvel onglet prend le **mode global** affiché dans la barre du haut.
Pour changer le mode d’un seul onglet, faites un clic droit sur son titre
et choisissez **Notepad**, **Emacs** ou **Vim** ; le mode actuel est
marqué. La barre du haut continue d’afficher le mode global.

La section **Éditeur** des Préférences (`Ctrl+,`) règle le mode global
gardé pour les lancements suivants, si les nouveaux onglets affichent les
numéros de ligne et renvoient les longues lignes à la ligne (`-e` sur la
ligne de commande l’emporte pour un lancement), et si les onglets de code
et Markdown affichent la barre de symboles (les onglets ouverts aussi), et
si les onglets d’édition (les onglets ouverts aussi), l’aperçu et l’aide
affichent la minicarte.

En mode Emacs, `M-x org-mode` active l’édition Org-mode pour n’importe quel
fichier, et `M-x text-mode` la désactive.

La barre en bas d’un onglet renseigne sur son fichier dans le style de son
mode : la branche git, le chemin depuis la racine du dépôt (ou le dossier
courant), une marque en cas de modifications non enregistrées, le langage,
la fin de ligne et l’encodage, la source de la navigation dans le code et,
avec un serveur de langage, le nombre d’erreurs et d’avertissements.

- Notepad : `⎇ main   src/main.c •` à gauche, puis
  `Li 3, Col 7 | 100 % | C | Unix (LF) | UTF-8 | clangd : 1 erreur, 0 avertissement`.
- Emacs : `-U:**-  src/main.c  Top (0%)  L3 C7  Git-main  (C clangd Flymake[1 0])`.
- Vim (style airline) : `NORMAL  ᚠ main  src/main.c [+]` à gauche, puis
  `word  c  utf-8[unix]    42% ☰   42/100 ㏑ :  7` à droite (le mot sous le
  curseur, le type de fichier, l’encodage et le format, le pourcentage,
  ligne/total, colonne).

## Mode Notepad

Les touches d’un éditeur de bureau habituel :

| Action | Touches |
|---|---|
| Couper, copier, coller, tout sélectionner | `Ctrl+X`, `Ctrl+C`, `Ctrl+V`, `Ctrl+A` |
| Annuler, rétablir | `Ctrl+Z`, `Ctrl+Y` ou `Ctrl+Shift+Z` |
| Rechercher, remplacer | `Ctrl+F`, `Ctrl+H` (`F3` / `Shift+F3` : suivant / précédent, `Esc` : fermer) |
| Aller à la ligne | `Ctrl+G` |
| Indenter, désindenter | `Tab`, `Shift+Tab` |
| Aligner à gauche, centrer, à droite, justifier | `Ctrl+L`, `Ctrl+E`, `Ctrl+R`, `Ctrl+J` |

Chaque occurrence d’une recherche est surlignée ; la casse est ignorée.

## Mode Emacs

L’allure de GNU Emacs : une ligne de mode et un minibuffer (zone d’écho) en
bas de l’onglet. Ses messages restent en anglais, comme dans Emacs.

| Action | Touches |
|---|---|
| Se déplacer | `C-f` `C-b` `C-n` `C-p`, `M-f` `M-b`, `C-a` `C-e`, `M-<` `M->`, `C-v` `M-v` |
| Marquer, couper, copier, coller | `C-SPC`, `C-w`, `M-w`, `C-y` ; `C-k` coupe la ligne, `M-d` coupe le mot |
| Recherche incrémentale | `C-s` / `C-r` (`RET` arrête, `C-g` revient en arrière) |
| Remplacement interactif | `M-%` |
| Annuler, rétablir | `C-/` ou `C-_`, `M-_` |
| Remplir le paragraphe, casse | `M-q`, `M-u` / `M-l` / `M-c` |
| Aller à la ligne | `M-g g` |
| Commandes par leur nom | `M-x` (`Tab` complète) |

`C-g` ou `Esc` annule une commande dans le minibuffer.

## Fonctions pour le code

Dans les fichiers source, chaque mode peut aller à une définition, lister
les références et afficher des fiches d’aide. Voir
[la navigation dans le code](code-navigation.md).

Les lignes modifiées depuis le dernier commit ont une barre colorée dans la
marge (vert : ajoutée, bleu : modifiée, rouge : supprimée), mise à jour
pendant la frappe. Voir [Git](git.md).

Les listes complètes des touches de chaque mode sont dans la
[référence du clavier](../keyboard.md#editor-tabs) (en anglais).
