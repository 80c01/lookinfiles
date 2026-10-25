# 📁 Explorateur de Fichiers en C avec GTK+ 3

Application graphique simple naviguer dans le système de fichiers.

## 🎯 Fonctionnalités (version actuelle)

- ✅ Panneau gauche: Placeholder pour l'arborescence des fichiers/dossiers
- ✅ Panneau droit: Visualiseur de contenu pour les fichiers texte
- ✅ Interface GTK+ native complète

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
explorateur_fichiers     # Binaire compilé (~17Kb)
```

## 📝 Notes de version

- **v1.0**: Version de base avec deux panneaux principaux
- L'arborescence avancée peut être ajoutée dans un futur commit
- Compatible avec GTK+ 3.x
