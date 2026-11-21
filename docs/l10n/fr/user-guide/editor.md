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

### Nouveaux onglets

Le bouton **+** au bout à droite d’une barre d’onglets (celle du panneau
d’aperçu et celle de chaque groupe d’édition) ouvre un nouvel onglet dans
ce groupe :

- **Applications** : un onglet qui liste ce qu’un onglet peut contenir.
  Tapez pour filtrer la liste, déplacez-vous avec `Haut` / `Bas`
  (`Page préc.` / `Page suiv.` de cinq en cinq), et appuyez sur `Entrée` ou
  cliquez sur une application : elle s’ouvre à la place de l’onglet
  Applications. `Esc`, `Ctrl+W` ou un clic du milieu sur l’onglet le
  ferme. Chaque espace de travail garde le sien ; il n’est pas gardé avec
  la session.
- **Éditeur** : un onglet d’édition vide « Sans-titre-N » dans le mode
  global (voir [Choisir le mode d’un onglet](#choisir-le-mode-dun-onglet)).
- **Terminal** : un shell dans un nouvel onglet (VTE est nécessaire).

Les applications :

- **Navigateur web** : un onglet de navigateur vide, la barre d’adresse
  active (WebKitGTK 6.0 est nécessaire, ou WebView2 sous Windows, comme
  pour `Ctrl+Shift+B`).
- **Notebook Jupyter** : un notebook avec une cellule de code vide, gardé en
  mémoire jusqu’à ce que vous l’enregistriez : `Ctrl+S` demande un nom de
  fichier (depuis le dossier de l’explorateur, `.ipynb` ajouté). Le fermer
  ou quitter avec des modifications propose d’abord de les enregistrer ;
  son noyau démarre dans le dossier de l’explorateur.
- **Canvas** : une nouvelle note Canvas « Sans-titre-N » dans le carnet
  sélectionné dans la vue Notes, sinon le premier (voir [notes](notes.md)).
- **Org-mode**, **Vim**, **Emacs**, **Notepad** : un onglet d’édition vide
  « Sans-titre-N » dans ce mode. Enregistrez-le avec l’« Enregistrer sous »
  du mode (Notepad `Ctrl+S`, Emacs et Org-mode `C-x C-s` ou `C-x C-w`, Vim
  `:w nom`) ; le fermer avec des modifications demande d’abord. Un onglet
  d’édition vide ouvert par **Éditeur** s’enregistre de la même façon.
- **Markdown** : un onglet d’édition vide « Sans-titre-N.md » dans le mode
  global, coloré comme du Markdown, ses titres dans la tagbar ;
  il s’enregistre de la même façon.

Une puce (●) dans le titre de l’onglet signale des modifications non
enregistrées. Fermer un onglet ou quitter avec des modifications non
enregistrées demande d’abord.

### Notebooks Jupyter

Un notebook Jupyter (`.ipynb`) s’ouvre plutôt dans un **onglet notebook** :
un cadre par cellule, avec son type (**Code**, **Markdown** ou **Brut**, à
changer dans son menu), sa source colorée dans le langage du noyau ou en
Markdown et, sous une cellule de code, ses sorties (texte, erreurs, images,
formules). Les cellules Markdown s’affichent rendues (titres, listes,
tableaux, images, code, formules TeX en `$...$` et `$$...$$`) : `Enter` ou un double-clic en édite la source, et exécuter la cellule
(`Ctrl+Enter`, `Shift+Enter`, **Tout exécuter**) la rend à nouveau. Une
cellule Markdown nouvelle ou vide reste en source.

- `Shift+Enter` exécute la cellule et passe à la suivante, `Ctrl+Enter`
  l’exécute ; le bouton qui suit celui d’exécution d’une cellule de code
  l’exécute avec toutes les cellules suivantes. Les cellules s’exécutent dans le noyau Jupyter du notebook,
  démarré à la première exécution dans le dossier du notebook et arrêté
  avec l’onglet : Python 3 avec `jupyter_client` et le noyau (`ipykernel`
  pour Python) doivent être installés. Le bouton du noyau dans la barre
  porte le nom du noyau du notebook, la barre d’état sous les cellules son
  état ; son menu liste les noyaux installés : en choisir un
  arrête le noyau en cours, l’enregistre dans le notebook et le démarre.
  **Tout exécuter** et les boutons
  d’arrêt et de redémarrage de la barre exécutent toutes les cellules,
  interrompent et redémarrent le noyau ; quand le code demande une ligne
  (`input()`), tapez-la dans la ligne sous la barre.
  L’en-tête d’une cellule de code montre comment s’est passée sa dernière
  exécution depuis l’ouverture de l’onglet, par la couleur de son prompt et
  la durée qui suit : *en attente* (gris) quand une autre cellule passe
  avant, `● 3.2s` (ambre) qui défile pendant qu’elle tourne, `✓ 1.4s` (vert)
  une fois terminée, `✗ 0.3s` (rouge) après une erreur, une interruption ou
  un noyau arrêté. Une cellule sautée après une erreur n’affiche pas de
  durée.
- `Ctrl+Up` / `Ctrl+Down` passent à la cellule précédente / suivante,
  `Alt+Up` / `Alt+Down` déplacent la cellule, `Alt+Enter` /
  `Alt+Shift+Enter` insèrent une cellule de code en dessous / au-dessus, `Alt+Delete` supprime la cellule
  (**Annuler** dans la barre la remet). Les boutons de
  chaque cellule font de même.
- `Ctrl+Shift+Up` / `Ctrl+Shift+Down`, ou `Shift`+clic sur l’en-tête
  d’une cellule, sélectionnent plusieurs cellules qui se suivent (leurs
  en-têtes en surbrillance, leur nombre dans la barre d’état) ; déplacer,
  supprimer, exécuter et le type de cellule s’appliquent alors à toutes,
  par les touches ou par les boutons d’une cellule sélectionnée. `Esc`,
  `Ctrl+Up` / `Ctrl+Down` ou passer à une autre cellule abandonnent la
  sélection. Un clic sur l’en-tête d’une cellule y va. **Scinder la cellule**, dans le menu du clic
  droit sur le source d'une cellule, la coupe au curseur (ou avant et après
  la sélection), comme dans JupyterLab : les sorties restent avec la
  dernière partie.
- `Ctrl+Shift+C` / `Ctrl+Shift+X` copient / coupent la cellule (les
  cellules sélectionnées), `Ctrl+Shift+V` colle les cellules copiées en
  dessous (depuis n’importe quel onglet notebook, sorties comprises ; les
  cellules collées sont sélectionnées). `Ctrl+Shift+M` fusionne les
  cellules sélectionnées dans la première, ou la cellule avec la suivante,
  comme dans JupyterLab : leurs sources jointes par une ligne vide, le type
  de la première, les sorties abandonnées. **Annuler** dans la barre remet
  les cellules supprimées, coupées ou fusionnées en dernier.
- Un clic droit sur l’en-tête d’une cellule, son en-tête `Out` ou son
  code replié ouvre le **menu de la cellule** : exécuter, copier, couper,
  coller, fusionner, insérer, déplacer et supprimer (les cellules
  sélectionnées quand la cellule en fait partie). Un clic droit dans le
  texte d’une cellule ouvre le menu du texte (couper, copier et coller du
  texte), avec **Scinder la cellule**.
- Une cellule Markdown avec des titres ouvre une **section** : les
  cellules qui la suivent jusqu’au prochain titre de même niveau ou plus
  haut. La flèche à gauche de son en-tête (celle de l’explorateur), ou
  `Tab` sur la cellule rendue, la replie : ses cellules sont masquées et
  une note dit combien, un clic sur la note la déplie. `Shift+Tab` replie
  toutes les sections, ou les déplie toutes quand elles sont toutes
  repliées. Les sorties d’une cellule de code viennent sous un en-tête
  `Out [n]:` (`n` : l’exécution qui les a produites) ; un clic dessus les
  masque, l’en-tête dit alors combien sont masquées. La flèche au début de
  l’en-tête d’une cellule de code replie son code : seule sa première
  ligne reste, avec le nombre de lignes qui suivent ; un clic dessus (ou
  `Entrée`) le déplie. Les trois sont gardés dans le notebook comme
  JupyterLab les garde : un
  notebook replié ici s’ouvre replié dans JupyterLab. Les cellules
  repliées s’exécutent quand même avec **Tout exécuter** ; `Ctrl+Up` /
  `Ctrl+Down` et `Shift+Enter` les sautent, et aller à une cellule masquée
  (depuis la tagbar, ou une cellule insérée dans une section repliée)
  déplie sa section.
- `Ctrl+K Ctrl+O` (ou le bouton de liste après celui du noyau) affiche la
  **barre de symboles** à côté des cellules : les titres des cellules
  Markdown, imbriqués par niveau. Elle suit la cellule courante ; `Enter`
  ou un clic va à la cellule du titre, `Esc` revient aux cellules, `q` ou
  `Ctrl+K Ctrl+O` la masque. Un nouvel onglet notebook l’affiche quand
  Préférences → Éditeur affiche la barre de symboles des nouveaux onglets.
- La barre ajoute une cellule de code (bouton `<>`) ou Markdown (bouton
  M↓) sous la cellule courante. Son menu **⋯**, au bout à droite, efface
  toutes les sorties (**Effacer les sorties**) et ouvre le fichier en texte
  JSON dans un onglet d’édition (**Éditer en JSON**). Dans un panneau
  étroit, les boutons de gauche sont coupés en premier : le bouton du
  noyau, celui de liste et le menu restent visibles.
- Le notebook s’enregistre tout seul peu après chaque modification, à la
  fermeture de l’onglet et en quittant (`Ctrl+S` enregistre tout de suite ;
  un nouveau notebook fait avec le bouton **+** demande d’abord un nom de
  fichier).
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
| Clic droit sur le titre | Changer le mode, les numéros de ligne, afficher la barre de symboles (code et Markdown), afficher la minicarte (cet onglet seulement), recharger depuis le disque, comparer avec le fichier enregistré / HEAD / un autre fichier, déplacer, diviser ou fermer l’onglet |

### Fichiers modifiés sur le disque

Les onglets ouverts surveillent leur fichier sur le disque selon les
habitudes de leur mode :

- **Notepad** : un tampon sans modification se recharge sur place, en gardant
  le curseur et le défilement. Si vous avez des modifications non enregistrées
  (ou si le fichier a été supprimé), un bandeau en haut de l’onglet propose
  **Comparer** (diff côte à côte avec le disque), **Recharger** et **Garder le
  mien**, et enregistrer (`Ctrl+S`) demande confirmation avant d’écraser les
  changements externes.
- **Vim** : avec `:set autoread` (actif par défaut ; `:set noautoread` le
  désactive), un tampon propre se recharge automatiquement. Sinon Vim prévient
  dans la ligne de commande (`W11` / `W12`) ; `:e!` recharge, `:checktime`
  vérifie le disque, et `:w` refuse d’écraser tant que vous n’utilisez pas
  `:w!`.
- **Emacs** et **Org-mode** : le rechargement automatique est désactivé par
  défaut (`M-x auto-revert-mode` le bascule pour l’onglet). Les changements
  sont signalés dans la zone d’écho ; `M-x revert-buffer` recharge depuis le
  disque, et `C-x C-s` demande `y/n` dans le minibuffer avant d’écraser.

### Le panneau d’édition dédié

Par défaut, les onglets d’édition partagent le panneau d’aperçu. Le bouton
au début de la barre d’onglets (**Séparer l’onglet Aperçu du panneau
principal**, affiché dès qu’un onglet est ouvert) les déplace dans un
panneau à eux, à côté de l’aperçu : vous pouvez ainsi lire un fichier tout
en en éditant un autre. Les onglets canvas, notebook, terminal et vidéo suivent. Le
même bouton (**Fusionner les onglets dans le panneau principal**) les
remet tous en place. Tant que le panneau est affiché, le bouton voisin
(**Échanger les panneaux Aperçu et Édition**) place le panneau d’édition de
l’autre côté de l’aperçu, et inversement ; chaque espace de travail garde
son côté, et les dispositions enregistrées le retiennent.

### Diviser

`Ctrl+Alt+flèches` (ou `Ctrl+Alt+H` / `J` / `K` / `L`) déplace l’onglet
actuel dans un nouveau groupe de ce côté, en divisant le panneau. Depuis le
panneau principal, **droite** ouvre le panneau d’édition dédié ; depuis
celui-ci, **gauche** remet l’onglet en place (l’inverse quand le panneau
d’édition est à gauche de l’aperçu).

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

Le menu du clic droit sur le titre d’un onglet copie aussi le nom du
fichier (**Copier le nom**), son chemin depuis la racine de son dépôt git
(**Copier le chemin relatif git**, seulement dans un dépôt) ou son chemin
absolu (**Copier le chemin**).

Enregistrer un fichier dont l’aperçu est rendu depuis son texte (Markdown,
Org, HTML, JSON, le PDF d’un document LaTeX...) met l’aperçu à jour, et
y affiche le fichier même quand l’aperçu montrait un autre fichier ;
sélectionner l’onglet d’un tel fichier, ou y revenir depuis
l’explorateur, l’y affiche aussi (tel
qu’enregistré ; un document LaTeX : son PDF, s’il a été compilé) ;
revenir dans l’explorateur affiche de nouveau sa sélection. Pour
garder l’aperçu tel quel pendant que vous travaillez, décochez **Mettre à
jour l’aperçu** dans le menu contextuel de l’onglet ; le recocher remet
l’aperçu à jour et laisse de nouveau les enregistrements le mettre à jour.

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
affichent la minicarte. **Mettre à jour l’aperçu depuis l’éditeur**
décoché fait démarrer chaque onglet, les onglets ouverts aussi, avec
**Mettre à jour l’aperçu** désactivé.

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
  ligne/total, colonne) ; le mode et le pourcentage, la ligne et la colonne
  sont dans la couleur du mode.

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

