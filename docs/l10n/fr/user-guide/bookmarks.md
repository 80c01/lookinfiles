# Signets

Les signets sont des liens nommés vers des dossiers, des fichiers ou des
pages web, rangés dans vos propres dossiers. Ouvrez la vue des signets avec
`Ctrl+A` (à nouveau pour revenir aux fichiers) ou le bouton signet en haut
de l’explorateur.

## Ajouter un signet

- Depuis n’importe quelle vue, `Ctrl+D` ajoute un signet sur l’élément
  sélectionné, ou sur le dossier courant.
- Dans la vue des signets, `a` ajoute un signet dans le dossier
  sélectionné. Le dialogue demande un **Nom**, un **Emplacement** (un
  chemin ou une URL ; le bouton **Dossier courant** remplit le dossier de
  l’explorateur) et un **Dossier**.
- `Shift+A` crée un dossier de signets, avec un **Nom**, une **Icône** et
  un **Dossier parent**.

## Enregistrer un espace de travail

Faites un clic droit dans la vue des signets et choisissez **Enregistrer
l’espace de travail comme signet…** pour garder l’espace de travail actuel
sous un nom, ou faites un clic droit sur une case de la carte des espaces
de travail, dans la barre du haut, et choisissez **Enregistrer comme
signet…** pour garder cet espace : son dossier, les dossiers dépliés, la
sélection, les fichiers cachés, le tri, les onglets d’édition ouverts et
leur place, les panneaux, leurs tailles et les panneaux de terminal. Le
signet va dans le dossier sélectionné et affiche une icône d’espace de
travail.

L’ouvrir (`Enter`) restaure cet espace de travail dans un **nouvel** espace
de travail ; l’actuel n’est pas touché, et les huit espaces de travail ne
doivent pas être tous utilisés. Les terminaux démarrent de nouveaux shells.
Un dossier manquant l’arrête avec un message ; les fichiers qui n’existent
plus sont ignorés. Renommez-le ou déplacez-le avec `F2` ; son emplacement
ne peut pas être modifié. Supprimer le signet supprime l’espace de travail
enregistré.

## Utiliser les signets

| Touche | Action |
|---|---|
| `Enter` | Ouvrir : un dossier devient le dossier de l’explorateur, un fichier s’ouvre, une URL s’ouvre dans votre navigateur web, un espace de travail enregistré s’ouvre dans un nouvel espace de travail |
| `Right` / `Left` | Ouvrir / fermer un dossier de signets |
| `F2`, `r` ou `e` | Modifier le nom, l’emplacement et le dossier (un dossier : son nom, son icône et son dossier parent) |
| `d` ou `Delete` | Supprimer le signet ou le dossier |
| `Shift+K` / `Shift+J` | Monter / descendre l’élément |
| `o` | Trier : A–Z, Z–A |

Sélectionner un dossier de signets affiche un résumé de son contenu dans
l’aperçu.

Quand le dossier ou le fichier d’un signet n’existe plus, l’ouvrir propose
de le supprimer.

Les signets sont enregistrés dans `~/.config/lookinfiles/bookmarks.xml` ;
les espaces de travail enregistrés sont dans
`~/.config/lookinfiles/sessions/`.
