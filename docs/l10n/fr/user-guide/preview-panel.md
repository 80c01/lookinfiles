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
| Word, OpenDocument, RTF | Le document, avec ses polices, couleurs, listes, tableaux et images |
| Tableurs (`.xlsx`, `.ods`, `.xls`), CSV | Une grille avec les lettres des colonnes |
| Présentations (`.pptx`, `.ppt`, `.odp`) | Les diapositives, une par une, avec les notes de l’orateur |
| Courrier (`.eml`, `.msg`) et boîtes aux lettres | Les en-têtes, le texte, les pièces jointes ; les images distantes ne sont jamais chargées |
| JSON, journaux, diffs, pages de manuel, agendas, contacts | Mis en forme et colorés |
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

- Markdown, HTML, Org, CSV, JSON, diffs, journaux : rendu ↔ texte source ;
- images : ajustée ↔ taille réelle ;
- PDF : pages ↔ texte extrait ;
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
`.doc`), OpenDocument ou EPUB, ou les signets d’un PDF (pour les pages
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
le menu du clic droit de l’aperçu le fait pour l’espace de travail courant
seulement, sans le garder. Dans un gros fichier, seule la partie affichée
en premier est listée.

`e` ouvre le fichier texte affiché dans un onglet d’édition ; `t` (Vim) ou
`C-o` (Emacs) l’ouvre dans le panneau d’édition dédié.

À droite, la **minicarte** montre tout le document en petit, avec les
barres de modification git le long de son bord. Cliquez ou faites glisser
dedans pour faire défiler. Pour la masquer (et retrouver la barre de
défilement), choisissez **Masquer la minicarte** dans le menu du clic droit
de l’aperçu (l’espace de travail courant seulement, sans le garder), ou
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
seul.
