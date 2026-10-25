# 📁 Explorateur de Fichiers en C avec GTK+ 3

Application graphique simple pour naviguer dans le système de fichiers.

## 🎯 Fonctionnalités (version actuelle)

- ✅ Panneau gauche : liste **réelle** du répertoire courant (énumération `GDir`, tri alphabétique, dossiers en premier), colonnes « Nom » / « Type » (`dir` / `file`)
- ✅ Panneau droit : visualiseur de contenu — un clic sur un fichier à gauche affiche son contenu à droite
- ✅ Panneau redimensionnable : séparateur `GtkPaned` draggable (position initiale 300 px, largeur minimale 150 px)
- ✅ Navigation dans l'arborescence : le label `Repertoire: …` suit le répertoire courant
- ✅ Raccourcis clavier : `Ctrl+Q` pour quitter
- ✅ Keybindings vim dans le panneau gauche
- ✅ Interface GTK+ native complète

## ⌨️ Raccourcis clavier

### Global

| Touche | Action |
|---|---|
| `Ctrl+Q` | Quitter l'application |

### Panneau gauche (lorsque la liste a le focus)

| Touche | Action |
|---|---|
| `j` / `k` | Sélection suivante / précédente |
| `Ctrl+F` | Avancer d'une page (comme Page Down) |
| `Ctrl+B` | Reculer d'une page (comme Page Up) |
| `gg` | Aller en haut de la liste |
| `G` | Aller en bas de la liste |
| `/` | Ouvrir la barre de recherche (recherche live, insensible à la casse) |
| `n` / `N` | Occurrence suivante / précédente du terme cherché |
| `Entrée` | Entrer dans le répertoire sélectionné |
| `Backspace` | Remonter au répertoire parent |
| `Échap` | Fermer la barre de recherche |

## 📝 Compilation

```bash
make                           # ou
gcc -o explorateur_fichiers explorateur_fichiers.c `pkg-config --cflags --libs gtk+-3.0`
```

## 🚀 Exécution

```bash
./explorateur_fichiers          # Ou
gnome-terminal && ./explorateur_fichiers
```

## 📦 Prérequis

- GTK+ 3 Development: `libgtk-3-dev`
- GCC: `gcc`

Installer sur Debian/Ubuntu:
```bash
sudo apt install libgtk-3-dev build-essential
```

## 🏗️ Structure du projet

```
explorateur_fichiers.c  # Code source principal
Makefile                 # Fichier de build
README.md                # Documentation
explorateur_fichiers     # Binaire compilé
```

## 📝 Notes de version

- **v1.0** : Version de base avec deux panneaux principaux
- **v1.1** : Liste réelle du répertoire courant, panneau redimensionnable (`GtkPaned`), visualiseur de contenu relié à la sélection, `Ctrl+Q`
- **v1.2** : Keybindings vim (`j`/`k`, `gg`, `G`, `/`, `n`/`N`) et navigation dans l'arborescence (`Entrée`/`Backspace`)
- Compatible avec GTK+ 3.10+ (nécessaire pour `GtkSearchEntry`)
