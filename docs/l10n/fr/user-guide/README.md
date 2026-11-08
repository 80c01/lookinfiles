# Guide d’utilisation de LookinFiles

LookinFiles (« look in your files », « regardez dans vos fichiers ») est un
explorateur de fichiers piloté au clavier : une arborescence à gauche, un
aperçu en direct du fichier sélectionné à droite, un éditeur à onglets et un
terminal intégré, le tout dans une seule fenêtre.

Ce guide explique comment s’en servir, un sujet par page. Dans
l’application, `F1` (ou un clic sur le bouton **Aide** de la barre du haut)
l’affiche dans un onglet **Aide** ; `?` (ou un clic droit sur ce bouton)
affiche les touches du mode actuel. La liste complète des touches est dans
la [référence du clavier](../keyboard.md) (en anglais).

## Sommaire

**Pour commencer**

- [Prise en main](getting-started.md) : lancement, la fenêtre, le mode
  global, les premiers pas.

**Parcourir**

- [L’explorateur](explorer.md) : se déplacer, ouvrir, créer, renommer et
  supprimer des fichiers, trier, fichiers cachés.
- [Le panneau d’aperçu](preview-panel.md) : l’allure de chaque type de
  fichier, vues rendue et brute, zoom, médias.
- [Trouver des fichiers et du texte](search.md) : aller à un fichier
  (`Ctrl+P`), lieux récents (`Alt+R`), rechercher dans les fichiers
  (`Ctrl+Shift+F`).
- [Signets](bookmarks.md) : liens nommés vers des dossiers, des fichiers et
  des pages web.
- [Notes](notes.md) : carnets de notes Markdown, Org-mode, texte brut et
  Canvas.
- [Machines distantes](remote.md) : parcourir des machines SSH comme des
  dossiers locaux.

**Éditer**

- [L’éditeur](editor.md) : onglets, divisions, le panneau d’édition dédié,
  modes Notepad et Emacs.
- [Mode Vim](vim.md) : le moteur Vim, les commandes Ex et les options.
- [Org-mode](org-mode.md) : plans, tâches TODO, dates, chronomètres et
  tableaux.
- [Navigation dans le code](code-navigation.md) : aller à la définition,
  références, fiches d’aide, serveurs de langage.

**Travailler**

- [Le terminal](terminal.md) : des panneaux de terminal à côté ou sous
  l’aperçu.
- [Git](git.md) : badges d’état, barres de modification, historique des
  commits.
- [Espaces de travail et dispositions](workspaces.md) : huit espaces de
  travail, dispositions de panneaux enregistrées, la session restaurée au
  lancement.
- [Thèmes et polices](themes.md) : thèmes intégrés, vos propres thèmes,
  tailles de police.

**Aide**

- [Dépannage](troubleshooting.md) : fonctions manquantes, problèmes
  courants.

## Lire ce guide dans LookinFiles

Appuyez sur `F1`, ou cliquez sur le bouton **Aide** de la barre du haut,
pour ouvrir ce guide dans un onglet **Aide** du panneau d’aperçu ; cliquez
à nouveau sur le bouton pour le fermer.

| Touche | Action |
|---|---|
| Flèches, `Page Down` / `Page Up` (et les touches de votre mode) | Faire défiler |
| `Tab` / `Shift+Tab` | Sélectionner le lien suivant / précédent |
| `Enter`, ou un clic | Suivre le lien |
| `Shift+H` / `Shift+L`, boutons précédent / suivant de la souris | Précédent / suivant |
| `Ctrl+W` (Vim : `Ctrl+W c`), clic du milieu sur l’onglet | Fermer l’onglet |

Les boutons au-dessus de la page reviennent en arrière, vont en avant et
à ce sommaire, affichent l’aide-mémoire du clavier et ferment l’onglet. Sur
une longue page, la minicarte à droite montre toute la page en petit :
cliquez dessus ou faites-la glisser pour vous déplacer.

## Pages de référence

Ces pages sont en anglais.

- [Référence du clavier](../keyboard.md) : tous les raccourcis, par
  panneau et par mode.
- [Aperçus pris en charge](../previews.md) : chaque format affiché et ses
  limites.
- [Configuration](../configuration.md) : les fichiers de
  `~/.config/lookinfiles/`, les serveurs de langage, le format des thèmes.
