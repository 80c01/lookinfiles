# Diagnostics

L’onglet **Diagnostics** liste ce que LookinFiles peut utiliser sur cet
ordinateur et ce qui manque, avec la commande qui installe chaque élément
manquant. Ouvrez-le depuis le bouton **+** au bout de la barre d’onglets :
**+** → **Diagnostics**.

## Ce qui est vérifié

- **Intégré** : les bibliothèques facultatives avec lesquelles
  cette copie de LookinFiles a été compilée (terminal intégré, coloration
  syntaxique, aperçu PDF, bases de données, archives, onglets de
  navigateur…). Une bibliothèque absente ici ne s’ajoute pas après coup :
  installez le paquet de développement indiqué, puis recompilez LookinFiles
  (`make`), ou utilisez un paquet de LookinFiles compilé avec elle.
- **Programmes** : les programmes que LookinFiles lance quand
  une fonction en a besoin, cherchés dans le `PATH` : Universal Ctags (plan
  du code, aller à la définition), LaTeX (`latexmk`, `tectonic` ou
  `pdflatex`), Ghostscript (aperçu PostScript et EPS), `dvips` (aperçu
  DVI), `ssh` (machines distantes), Python et ses modules `jupyter_client`
  et `ipykernel` (exécution des cellules des notebooks). Ils fonctionnent
  dès qu’ils sont installés, sans redémarrer.
- **Serveurs de langage** : le serveur de chaque langage
  (voir [la navigation dans le code](code-navigation.md)). Ceux des langages
  du dossier affiché viennent en premier ; **Afficher N autres langages**
  déplie les autres.

Une vérification réussie montre ce qui a été trouvé (le chemin du
programme) ; une vérification manquée montre la commande d’installation
pour le gestionnaire de paquets de ce système (`apt`, `dnf`, `pacman`,
`zypper`, `apk` ou Homebrew, lu dans `/etc/os-release`) et un bouton
**Copier**. Certains programmes ne sont pas empaquetés partout : la
commande est alors celle de leur propre installateur (`pipx`, `npm`, `go`,
`rustup`…). LookinFiles ne lance jamais une commande d’installation
lui-même.

L’en-tête nomme le système et le nombre de vérifications manquées. **Vérifier à
nouveau** (le bouton d’actualisation) relance les vérifications après une
installation.

| Touche | Action |
|---|---|
| `Ctrl+W` | Fermer l’onglet Diagnostics (`Ctrl+W c` en mode Vim) |
| Clic du milieu sur l’onglet | Le fermer |

## Notebooks et environnements virtuels

La vérification de Python utilise le `python3` du `PATH`. Un notebook dont
le dossier a un environnement virtuel y lance son noyau (voir le menu du
noyau des notebooks) : installez aussi `jupyter_client` et `ipykernel` dans
cet environnement (`pip install jupyter_client ipykernel`, l’environnement
activé).
