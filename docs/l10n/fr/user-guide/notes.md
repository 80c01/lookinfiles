# Notes

La vue des notes est un gestionnaire de carnets intégré à LookinFiles : des
notes rangées dans des carnets, avec une corbeille. Ouvrez-la avec `Ctrl+U`
(à nouveau pour revenir aux fichiers) ou le bouton des notes en haut de
l’explorateur.

Un premier carnet, **General**, contient une note de bienvenue.

## Formats de note

| Format | Fichier | Édité dans |
|---|---|---|
| Markdown | `.md` | Un onglet d’édition ; l’aperçu l’affiche rendu |
| Org-mode | `.org` | Un onglet d’édition Org-mode (voir [Org-mode](org-mode.md)) |
| Texte brut | `.txt` | Un onglet d’édition |
| Canvas | un dossier `.canvas` | L’onglet **Canvas** (voir plus bas) |

Le menu du clic droit convertit une note dans un autre format texte, et
exporte une note Canvas en note Markdown.

## Travailler avec les notes

| Touche | Action |
|---|---|
| `a` | Nouvelle note dans le carnet sélectionné (demande un titre et un format) |
| `Shift+A` | Nouveau carnet |
| `Enter` | Éditer la note |
| `F2`, `r` ou `e` | Modifier le titre, le format et le carnet de la note, ou le nom et l’icône du carnet |
| `i` | Changer l’icône du carnet |
| `m` | Déplacer la note dans un autre carnet |
| `d` ou `Delete` | Mettre la note à la corbeille, ou supprimer le carnet |
| `Shift+R` | Restaurer la note sélectionnée de la corbeille dans son carnet |
| `Shift+K` / `Shift+J` | Monter / descendre la note ou le carnet |
| `o` | Trier : ordre manuel, A–Z, Z–A |

Sélectionner une note l’affiche dans l’aperçu ; sélectionner un carnet
affiche un résumé. La **Corbeille**, en bas de l’arborescence, garde les
notes supprimées jusqu’à ce que vous la vidiez (clic droit sur la
Corbeille, **Vider la corbeille…**).

## Ouvrir une note depuis la ligne de commande

```bash
lookinfiles -n Idées               # la note « Idées », dans n’importe quel carnet
lookinfiles -n Travail/Réunion.org # « Réunion » dans le carnet « Travail », en Org-mode
```

Une note qui n’existe pas est créée, avec son carnet. L’extension (`.md`,
`.org`, `.txt`) fixe le format d’une nouvelle note.

## Notes Canvas

Une note Canvas est un espace libre de zones de texte, de formes dessinées
à main levée, de tableaux et d’images, placés où vous voulez. Elle s’ouvre
dans un onglet du panneau d’édition, nommé d’après le canvas, et
s’enregistre toute seule. Plusieurs canvas peuvent être ouverts à la fois,
chacun dans son onglet ; faites glisser un onglet comme un onglet d’édition
vers un autre volet, vers le bord d’un volet pour le diviser, ou vers le
panneau d’aperçu. Un onglet canvas appartient à l’espace de travail où il a
été ouvert : il se masque avec l’espace de travail et revient avec lui (l’ouvrir
depuis un autre espace de travail l’y déplace).

### Outils et blocs

Une barre d’outils flottante se trouve en haut de la page. Choisissez un
outil, puis appuyez et faites glisser sur la page ; la lettre entre
parenthèses le choisit au clavier (quand vous n’êtes pas en train de taper
dans un bloc).

| Outil | Usage |
|---|---|
| Sélection (`V`) | Sélectionner, déplacer et redimensionner des blocs ; faites glisser sur la page vide pour sélectionner les blocs dans le cadre (`Shift` : les ajouter) |
| Rectangle (`R`), Losange (`D`), Ellipse (`O`) | Faites glisser pour dessiner la forme (`Shift` : carré, cercle) ; un clic en crée une de taille par défaut ; double-cliquez dessus (ou `Enter` quand elle est sélectionnée) pour taper du texte à l’intérieur, centré |
| Flèche (`A`), Ligne (`L`) | Faites glisser du début à la fin (`Shift` : multiple de 45 degrés) ; une extrémité tracée sur un bloc y est attachée et le suit quand il bouge (déplacer la flèche seule la libère) |
| Crayon (`P`) | Dessiner à main levée ; le crayon reste choisi pour le trait suivant |
| Zone de texte (`T`) | Cliquez à l’endroit du texte (un double-clic ou `Ctrl+Enter` marchent aussi) |
| Tableau (`B`) | Cliquez à l’endroit du tableau : une grille 3 × 3 de cellules modifiables avec les commandes `+ Row`, `- Row`, `+ Col`, `- Col` |
| Gomme (`E`) | Cliquez ou faites glisser sur des blocs pour les supprimer |

Après le dessin d’une forme, l’outil revient à Sélection (`Esc` aussi).
Le menu au bout de la barre contient **Snap to Grid** (aimanter à la
grille), les commandes d’alignement, l’ordre de dessin, **Lock / Unlock**
(verrouiller / déverrouiller), **Crop Selected Picture** (recadrer l’image
sélectionnée) et **Export as PNG** / **Export as SVG** : une image des blocs
(couleurs du thème, PNG au double de l’échelle de la page) écrite à côté de
la note sous le nom `<titre>.png` ou `<titre>.svg`.

Les images viennent de `Ctrl+V` avec une image copiée, ou de fichiers image
déposés sur la page ; double-cliquez sur une image pour la recadrer.

### Panneau des propriétés et barre de mise en forme

- **Formes** : un panneau à gauche (quand un outil de dessin est choisi, ou
  qu’une forme est sélectionnée) règle la couleur du trait, le remplissage
  (aucun, hachuré, plein ; formes fermées seulement), l’épaisseur du trait
  et le style dessiné à la main ou net. Le choix s’applique aux formes
  sélectionnées et aux suivantes.
