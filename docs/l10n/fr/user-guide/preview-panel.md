# Le panneau d’aperçu

Sélectionnez un fichier dans l’explorateur et l’onglet **Aperçu** l’affiche
aussitôt, aussi proche que possible de ce que montrerait le programme
conçu pour ce format. Rien à installer : LookinFiles lit les formats
lui-même (les pages PDF demandent poppler, voir
[Prise en main](getting-started.md#installation)).

La liste complète des formats et de leurs limites est dans
[Aperçus pris en charge](../previews.md) (en anglais). Cette page explique
comment se servir du panneau.

## Ce que vous voyez

| Fichier | Aperçu |
|---|---|
| Texte et code source | Coloration syntaxique, numéros de ligne pour le code, barres de modification git |
| Markdown | Rendu comme une page web : titres, listes, listes de tâches, tableaux, blocs de code, images, formules TeX (`$...$`, `$$...$$`, `\(...\)`, `\[...\]`, environnements mathématiques, blocs ```` ```math ````) |
| HTML, Org-mode | Rendus ; Org-mode est interactif (voir plus bas) |
| Images | Ajustées au panneau, avec l’appareil, l’exposition, la date et la position GPS |
| PDF | Les pages |
| Vidéo et audio | Un lecteur |
| Word, texte OpenDocument | Les pages, comme imprimées : format et marges, en-tête et pied de page, polices, couleurs, listes, tableaux et images (zoom `+` / `-`) |
| RTF | Le document, avec ses polices, couleurs, listes et tableaux |
| Tableurs (`.xlsx`, `.ods`, `.xls`), CSV | Une grille avec les lettres des colonnes |
| Présentations (`.pptx`, `.ppt`, `.odp`) | Les diapositives, une par une, avec les notes de l’orateur |
| Courrier (`.eml`, `.msg`) et boîtes aux lettres | Les en-têtes, le texte, les pièces jointes ; les images distantes ne sont jamais chargées |
| JSON, XML, TOML, YAML, journaux, diffs, pages de manuel, agendas, contacts | Mis en forme et colorés |
| Archives (`.zip`, `.tar.gz`, `.deb`…) | La liste des fichiers, avec tailles et dates |
| Certificats et clés | Sujet, émetteur, validité, empreintes |
| Polices | Noms, détails et échantillons |
| Programmes et bibliothèques (ELF) | Architecture, bibliothèques liées, symboles exportés |
| Bases SQLite | Tables et vues avec leurs colonnes et premières lignes (ouvertes en lecture seule) |
| Tout le reste | Un vidage hexadécimal des 64 Ko du début |
| Un dossier | Totaux, types de fichiers, modifications git, tous les éléments, et son README rendu en dessous |

Le type vient du contenu du fichier, pas seulement de son nom : un script
sans extension ou un `.txt` qui est en fait un ZIP obtient le bon aperçu.
La barre en bas de l’aperçu affiche le nom, le type, la taille et
l’encodage de texte détecté.

## Vues rendue et brute

`m` bascule entre la vue rendue et la vue brute :

- JSON, XML : arbre ↔ texte indenté ;
- TOML, YAML : arbre ↔ tel qu’il est écrit ;
- Markdown, HTML, Org, CSV, XML, TOML, YAML, diffs, journaux : rendu ↔ texte source ;
- images : ajustée ↔ taille réelle ;
- PDF, Word, texte OpenDocument : pages ↔ texte ;
- vidéo et audio : lecteur ↔ détails du fichier ;
- documents, archives, certificats, programmes : résumé ↔ vidage
  hexadécimal.

## Se déplacer dans l’aperçu

Cliquez dans l’aperçu ou appuyez sur `Alt+Right` pour lui donner le focus.
Ensuite :

| Action | Tous les modes | Vim | Emacs |
|---|---|---|---|
| Défiler d’une ligne | `Down` / `Up` | `j` / `k` | `C-n` / `C-p` |
| Défiler d’une page | `Page Down` / `Page Up` | `Ctrl+F` / `Ctrl+B` | `C-v` / `M-v` |
| Début / fin | `Home` / `End` | `gg` / `G` | `M-<` / `M->` |
| Chercher dans le contenu | `Ctrl+F` (Notepad) | `/`, `n` / `N` | `C-s` / `C-r` |

`o` (ou `Ctrl+K Ctrl+O`, comme dans l’éditeur) affiche la **barre de
symboles** : les titres d’un fichier Markdown, HTML, Org, Word (`.docx`,
`.doc` : dans leur vue texte, `m`), OpenDocument ou EPUB, ou les signets d’un PDF (pour les pages
affichées), à côté du texte, imbriqués par niveau ; pour du code source,
ses fonctions, types… trouvés par l’analyseur intégré (sans serveur de
langage), regroupés par sorte (`s` : ordre du fichier). Elle suit le
défilement ; un clic ou `Enter` fait défiler jusqu’à un titre ou un
symbole, `Esc` revient au texte, `q` la masque. Une fois activée, elle
apparaît dans chaque aperçu de l’espace de travail qui a des titres ou des
symboles ; chaque espace de travail garde son propre choix, et les
nouveaux espaces de travail et le prochain démarrage suivent le dernier
fait avec `o` (aussi dans les Préférences, page Éditeur, qui règle tous
les espaces de travail). **Afficher / Masquer la barre de symboles** dans
le menu du clic droit de l’onglet Aperçu le fait pour l’espace de travail courant
seulement, sans le garder. Dans un gros fichier, seule la partie affichée
en premier est listée.

Le menu du clic droit de l’onglet Aperçu copie le nom du fichier affiché
(**Copier le nom**), son chemin depuis la racine de son dépôt git
(**Copier le chemin relatif git**, seulement dans un dépôt) ou son chemin
absolu (**Copier le chemin**).

`e` ouvre le fichier texte affiché dans un onglet d’édition ; `t` (Vim) ou
`C-o` (Emacs) l’ouvre dans le panneau d’édition dédié.

À droite, la **minicarte** montre tout le document en petit, avec les
barres de modification git le long de son bord. Cliquez ou faites glisser
dedans pour faire défiler. Pour la masquer (et retrouver la barre de
défilement), choisissez **Masquer la minicarte** dans le menu du clic droit
de l’onglet Aperçu (l’espace de travail courant seulement, sans le garder), ou
décochez **Afficher la minicarte** dans les Préférences, page Éditeur
(toutes les minicartes et tous les espaces de travail, gardé).

## Images et PDF

- `+` / `-` zooment et dézooment, `0` revient à la taille ajustée ;
  `Ctrl+molette` zoome aussi.
- Clic droit sur une image, puis **Copier l’image**, met l’image entière
  dans le presse-papiers.
- Dans un PDF, `Page Down` / `Page Up` vont à la page suivante et
  précédente.

## Vidéo et audio

`Space` ou `p` lance et met en pause, `Left` / `Right` reculent / avancent
de 5 secondes dans une vidéo (aussi depuis l’explorateur, sur la vidéo
affichée). Le lecteur a aussi ses propres commandes.

`Enter` sur une vidéo l’ouvre plutôt dans un onglet **Vidéo** du panneau
d’édition, où elle se lit : faites glisser l’onglet comme un onglet
d’édition (un autre volet, une division, le panneau d’aperçu). Dans
l’onglet, `Space` lance et met en pause, `Left` / `Right` se déplacent de
5 secondes, `Home` revient au début, `a` / `s` passent à la piste audio /
aux sous-titres suivants, `Ctrl+W` le ferme.

Quand une vidéo a plusieurs pistes audio (langues) ou sous-titres, le
lecteur affiche des boutons **Audio** et **Sous-titres** (sous le titre
dans l’aperçu, dans la barre du bas d’un onglet Vidéo) pour les choisir.
Les fichiers de sous-titres à côté de la vidéo et de même nom
(`Film.srt`, `Film.fr.srt` ; `.srt`, `.ass`, `.ssa`, `.vtt`) sont listés
avec ceux du fichier, et le premier est affiché dès le début. Il faut une
compilation avec GStreamer (`make deps`).

## Boîtes aux lettres

Une boîte aux lettres (`Inbox` de Thunderbird, `.mbox`…) affiche d’abord la
liste de ses messages ; `Page Down` / `Page Up` passent de la liste à
chaque message et reviennent. Les dossiers Maildir et MH affichent leurs
messages les plus récents au lieu de leurs sous-dossiers `cur` / `new` /
`tmp`.

## Fichiers Org-mode

L’aperçu Org-mode s’utilise sans ouvrir d’éditeur :

| Touche ou souris | Action |
|---|---|
| `Tab` / clic sur un titre | Replier ou déplier le titre |
| `Shift+Tab` | Faire tourner la visibilité de tout le fichier |
| `t` / clic sur le mot-clé | Faire tourner le mot-clé TODO (`TODO` → `DONE` → aucun, ou les mots-clés `#+TODO:` du fichier) |
| `Space`, `Enter` / clic | Cocher ou décocher une case (les parents et les compteurs `[n/m]` suivent) |
| Clic sur un lien | L’ouvrir |

Les modifications sont enregistrées dans le fichier. Voir
[Org-mode](org-mode.md) pour l’édition.

## Fichiers JSON, XML, TOML et YAML

Le JSON est affiché comme un arbre de clés et de valeurs sur deux
colonnes : chaque clé indentée selon sa profondeur, la valeur colorée selon
son type, et chaque objet ou tableau avec son nombre de clés ou d’éléments
et une flèche ▾ / ▸ qu’un clic replie ou déplie (`m` affiche plutôt le JSON
indenté et coloré). Le XML est affiché de la même façon : les attributs
(`@id`), éléments (`entry[2]`), textes (`#text`), sections CDATA et
commentaires de chaque élément sous lui, un élément qui ne contient qu’un
texte comme une valeur (`m` : le XML indenté et coloré). Le YAML et le
TOML sont aussi des arbres (`m` : tels qu’ils sont écrits et colorés, avec
leurs commentaires ; une table TOML contient les tables qui en dépendent,
un tableau de tables ses entrées). Leurs clés, éléments ou tables de
premier niveau sont dans la barre de symboles (`o`). `Tab` replie ou
déplie l’objet, le tableau, l’élément, la table, la correspondance ou la
séquence sous le curseur (sur une ligne de valeurs ou de texte : celui qui
l’entoure), surligné avec la ligne du curseur, et un bloc replié indique son nombre de clés, d’éléments ou
d’enfants ; une table TOML se replie avec les tables qui en dépendent
(`[servers.alpha]` sous `[servers]`). `Shift+Tab` affiche les membres de
premier niveau seulement, puis deux niveaux, puis tout. Un document trop
long pour être affiché en entier s’ouvre replié sous le niveau le plus
profond qui tient. Les replis de chaque fichier sont gardés jusqu’à la
fermeture de LookinFiles.

La barre du bas affiche le chemin de la valeur sous le curseur, par exemple
`$.servers[2].host` en JSON, `/feed/entry[2]/title` en XML,
`servers.alpha.ip` en TOML ou `jobs.build.steps[1].run` en YAML
(`[1].kind` dans le deuxième document d’un fichier qui en a plusieurs) ;
`y` (ou **Copier le chemin de la valeur** dans le menu du clic droit de
l’onglet Aperçu) le copie. Un
fichier de plus de 512 Ko affiche ses 64 premiers Ko, les valeurs ouvertes
à cet endroit se terminant par `…` ; **Charger tout le fichier** l’affiche
en entier. Un fichier qui n’est pas du JSON, du XML ou du TOML valide, ou
du YAML avec des tabulations dans son indentation ou des clés complexes
`?`, est affiché comme texte.

## Gros fichiers

Les fichiers texte de plus de 512 Ko affichent leurs 64 premiers Ko, et la
suite se charge en faisant défiler vers le bas (jusqu’à 8 Mo). Les longs
documents s’arrêtent après 256 Ko de texte, et l’aperçu le signale quand
une limite est atteinte. Passer à un autre fichier annule un aperçu encore
en cours de chargement.

## Code source

`Ctrl+clic` sur un nom dans l’aperçu d’un fichier source ouvre le fichier
où ce nom est défini (voir [la navigation dans le code](code-navigation.md)).

Quand le fichier affiché change sur le disque, l’aperçu se recharge tout
seul, en restant sur la ligne qu’il montrait. Quand le fichier est ouvert
dans un éditeur à côté de l’aperçu, l’aperçu suit le curseur de l’éditeur :
sa ligne (aussi dans le rendu Markdown et Org) est montrée à la hauteur du
curseur quand il se déplace. L’enregistrement laisse l’aperçu où il est.

## Moniteur système

Sous Linux et Windows, la barre du haut montre un petit graphique de
l’utilisation du processeur sur la dernière minute, avec sa dernière
valeur ; son info-bulle y ajoute la charge moyenne (Linux) et la mémoire
utilisée. Un clic dessus ouvre
l’onglet **Moniteur système** dans le panneau d’aperçu (un autre clic le
ferme) :

- **Processeur** (et une barre par cœur), **Mémoire** (et swap), **Charge
  moyenne**, **Réseau** (reçu et envoyé) et **E/S disque** (lu et écrit),
  chacun avec un graphique des deux dernières minutes, mis à jour chaque
  seconde ;
- les **Systèmes de fichiers** montés, avec leur taux de remplissage ;
- les **Processus**, d’abord par utilisation du processeur : un clic sur
  l’en-tête d’une colonne trie selon elle. Le processeur d’un processus
  compte 100 % pour un cœur entier.

Sous Windows, il n’y a ni carte de charge moyenne ni état des processus ;
les systèmes de fichiers sont les lecteurs fixes et amovibles, et les
processus des autres utilisateurs (services) n’affichent ni processeur, ni
mémoire, ni utilisateur, sauf si l’application tourne en administrateur.

L’onglet ne fait qu’afficher : il n’arrête ni ne modifie jamais un
processus. Les processus et les systèmes de fichiers ne sont lus que
pendant que l’onglet est à l’écran. `Ctrl+W` ou un clic du milieu sur
l’onglet le ferme.

## Espace disque

`Shift+S` dans l’explorateur (ou **Espace disque** dans le menu du clic
droit d’un dossier, ou le bouton **Espace disque** de l’aperçu d’un
dossier, après ses totaux) ouvre l’onglet **Espace disque** dans le panneau
d’aperçu : la place que prennent le dossier et tout ce qu’il contient. Sur
un fichier, c’est le dossier du fichier.

Le dossier est lu en arrière-plan : une barre montre où en est la lecture
(les dossiers terminés sur l’ensemble, ou, pour la racine d’un disque, la
part de son espace utilisé déjà lue), avec les fichiers, dossiers et octets
comptés jusque-là ; les dossiers déjà lus s’affichent au fur et à mesure.
`Escape` (ou le bouton **Arrêter**) arrête la lecture et garde ce qui a été
lu ; `F5` relit le dossier.

Ensuite, la liste montre les dossiers et les plus gros fichiers du dossier,
du plus gros au plus petit, chacun avec sa part du dossier, sa taille, son
nombre de fichiers et sa dernière modification (un clic sur l’en-tête d’une
colonne trie selon elle). `Enter` ouvre un dossier, `Backspace`
revient ; `Enter` sur un fichier le montre dans l’explorateur. Un clic
droit sur une ligne permet aussi de copier son chemin.

Sous la liste, une carte montre le même dossier en rectangles imbriqués,
chacun aussi grand que la place qu’il prend : les dossiers avec leur nom et
leur contenu (sur trois niveaux), les fichiers colorés selon leur type
(documents, images, vidéos, audio, archives, texte et code, programmes). La
ligne sélectionnée dans la liste est entourée ; un clic sur un rectangle
sélectionne sa ligne, un double clic ouvre un dossier ou montre un fichier
dans l’explorateur, et le pointeur posé sur un rectangle donne son chemin
et sa taille. Faites glisser la ligne entre la liste et la carte pour
partager la hauteur.

À côté de la liste, une fois le dossier lu, ses statistiques : tailles (et
la place occupée sur le disque), fichiers, dossiers, taille moyenne d’un
fichier, fichiers et dossiers vides, liens, la plus ancienne et la plus
récente modification, le dossier le plus profond ; puis la place prise
par type de fichier (documents, images, vidéos, audio, archives, texte et
code, programmes), par dernière modification (moins d’un jour, d’une
semaine, d’un mois, d’un an, 1 à 3 ans, plus) et par extension, les plus
gros fichiers de tout le dossier (un clic en montre un dans
l’explorateur), et les éléments illisibles.

Les liens symboliques ne sont pas suivis, un fichier qui a plusieurs liens
physiques ne compte qu’une fois, et les autres disques montés dans le
dossier sont laissés de côté. Le résumé sous le titre donne aussi la place
réellement occupée sur le disque et le nombre d’éléments illisibles.
`Ctrl+W` ou un clic du milieu sur l’onglet le ferme et arrête la lecture.
