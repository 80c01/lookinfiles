# Prise en main

## Installation

LookinFiles a besoin de GTK 4.12 ou plus récent, d’un compilateur C, de
`make` et de `pkg-config`. Sur Debian ou Ubuntu :

```bash
sudo apt install build-essential pkg-config libgtk-4-dev
make
./lookinfiles .
```

Les autres bibliothèques sont facultatives : le terminal (VTE), la
coloration syntaxique (GtkSourceView), les pages PDF (poppler), les bases
SQLite et d’autres formats d’archive (libarchive). `make` utilise ce qui est
installé ; une fonction dont la bibliothèque manque affiche un avis à la
place ; voir [Dépannage](troubleshooting.md#une-fonction-manque) pour les
paquets.

## Lancement

```bash
lookinfiles                     # rouvrir les espaces de travail de la dernière session
lookinfiles .                   # afficher le dossier courant
lookinfiles ~/projects/site     # afficher un dossier
lookinfiles notes.txt           # éditer un fichier
lookinfiles -n Travail/Idées    # ouvrir (ou créer) une note
```

| Option | Effet |
|---|---|
| `-e`, `--editor notepad\|emacs\|vim` | Mode global (voir plus bas) ; les fichiers `.org` s’ouvrent toujours en Org-mode |
| `-n`, `--note` | L’argument est une note : `Titre` ou `Carnet/Titre` ; une extension `.md`, `.org` ou `.txt` fixe le format d’une nouvelle note |
| `-f`, `--fullscreen` | Démarrer en plein écran |
| `-s`, `--size LARGEURxHAUTEUR` | Taille de la fenêtre, par exemple `1920x1080` |
| `-h`, `--help` | Afficher les options et quitter |
| `-V`, `--version` | Afficher la version et quitter |

Sans argument, LookinFiles rouvre les espaces de travail que vous aviez en
quittant la dernière fois : leurs dossiers, les dossiers dépliés, la
sélection, le tri et les fichiers ouverts. Un dossier qui n’existe plus est
ignoré. Un fichier donné sur la ligne de commande s’ouvre dans le panneau
d’édition dédié, l’explorateur et l’aperçu masqués.

## La fenêtre

```
┌──────────────────────────────────────────────────────────────────────┐
│ ~ › projects › site      [1][2][+]  Disp. Thèmes │ Explorateur Aperçu │
│                                                  │ Terminal  Aide     │  ← barre du haut
├───────────────┬──────────────────────────────────────────────────────┤
│ Explorateur   │ Aperçu │ main.c ● │ Commits Git                      │
│ [▢][▢][▢][▢]  │                                                      │
│ ▸ src         │   fichier rendu, onglets d’édition,                  │
│ ▾ docs        │   historique des commits, résultats de recherche     │
│   README.md   │                                                      │
│               ├──────────────────────────────────────────────────────┤
│               │ panneaux de terminal                                 │
├───────────────┴──────────────────────────────────────────────────────┤
│ ⎇ main  README.md  Mis à la corbeille [Annuler]  2,1 kB Markdown Vim │  ← barre d’état
└──────────────────────────────────────────────────────────────────────┘
```

- **Barre du haut.** Le dossier courant sous forme de fil d’Ariane
  (cliquez sur une partie pour y aller), la carte des espaces de travail,
  les boutons **Dispositions** et **Thèmes**, puis les boutons qui
  affichent ou masquent les panneaux **Explorateur**, **Aperçu** et
  **Terminal**, et **Aide** : un clic ouvre ce guide, un clic droit
  l’aide-mémoire du clavier.
- **Panneau Explorateur** (à gauche). Quatre vues, choisies avec les
  boutons en haut : fichiers, signets, notes et machines distantes.
  En dessous, une petite barre d’outils avec les actions de la vue.
- **Panneau Aperçu** (à droite). L’onglet **Aperçu** montre le fichier
  sélectionné. Les onglets d’édition s’ouvrent à côté, de même que les
  onglets **Commits Git**, **Recherche**, **Références** et **Aide**.
- **Terminal** (sous l’aperçu ou à côté). Des panneaux de shell, ouverts
  avec `Ctrl+T`.
- **Barre de l’explorateur** (en bas de l’explorateur). La branche git et
  de courts messages (avec un bouton **Annuler** après une mise à la
  corbeille ou un renommage ; dans la barre du haut quand l’explorateur est
  masqué).
- **Barre de l’aperçu** (en bas de la page d’aperçu). Le nom, la taille, le
  type et l’encodage du texte de l’élément affiché.
- Chaque **onglet d’édition** a sa propre barre, dans le style de son
  mode : la branche git et le chemin du fichier, la marque de modification
  non enregistrée, le langage, la fin de ligne, la source de la navigation
  dans le code et les diagnostics (voir [L’éditeur](editor.md)).

Chaque séparation entre panneaux peut être déplacée à la souris. Le panneau
qui a le focus du clavier est entouré ; `Alt+Left` et `Alt+Right` déplacent
le focus de panneau en panneau.

## Le mode global : Notepad, Emacs ou Vim

LookinFiles parle trois dialectes du clavier. Le **mode global**, affiché
dans la barre du haut, détermine :

- les touches de navigation de l’explorateur et de l’aperçu (`j` / `k` en
  Vim, `C-n` / `C-p` en Emacs, les flèches partout) ;
- l’allure de leur barre de recherche (une barre de recherche Notepad, un
  minibuffer I-search d’Emacs ou une ligne de commande `/` de Vim) ;
- le mode des nouveaux onglets d’édition.

Cliquez sur le mode dans la barre du haut pour en changer, ou démarrez avec
`-e vim` / `-e emacs`. Un onglet d’édition peut ensuite passer seul dans un
autre mode (clic droit sur son titre). Les touches qui agissent sur une
sélection (`a`, `d`, `r`, `m`, `o`, `i`, `?`…) sont les mêmes dans tous les
modes.

## Premiers pas

1. Déplacez-vous dans l’arborescence avec les flèches (ou les touches de
   votre mode) et regardez l’aperçu suivre la sélection.
2. Appuyez sur `Right` pour ouvrir un dossier, `Left` pour le fermer,
   `Backspace` pour aller au dossier parent.
3. Appuyez sur `Enter` sur un fichier texte pour l’éditer dans un onglet ;
   `Ctrl+S` l’enregistre.
4. Appuyez sur `m` sur un fichier Markdown pour passer de la page rendue à
   sa source.
5. Appuyez sur `Ctrl+P` et tapez une partie d’un nom de fichier pour y
   aller.
6. Appuyez sur `Ctrl+T` pour un terminal.
7. Appuyez sur `?` pour l’aide-mémoire du mode actuel, `F1` pour ce guide,
   `Ctrl+Q` pour quitter.
