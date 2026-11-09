# Espaces de travail et dispositions

## Panneaux

La fenêtre a quatre panneaux : l’explorateur, l’aperçu, le panneau
d’édition dédié et le terminal. Les boutons de la barre du haut les
affichent ou les masquent :

- **Explorateur** et **Aperçu** affichent ou masquent ces panneaux (au
  moins un panneau reste toujours visible) ;
- **Terminal** affiche ou masque le terminal (voir
  [le terminal](terminal.md)) ;
- le bouton au début de la barre d’onglets déplace les onglets d’édition
  dans le panneau d’édition dédié et les ramène (voir
  [l’éditeur](editor.md)).

Faites glisser la séparation entre deux panneaux pour les redimensionner ;
une petite étiquette affiche la taille pendant le glissement. `f` bascule
le plein écran.

## Espaces de travail

Un espace de travail est un ensemble complet de panneaux : son propre
dossier, ses dossiers dépliés, sa sélection, son tri, ses fichiers cachés,
ses tailles de panneaux, ses onglets d’édition, ses terminaux, son onglet
Recherche et son onglet Aide. Il peut y en avoir jusqu’à huit, comme des
bureaux virtuels.

| Touche | Action |
|---|---|
| `Alt+1` … `Alt+8` | Passer à l’espace de travail 1 … 8 |
| `Alt++` | Ajouter un espace de travail, avec la disposition par défaut |
| `Alt+-` | Fermer l’espace de travail actuel |

La carte des espaces de travail, dans la barre du haut, montre une petite
case par espace de travail, dessinée comme ses panneaux. Cliquez sur une
case pour y passer, sur **+** pour en ajouter un, faites un clic droit sur
une case pour son menu (**Enregistrer comme signet…**, **Fermer l’espace de
travail**) ou un clic du milieu pour le fermer.

## Dispositions enregistrées : `Alt+L`

Une disposition est un agencement de panneaux : lesquels sont visibles,
leurs tailles, si le terminal est à côté de l’aperçu ou dessous, les
panneaux de terminal, les divisions du panneau d’édition avec leurs onglets
de terminal, et les barres de symboles (affichées dans les onglets
d’édition, dans l’aperçu, et leur largeur ; les dispositions enregistrées
avant cela gardent les barres de symboles actuelles). Elle ne garde aucun
fichier : l’appliquer déplace les fichiers ouverts dans les volets
d’édition qui contenaient des fichiers (un volet vide en prend un au plus
rempli), réutilise les terminaux en cours et démarre les shells qui
manquent.

`Alt+L` (ou le bouton grille de la barre du haut) ouvre la liste des
dispositions sous le bouton : l’agencement actuel, la disposition par
défaut, celles enregistrées, puis les dispositions intégrées. Se déplacer dans la liste (survol à la
souris, flèches, ou `Alt+L` à nouveau pour la suivante) montre chaque
disposition exactement comme elle serait appliquée : panneaux, divisions et
onglets d’édition, panneaux de terminal. Rien ne se ferme entre-temps : les
terminaux pour lesquels une disposition n’a pas de place sont seulement mis
de côté, et les shells démarrés pour un aperçu s’arrêtent avec lui. `Enter`
ou un clic applique la disposition (en demandant d’abord quand elle ferme
des terminaux en cours) ; `Escape` remet tout comme avant, les mêmes
onglets aux mêmes endroits et les mêmes terminaux.

**Modifier les dispositions…**, en bas de la liste, ouvre le dialogue des
dispositions :

- **appliquer** une disposition enregistrée à l’espace de travail actuel ;
- **enregistrer** les panneaux actuels sous un nom ;
- l’enregistrer comme **disposition par défaut** pour les nouveaux espaces
  de travail ;
- **supprimer** une disposition, ou réinitialiser celle par défaut.

Trois dispositions sont intégrées, sous une ligne en fin de liste :

- **Code** : explorateur, aperçu et panneau d’édition, un terminal dessous ;
- **Terminal** : explorateur et aperçu, deux terminaux à côté ;
- **Focus** : l’aperçu seul.

Elles ne peuvent pas être supprimées. Enregistrer une disposition sous le
nom d’une disposition intégrée la remplace ; supprimer la vôtre fait
revenir celle intégrée.

Quand des fichiers sont ouverts, **Auto** vient en premier parmi elles : les
panneaux restent tels quels, l’explorateur prend la largeur de son plus long
nom visible (jusqu’à 40 % de la fenêtre), et les fichiers ouverts vont dans
le panneau d’édition, en grille selon leur nombre et la place que le panneau
a dans la fenêtre :

- un fichier : une zone ;
- deux : côte à côte quand le panneau est plus large que haut et a la place
  pour deux éditeurs d’environ 70 colonnes, l’une au-dessus de l’autre
  sinon ;
- trois : une à gauche et deux empilées à droite (panneau large), ou une en
  haut et deux côte à côte en dessous (panneau haut) ;
- quatre ou plus : deux par deux.

Aucune zone ne fait moins d’environ 70 colonnes sur 12 lignes : sans place
pour deux par deux, les zones forment une rangée (panneau large) ou une
colonne (panneau haut), et quand il y a moins de place que de fichiers, les
fichiers se partagent les zones en onglets. Le fichier courant reste dans
la première zone. Avec deux fichiers ou plus et l’aperçu à côté du panneau
d’édition, l’aperçu se rétrécit (jusqu’au quart de la place qu’ils se
partagent) quand cela laisse la place à deux éditeurs côte à côte.

Préférences (`Ctrl+,`) → **Dispositions** règle ce qu’Auto fait du terminal,
de l’aperçu et de la barre de symboles des onglets d’édition : les laisser
tels quels (par défaut), les afficher ou les masquer ; et où il met le terminal : où il est (par défaut), en bas ou à
droite (comme le fait `Ctrl+Shift+L`). La grille est faite pour la place qui
reste une fois ceux-ci placés.

Les dispositions sont enregistrées dans `~/.config/lookinfiles/layouts.ini`.
Appliquer une disposition garde ensuite ses barres de symboles activées ou
non, comme le font leurs touches ; un nouvel espace de travail garde les
barres de symboles actuelles, puisqu’elles sont partagées par tous les
espaces de travail.

## La session

Quand vous quittez (`Ctrl+Q` ou en fermant la fenêtre), LookinFiles
enregistre chaque espace de travail : son dossier, ses dossiers dépliés, sa
sélection, ses fichiers cachés, son tri, ses fichiers ouverts et ses
onglets de terminal et leur place (aperçu ou panneau d’édition, vues
divisées, onglet actuel de chaque groupe), les panneaux affichés, leurs
tailles et les panneaux de terminal. Le lancer sans argument les
restaure ; les terminaux démarrent de nouveaux shells. Les dossiers et
fichiers qui n’existent plus sont ignorés.

Le lancer avec un dossier ou un fichier démarre plutôt sur celui-ci
(`lookinfiles .` affiche toujours le dossier courant).

La session est enregistrée dans `~/.config/lookinfiles/session.ini`.
