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
une petite étiquette affiche la taille pendant le glissement. Pointez une
séparation et une petite barre apparaît à côté de son milieu (encore une
seconde après que le pointeur l’a quittée) : ses boutons aux extrémités replient le panneau de ce côté (ou le déplient à sa
taille, une fois replié), ceux du milieu placent la séparation au tiers, à
la moitié ou aux deux tiers. La séparation de l’explorateur propose plutôt
les largeurs utiles à un panneau latéral : un cinquième, un quart ou un
tiers de la fenêtre, et un bouton qui ajuste l’explorateur à son nom visible
le plus long (au plus 40 % de la fenêtre ; recliquez-le après avoir ouvert
d’autres dossiers). Un double clic sur une séparation la met à la moitié,
ou ajuste l’explorateur sur sa séparation. L’aperçu, l’explorateur et le panneau du terminal ne se replient
pas depuis une séparation (ils ont leur propre bouton dans la barre du
haut) : la séparation entre l’aperçu et le panneau d’édition ne replie que
le panneau d’édition. La séparation d’un panneau replié apparaît comme une
bande colorée au bord ; un clic dessus déplie le panneau.
`f` bascule le plein écran.

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
d’édition, dans l’aperçu, et leur largeur ; appliquer la disposition règle
la barre de symboles de chaque onglet d’édition ; les dispositions
enregistrées avant cela gardent les barres de symboles actuelles). Elle ne garde aucun
fichier : l’appliquer déplace les fichiers ouverts dans les volets
d’édition qui contenaient des fichiers (un volet vide en prend un au plus
rempli), réutilise les terminaux en cours et démarre les shells qui
manquent ; les terminaux en cours pour lesquels elle n’a pas de place
deviennent des onglets de son dernier panneau de terminal, si bien
qu’appliquer une disposition n’en ferme jamais.
Elle garde aussi les onglets **Problèmes**, **Références**, **Recherche**,
**Build** et **Deep Search** qui étaient ouverts, et leur emplacement
(l’aperçu ou un volet d’édition ; un volet qui ne contient que de tels
onglets leur reste réservé) : l’appliquer ouvre ceux qui sont fermés, sans
leur donner le focus (un onglet vide indique comment le remplir :
`Shift+F12`, `Ctrl+Shift+F`, enregistrer un fichier `.tex`), et les y
place ; un onglet ouvert qu’elle ne cite pas reste où il est.

`Alt+L` (ou le bouton grille de la barre du haut) ouvre la liste des
dispositions sous le bouton : l’agencement actuel, la disposition par
défaut, celles enregistrées, puis les dispositions intégrées. Se déplacer dans la liste (survol à la
souris, flèches, ou `Alt+L` à nouveau pour la suivante) montre chaque
disposition exactement comme elle serait appliquée : panneaux, divisions et
onglets d’édition, panneaux de terminal. Rien ne se ferme entre-temps : les
terminaux pour lesquels une disposition n’a pas de place sont empilés en
onglets, et les shells démarrés pour un aperçu s’arrêtent avec lui. `Enter`
ou un clic applique la disposition ; `Escape` remet tout comme avant, les
mêmes onglets aux mêmes endroits et les mêmes terminaux.

Chaque espace de travail conserve aussi un historique de ses dispositions
(panneaux affichés ou masqués, redimensionnements, divisions, onglets
déplacés d’un volet à un autre, barres de symboles et dispositions
appliquées) : les flèches à gauche et à droite du bouton grille (ou
`Alt+Shift+Left` / `Alt+Shift+Right`) permettent de revenir à la
disposition précédente ou d’avancer à la suivante, sans fermer les
terminaux en cours.

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
non, et les onglets d’édition ouverts ensuite la suivent, comme après les
Préférences ; cela vaut dans tous les espaces de travail.

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
