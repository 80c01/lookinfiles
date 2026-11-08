# Git

Quand le dossier courant est dans un dépôt git, LookinFiles montre ce qui a
changé, sans que vous lanciez quoi que ce soit. Le programme `git` n’est
pas nécessaire : LookinFiles lit le dépôt avec libgit2.

LookinFiles montre l’état du dépôt ; pour indexer, commiter, pousser ou
changer de branche, utilisez git dans le [terminal](terminal.md).
L’affichage se met à jour tout seul après ces commandes.

## Dans l’explorateur

Les fichiers affichent une lettre colorée après leur nom :

| Badge | Signification |
|---|---|
| **M** | Modifié |
| **A** | Ajouté à l’index |
| **D** | Supprimé |
| **R** | Renommé |
| **U** | Non suivi (nouveau fichier pas encore ajouté) |
| **!** | Conflit de fusion |

Un dossier affiche **M** quand il contient des fichiers modifiés, **U**
quand il ne contient que des nouveaux.

La barre en bas de l’explorateur affiche la branche (⎇ `main`), avec `*`
quand le dépôt a des modifications non commitées, et `↑` / `↓` avec le
nombre de commits quand il est en avance ou en retard sur sa branche amont
(par exemple `⎇ main * ↑1 ↓2`). L’aperçu d’un dossier indique la branche et
les fichiers modifiés.

## Barres de modification

Dans l’aperçu d’un fichier texte et dans les onglets d’édition, une barre
dans la marge signale les lignes modifiées depuis le dernier commit, comme
dans VS Code :

- vert : lignes ajoutées ;
- bleu : lignes modifiées ;
- marque rouge : lignes supprimées.

Les onglets d’édition mettent les barres à jour pendant la frappe. La
minicarte les montre le long de son bord gauche.

## L’onglet Commits Git

L’onglet **Commits Git**, à côté de l’onglet **Aperçu**, apparaît quand le
dossier courant est dans un dépôt. Il liste l’historique, du plus récent au
plus ancien, avec le commit, le message, l’auteur et la date de chaque
commit. Sélectionnez-en un pour voir son diff complet en dessous.

Les touches de navigation du mode global se déplacent dans la liste
(`j` / `k` en Vim, les flèches dans tous les modes).

## Fichiers diff et patch

Un fichier `.diff` ou `.patch` s’affiche avec un résumé des fichiers qu’il
modifie et des lignes colorées ; `m` montre le texte brut.
