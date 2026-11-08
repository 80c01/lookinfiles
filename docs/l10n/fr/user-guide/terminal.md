# Le terminal

LookinFiles a un terminal intégré : vous pouvez lancer des commandes sans
quitter la fenêtre. Sous Linux, il a besoin de la bibliothèque VTE (voir
[Prise en main](getting-started.md#installation)) ; sans elle, le panneau
du terminal affiche « Terminal panel disabled » et comment l’activer. Sous
Windows (10 version 1809 ou plus récent), le terminal est intégré : le
shell est PowerShell 7 s’il est installé, sinon Windows PowerShell, sinon
`cmd.exe`.

## Ouvrir et fermer

| Touche | Action |
|---|---|
| `Ctrl+T` | Ouvrir un panneau de terminal à côté de ceux qui tournent |
| `Ctrl+Shift+T` | Ouvrir un panneau de terminal sous ceux qui tournent |
| `Ctrl+Shift+L` | Mettre le terminal sous l’aperçu, ou de nouveau à côté |
| Bouton **Terminal** (barre du haut) | Afficher ou masquer le panneau du terminal |

Un nouveau shell démarre dans le dossier courant de l’explorateur. Le menu
du clic droit de l’explorateur peut aussi ouvrir un terminal dans un
dossier donné.

Sélectionnez du texte à la souris, puis `Ctrl+Shift+C` le copie ;
`Ctrl+Shift+V` colle le presse-papiers dans le shell.

Pour fermer un panneau, quittez son shell (`exit` ou `Ctrl+D`). Masquer le
panneau avec le bouton **Terminal** laisse les shells tourner.

## Déplacer le focus

`Alt+Left` / `Alt+Right` déplacent le focus entre le terminal et les autres
panneaux, dans tous les modes. Dans un terminal, les autres touches vont au
shell : les touches de panneau de Vim, Emacs et Notepad n’y servent pas.

Les séparations entre panneaux se déplacent comme toutes les autres.

## Machines distantes

Pendant que vous parcourez une [machine distante](remote.md) connectée, le
bouton **Terminal** affiche le nom de la machine et ouvre un terminal SSH
vers elle. Dans la vue distante, `t` ouvre un terminal SSH vers la machine
sélectionnée.

## Espaces de travail

Chaque [espace de travail](workspaces.md) a ses propres panneaux de
terminal. Une disposition enregistrée retient le nombre de panneaux et leur
agencement, ainsi que les onglets de terminal du panneau d’édition ;
l’appliquer démarre les shells qui manquent.

Le terminal suit les couleurs du thème et la police du code (voir
[les thèmes](themes.md)).
