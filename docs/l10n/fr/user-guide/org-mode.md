# Org-mode

[Org-mode](https://orgmode.org) est le format de plans et de tâches de GNU
Emacs. LookinFiles lit et édite les fichiers `.org` avec les principales
commandes d’Org ; ils restent ainsi compatibles avec Emacs.

Il y a deux façons de travailler avec un fichier Org :

- **Dans l’aperçu** : replier des titres, faire tourner les mots-clés TODO,
  cocher des cases et suivre des liens sans ouvrir d’éditeur (voir
  [le panneau d’aperçu](preview-panel.md#fichiers-org-mode)).
- **Dans un onglet d’édition** : les fichiers `.org` s’ouvrent dans le mode
  d’édition **Org-mode**, qui utilise les touches d’Emacs et ajoute les
  commandes Org ci-dessous.

Les notes Org de la [vue des notes](notes.md) marchent de la même façon.

## Comment lire les touches

`Alt` est la touche Meta d’Emacs (`M-`). `C-c C-t` signifie `Ctrl+C`, puis
`Ctrl+T`. Les touches sans `C-c` marchent aussi dans les onglets Notepad et
Vim. `M-x org-…` lance une commande par son nom Emacs (`org-todo`,
`org-schedule`, `org-clock-in`…). Les messages et les questions restent en
anglais, comme dans Emacs.

## Plan

| Touche | Action |
|---|---|
| `Tab` | Sur un titre : replié → enfants → tout le sous-arbre |
| `Shift+Tab` | Tout le fichier : vue d’ensemble → contenu → tout |
| `Alt+Enter` | Nouveau titre (ou nouvel élément dans une liste) |
| `Ctrl+Enter` | Nouveau titre après le sous-arbre actuel |
| `Alt+Left` / `Alt+Right` | Remonter / descendre le titre d’un niveau |
| `Alt+Shift+Left` / `Alt+Shift+Right` | Remonter / descendre tout le sous-arbre d’un niveau |
| `Alt+Up` / `Alt+Down` | Déplacer le sous-arbre après son voisin |
| `C-c C-n` / `C-c C-p` | Titre suivant / précédent |
| `C-c C-u` | Titre parent |
| `C-c *` | Faire d’une ligne un titre, ou l’inverse |

Le fichier s’ouvre replié sur ses titres de premier niveau, sauf s’il a une
ligne `#+STARTUP:` : `content` montre tous les titres, `showall` montre
tout. Les replis restent en place pendant l’édition, et amener le curseur
dans un texte replié (recherche, aller à la ligne) le déplie.

## Tâches

| Touche | Action |
|---|---|
| `Shift+Left` / `Shift+Right`, `C-c C-t` | Faire tourner le mot-clé TODO |
| `Alt+Shift+Enter` | Nouveau titre TODO |
| `Shift+Up` / `Shift+Down`, `C-c ,` | Changer la priorité `[#A]` |
| `C-c q` | Définir les étiquettes du titre |
| `C-c / t` | N’afficher que les entrées TODO ouvertes |
| `C-c / /` | N’afficher que les lignes correspondant à une expression régulière |

Les mots-clés sont `TODO` et `DONE`, sauf si le fichier déclare les siens :

```org
#+TODO: TODO NEXT | DONE CANCELLED
#+STARTUP: logdone
```

Avec `logdone`, terminer une entrée ajoute `CLOSED: [date]`. Un mot-clé
écrit `RUNNING(r!)` note l’heure à laquelle l’entrée est passée dans cet
état ; ajoutez `#+STARTUP: logdrawer` pour garder ces lignes dans un tiroir
`:LOGBOOK:`.

## Listes de contrôle

```org
* Voyage [1/3]
- [X] Réserver le train
- [ ] Faire les valises
- [ ] Arroser les plantes
```

`C-c C-c` ou `Ctrl+Space` coche l’élément sous le curseur. Les parents
deviennent `[-]` quand ils sont en partie faits, et les compteurs `[n/m]`
et `[n%]` se mettent à jour. `C-c #` met à jour tous les compteurs.

## Dates et chronomètre

| Touche | Action |
|---|---|
| `C-c C-s` | Planifier l’entrée |
| `C-c C-d` | Fixer une échéance |
| `C-c .` / `C-c !` | Insérer une date active `<…>` / inactive `[…]` (`C-u` d’abord : avec l’heure) |
| `Shift+Up` / `Shift+Down` sur une date | Changer la partie sous le curseur |
| `C-c C-x C-i` / `C-c C-x C-o` | Démarrer / arrêter le chronomètre (dans un tiroir `:LOGBOOK:`) |

Les dates se tapent sous la forme `today`, `+3`, `+2w`, `mon`,
`2026-11-02 10:00`, ou `none` pour en retirer une. Une date répétée comme
`SCHEDULED: <2026-11-02 Mon +1w>` passe à son occurrence suivante quand
l’entrée est marquée terminée.

## Tableaux

```org
| Nom    | Qté |
|--------+-----|
| Pomme  |   3 |
```

`Tab` et `Shift+Tab` passent d’une cellule à l’autre, `Enter` va à la ligne
suivante (ajoutée si besoin), et le tableau se réaligne au fur et à mesure.
Une ligne commençant par `|-` devient un filet. `Alt+flèches` déplacent les
colonnes et les lignes, `Alt+Shift+flèches` les suppriment ou les insèrent.

## Liens et blocs

- `C-c C-l` insère un lien, `C-c C-o` ouvre le lien sous le curseur (une
  URL, `file:`, `*Titre`, `#custom-id` ou `<<cible>>`).
- Tapez `<s` puis `Tab` pour un bloc de code source (`<e` exemple, `<q`
  citation, et les autres modèles d’Emacs).

La liste complète est dans la
[référence du clavier](../keyboard.md#org-mode-editing) (en anglais).