## Documents LaTeX

Enregistrer un fichier `.tex` (`Ctrl+S`, `:w`, `C-x C-s`) compile son
document en arrière-plan, comme Overleaf, et affiche le PDF dans l’aperçu.
Le PDF suit ensuite le curseur : se déplacer dans le fichier `.tex` fait
défiler l’aperçu jusqu’à l’endroit de la ligne (SyncTeX, par la commande
`synctex` de TeX Live), à la hauteur du curseur dans l’éditeur. Après une
compilation, l’endroit reste où il était dans l’aperçu.

- La commande est `latexmk` s’il est installé, sinon Tectonic, sinon
  `pdflatex`, `xelatex` ou `lualatex` lancé une fois. Le moteur vient d’une
  ligne `% !TEX program = xelatex`, sinon des paquets (`fontspec`,
  `unicode-math` et `polyglossia` demandent XeLaTeX ; `luacode` LuaLaTeX).
  Un `latexmkrc` dans le dossier du document le choisit à la place.
- Un chapitre est compilé par son document principal : le fichier nommé par
  une ligne `% !TEX root = ../main.tex`, sinon le document de son dossier
  (ou du dossier au-dessus) qui l’inclut avec `\input` ou `\include`,
  sinon `main.tex`.
- Les erreurs, avertissements et boîtes mal remplies vont dans l’onglet
  **LaTeX**, à côté de **Commits Git**, avec la ligne source de chaque
  erreur. `Enter` ou un double-clic ouvre l’endroit dans un onglet
  d’édition.
  L’onglet s’ouvre de lui-même quand il y a des erreurs ou des
  avertissements.
- Les fichiers de la compilation (`.aux`, `.log`, `.synctex.gz`...) sont
  écrits à côté du document. Un enregistrement pendant une compilation
  recompile à la fin de celle-ci.

## Fonctions pour le code

Dans les fichiers source, chaque mode peut aller à une définition, lister
les références et afficher des fiches d’aide. Voir
[la navigation dans le code](code-navigation.md).

Les lignes modifiées depuis le dernier commit ont une barre colorée dans la
marge (vert : ajoutée, bleu : modifiée, rouge : supprimée), mise à jour
pendant la frappe. Voir [Git](git.md).

Les listes complètes des touches de chaque mode sont dans la
[référence du clavier](../keyboard.md#editor-tabs) (en anglais).