- **Texte** : quand une zone de texte est éditée ou sélectionnée, une barre
  flotte juste au-dessus (en dessous près du haut de la page) et la suit :
  style de paragraphe (titres, corps, citation), gras, italique, souligné,
  barré, couleur du texte, autres styles (surlignage, code, polices,
  tailles), listes, cases à cocher et retrait. Gras, italique, souligné et
  barré apparaissent enfoncés quand le texte au curseur (ou toute la
  sélection) les a.

### Actions et raccourcis du canvas

| Action | Comment |
|---|---|
| Déplacer un bloc | Faites glisser la barre de prise au-dessus (ou faites glisser directement une image ou une forme) |
| Redimensionner un bloc | Faites glisser son coin inférieur droit (les images gardent leurs proportions) |
| Faire pivoter une forme | Sélectionnez un rectangle, un losange ou une ellipse seul et faites glisser la poignée ronde au-dessus : elle tourne autour de son centre avec son texte (`Shift` : pas de 15 degrés) ; les flèches qui y sont attachées restent sur son contour |
| Modifier une flèche ou une ligne | Cliquez sur son trait pour la sélectionner : une poignée apparaît sur chaque point. Faites glisser une extrémité n’importe où (déposée sur un bloc, elle s’y attache), faites glisser la poignée pâle au milieu d’un segment pour ajouter un point et le courber, faites glisser un point pour changer la forme (`Shift` : multiple de 45 degrés), double-cliquez sur un point entre les extrémités pour le supprimer (ou cliquez dessus, plein une fois choisi, puis `Delete`) |
| Sélectionner plusieurs blocs | Faites glisser sur la page vide autour d’eux, ou `Shift+clic` sur chacun (à nouveau pour le retirer) ; en déplacer un les déplace tous |
| Aimantation et guides | Aimantation aux bords des blocs voisins et à la grille (**Snap to Grid** dans le menu de la barre d’outils) |
| Aligner des blocs | Sélectionnez deux blocs ou plus, puis le menu de la barre d’outils : gauche, centres, droite, haut, milieux, bas, répartir |
| Styles prédéfinis | Menu du style de paragraphe de la barre de mise en forme : Titre 1, Titre 2, Corps de texte, Citation (`Ctrl+Alt+1`, `2`, `0`, `Q`) |
| Mise en forme du texte | `Ctrl+B` (gras), `Ctrl+I` (italique), `Ctrl+U` (souligné), `Ctrl+Shift+S` (barré), surlignage, chasse fixe, police, couleur du texte |
| Listes et retraits | Liste à puces, liste numérotée, augmenter / diminuer le retrait dans la barre de mise en forme ; Enter continue la liste |
| Cases à cocher | Bouton case à cocher ou tapez `- [ ] ` ; cliquez sur `[ ]` / `[X]` ou appuyez sur `Ctrl+Space` (ou `Ctrl+Shift+C`) pour basculer |
| Sélectionner le bloc / arrêter d’éditer | `Esc` |
| Bloc suivant / précédent | `Tab` / `Shift+Tab` |
| Déplacer les blocs sélectionnés | Flèches (`Shift` pour de plus grands pas) |
| Éditer la zone de texte sélectionnée, ou le texte d’une forme | `Enter` (une forme : aussi un double-clic) |
| Supprimer les blocs sélectionnés | `Delete` ou `Backspace` |
| Ordre de dessin | `Ctrl+]` / `Ctrl+[` d’un cran vers le haut / le bas, `Ctrl+Shift+]` / `Ctrl+Shift+[` au premier / dernier plan (aussi dans le menu de la barre d’outils) ; déplacer un bloc garde sa place dans l’ordre |
| Verrouiller des blocs | `Ctrl+Shift+K` (ou le menu de la barre d’outils) sur les blocs sélectionnés : ils ne peuvent plus être déplacés, redimensionnés, supprimés ni pris par un cadre de sélection (bordure en pointillés quand ils sont sélectionnés ; le texte reste modifiable) ; à nouveau pour déverrouiller |
| Copier, couper, coller, dupliquer | `Ctrl+C`, `Ctrl+X`, `Ctrl+V` (au pointeur, aussi dans une autre note Canvas, images comprises), `Ctrl+D` ; les flèches copiées avec leurs blocs restent attachées aux copies |
| Zoom | `Ctrl` + molette (autour du pointeur) ou `Ctrl+=` / `Ctrl+-` ; `Ctrl+0`, ou un clic sur l’échelle au-dessus de la page, revient à 100 % |
| Déplacer la vue | Faites glisser avec le bouton du milieu, ou maintenez `Space` et faites glisser (hors saisie dans un bloc) |
| Annuler / rétablir | `Ctrl+Z` / `Ctrl+Shift+Z` (ou `Ctrl+Y`) : blocs ajoutés, supprimés, déplacés, redimensionnés, pivotés, restylés, alignés ou recadrés ; pendant la saisie dans une zone de texte ou une cellule, ils annulent son texte |
| Fermer l’onglet Canvas | `Ctrl+W`, ou clic du milieu sur l’onglet |
| Déplacer l’onglet Canvas | `Ctrl+Alt+flèche`, le menu de l’onglet (clic droit : Déplacer à droite / à gauche / en bas / en haut, Fermer l’onglet), ou faites glisser l’onglet |

## Où sont gardées les notes

Chaque carnet est un dossier de `~/.config/lookinfiles/notes/`, et chaque
note un fichier normal dedans : vous pouvez les sauvegarder, les
synchroniser ou les ouvrir avec d’autres programmes.
