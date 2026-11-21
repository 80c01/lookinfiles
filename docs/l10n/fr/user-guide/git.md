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

## L’onglet Git

L’onglet **Git**, à côté de l’onglet **Aperçu**, apparaît quand le dossier
courant est dans un dépôt. Les boutons en haut (ou `v`) passent d’une de ses
quatre vues à l’autre : **Modifications**, **Historique**, **Statistiques**
et **Dépôt**. Il s’ouvre sur **Historique**.

### Modifications

Ce qui n’est pas encore commité, en trois groupes :

- **Indexé** : ce que contiendra le prochain commit (`git add`) ;
- **Modifications** : ce qui a changé dans les fichiers depuis, non indexé ;
- **Non suivi** : les nouveaux fichiers que git ne suit pas encore.

Chaque groupe est une ligne avec une flèche : un clic dessus, `Entrée` ou
`Gauche` / `Droite` le replient et le déplient (`Gauche` sur un fichier
mène à son groupe) ; chaque groupe reste comme vous l’avez laissé quand la
liste est relue. Un fichier indexé puis modifié de nouveau est dans les
deux premiers groupes. Chaque fichier montre sa lettre d’état (les badges de
l’explorateur), son dossier et les lignes qu’il ajoute et retire
(`+12 −3`). Sélectionnez un fichier pour voir son patch en dessous : ce que
le commit ajoutera pour un fichier indexé, ce qui a changé depuis l’index
pour les autres, le fichier entier pour un fichier non suivi. `Entrée` ou un
double-clic affiche le fichier dans l’aperçu. Le bouton indique combien il
y a de modifications, depuis les autres vues aussi (comptées à partir du
seul statut, aussi vite que `git status`) ; la liste est relue chaque fois
que la vue est affichée et quand le dossier change.

### Historique

L’historique, du plus récent au plus ancien, avec le commit, le message
(précédé de badges colorés pour `HEAD`, les branches locales et distantes,
et les tags), l’auteur et la date de chaque commit (relative, complète dans
son infobulle). Sélectionnez-en un pour voir son diff complet en dessous,
sous une ligne donnant son hash, son sujet, son auteur et sa date. Les 100
commits les plus récents sont lus d’abord ; faire défiler jusqu’au bas de la
liste (ou y amener la sélection) lit les 100 suivants.

**Historique Git** dans le menu du clic droit d’un fichier ou d’un dossier
de l’explorateur ne montre que les commits qui l’ont modifié (les
renommages ne sont pas suivis) : son chemin s’affiche à côté du champ de
filtre, et sa croix ramène tous les commits.

Le champ au-dessus de la liste filtre l’historique : seuls restent les
commits dont le message, l’auteur (nom ou e-mail) ou le hash contiennent
chaque mot tapé (sans tenir compte de la casse), cherchés dans tout
l’historique et pas seulement parmi les commits affichés. À côté, le nombre
de commits listés (les suivants qui correspondent sont lus de la même façon).
`Ctrl+F`, `/` en mode Vim ou `C-s` en mode Emacs y mènent depuis la liste ;
`Entrée` ou `Bas` ramènent à la liste, `Échap` le vide.

Un clic droit sur un commit copie son hash, son hash court ou son message
complet. `Ctrl+C` (`y` en mode Vim, `M-w` en mode Emacs) copie le hash du
commit sélectionné.

Les touches de navigation du mode global se déplacent dans la liste
(`j` / `k` en Vim, les flèches dans tous les modes).

### Statistiques

Combien de commits, par combien d’auteurs, du premier mois au dernier,
puis :

- **Les douze derniers mois** : un carré par jour, une colonne par semaine
  (le lundi en haut), plus foncé avec plus de commits ;
- **Par jour de la semaine et par heure** : un cercle par heure de chaque
  jour, plus grand avec plus de commits, à l’heure de chaque auteur (dans
  son propre fuseau horaire) ;
- **Par mois** : une barre par mois depuis le premier commit (par année
  au-delà de six ans) ;
- **Auteurs** : leurs commits, leur part et les mois de leurs premier et
  dernier commits, du plus actif au moins actif (les 20 premiers).

Chaque carré, cercle et barre donne son nombre dans son infobulle. Tout
l’historique est compté quand la vue est affichée, et de nouveau après un
commit.

### Dépôt

La configuration du dépôt :

- **Identité** : le nom et l’e-mail avec lesquels vos commits sont faits,
  et d’où vient chacun (**Système**, **Global** : votre `~/.gitconfig`,
  **Dépôt** : son `.git/config`) ; **Non défini** s’il manque ; si les
  commits sont signés, et avec quelle clé.
- **Remotes** : chaque remote et son URL (l’URL de push aussi quand elle
  diffère).
- **Branches** : chaque branche locale (celle extraite en gras), sa branche
  amont et combien de commits sont à pousser (`↑`) ou à tirer (`↓`), **à
  jour**, **pas d’amont**, ou **amont disparu** quand sa branche distante a
  été supprimée.
- **Configuration** : chaque réglage avec son niveau ; un réglage redéfini
  à un niveau plus proche est barré (il ne s’applique pas). Le champ
  au-dessus filtre les réglages (nom ou valeur).

Les mots de passe et les jetons ne sont jamais affichés : dans les URL
(`https://moi:•••@hôte/...`) et dans les réglages qui en contiennent
(jetons, mots de passe, en-têtes HTTP supplémentaires). La vue ne fait que
montrer la configuration ; **Modifier .git/config** et **Modifier la
config globale** ouvrent ces fichiers dans un onglet d’édition. Elle est
relue chaque fois qu’elle est affichée.

## Fichiers diff et patch

Un fichier `.diff` ou `.patch` s’affiche avec un résumé des fichiers qu’il
modifie et des lignes colorées ; `m` montre le texte brut.
