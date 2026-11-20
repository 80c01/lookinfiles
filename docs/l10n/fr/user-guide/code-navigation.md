# Navigation dans le code

Dans les fichiers source, les onglets d’édition peuvent sauter à l’endroit
où un nom est défini, lister où il est utilisé et montrer ce qu’il est,
dans les trois modes.

## Les touches

| Action | Notepad | Emacs | Vim |
|---|---|---|---|
| Aller à la définition | `F12`, `Ctrl+clic` | `M-.`, `Ctrl+clic` | `gd`, `Ctrl+]`, `Ctrl+clic` |
| Aller à la déclaration | `Ctrl+F12` | `C-u M-.` | `gD` |
| Revenir / repartir en avant | `Ctrl+Alt+-` / `Ctrl+Shift+-` | `M-,` / `C-M-,` | `Ctrl+O` / `Tab` |
| Fiche d’aide | `Ctrl+K Ctrl+I`, laisser la souris dessus | `C-h .`, souris | `K`, souris |
| Signature de l’appel en cours de frappe | automatique, `Ctrl+Shift+Space` | dans la zone d’écho | automatique en mode Insertion |
| Références | `Shift+F12` | `M-?` | `gr` |
| Symboles du fichier | `Ctrl+Shift+O` | `M-g i` | `:Symbols` |
| Symboles du projet | `Ctrl+Alt+O` | `M-g s` | `:WSymbols` |
| Panneau Problèmes (diagnostics) | `Ctrl+Shift+M` | `M-x flymake-show-buffer-diagnostics` | `:Problems` |
| Diagnostic suivant / précédent | `F8` / `Shift+F8` | `M-g n` / `M-g p` | `]d` / `[d` |
| Autocomplétion | `Ctrl+Space` (et auto) | `C-M-i`, `M-/` (et auto) | `Ctrl+Space` (Insertion) |
| Renommer le symbole (LSP) | `Shift+F2`, `Ctrl+K Ctrl+R` | `M-x lsp-rename` | `:Rename` |
| Barre de symboles (à côté du texte) | `Ctrl+K Ctrl+O` | `M-x imenu-list` | `:TagbarToggle` |

- Redemander la définition sur une définition passe au candidat suivant ;
  la ligne de message dit lequel est affiché et combien il y en a.
- L’onglet **Références**, à côté de **Commits Git**, liste les
  utilisations regroupées par fichier. `Enter` ou un double-clic en ouvre
  une ; la croix ferme l’onglet. Le retour (`Ctrl-O` en Vim, `M-,` en
  Emacs, `Ctrl+Alt+-` en Notepad), depuis la liste ou depuis la référence
  ouverte, ramène là où la recherche a commencé.
- Les autres utilisations du nom sous le curseur sont surlignées.
- `Ctrl+clic` marche aussi dans l’aperçu d’un fichier source.
- Les symboles du projet se cherchent aussi depuis l’explorateur :
  `Ctrl+Alt+O` (`M-g s` en mode Emacs) cherche dans le projet du dossier
  courant, dans tous les langages.
- Dans les symboles du fichier et du projet, la liste à côté du champ de recherche ne
  garde qu’une sorte (fonctions, macros, types…), chacune avec le nombre
  de symboles trouvés ; `Alt+K` / `Alt+Shift+K` passent à la sorte
  suivante / précédente qui en a.

La **barre de symboles** affiche les symboles d’un fichier de code à côté
du texte, regroupés par sorte (fonctions, types…) et imbriqués dans leur
struct ou leur classe, ou les titres d’un fichier Markdown. Elle suit le
curseur et se rafraîchit peu après chaque modification. Un clic ou `Enter`
va au symbole (retour avec la touche habituelle), `s` bascule entre le
regroupement par sorte et l’ordre du fichier, `Esc` revient au texte, `q`
la masque ; `Alt+Right` depuis le texte y entre, `Alt+Left` en revient (aussi les touches de panneau du mode de l’onglet : `Ctrl+W l` / `Ctrl+W h` en Vim, `C-x o`, `F6` / `Shift+F6`) ; son en-tête porte une ligne d’accent tant qu’elle a le focus. Dedans, les touches du mode de l’onglet déplacent (`j` / `k`, `gg` / `G`, `Ctrl+F` / `Ctrl+B`, `h` / `l` replient / déplient une ligne en Vim ; `C-n` / `C-p`, `M-<` / `M->`, `C-v` / `M-v`, `C-b` / `C-f` en Emacs ; les flèches, `Page Up` / `Page Down`, `Ctrl+Home` / `Ctrl+End` dans tous les modes) et cherchent dans les noms, lignes repliées comprises, dans une barre en bas (`/` puis `n` / `N` en Vim, `C-s` / `C-r` en Emacs, `Ctrl+F` puis `F3` / `Shift+F3` en Notepad ; `Enter` ou `Esc` reste sur le résultat, `C-g` revient au départ) ; la barre de symboles de l’aperçu prend les touches du mode global. Chaque onglet affiche ou masque la sienne (un
nouvel onglet suit les Préférences, page Éditeur), et
sa largeur est partagée par les onglets.

Dans un fichier Markdown, la liste des symboles et la barre de symboles
affichent les titres.

En mode Vim, comme dans Vim, la barre de symboles et le texte ont chacun
leur ligne de statut : `Tagbar`, puis `Name` (groupé par type) ou `Order`
(ordre du fichier) et le fichier sous la barre de symboles ; la ligne de
commande reste sous les deux, et `/` dans la barre de symboles y fait sa
recherche. Comme dans airline, la ligne de statut du côté qui n’a pas le
focus est grisée.

## D’où viennent les réponses

LookinFiles utilise la meilleure source qu’il trouve pour le projet (le
dépôt git du fichier, sinon son dossier), et se rabat sur la suivante :

| Source | Qualité | Besoin |
|---|---|---|
| Serveur de langage | Définitions et références exactes, documentation, erreurs et avertissements | Un serveur comme `clangd`, `pylsp`, `gopls`, `rust-analyzer` |
| Universal Ctags | Bonnes définitions, signatures | `ctags` installé |
| Index intégré | Bonnes estimations pour C, C++, Java, C#, Python, Shell, JavaScript / TypeScript, Go, Rust, Lua, Ruby, Makefiles | Rien |

La barre en bas d’un onglet d’édition indique la source utilisée : le nom
du serveur, `ctags` ou l’index intégré.

Avec un serveur de langage, les erreurs sont surlignées en rouge et les
avertissements soulignés en jaune, comptés dans la barre de l’onglet d’édition,
listés dans le panneau **Problèmes** et expliqués quand la souris reste dessus.
Pour les fichiers JSON, TOML, YAML et XML, les parseurs intégrés fournissent
automatiquement des diagnostics de syntaxe en direct sans serveur externe.

Sans serveur, les références sont des correspondances de mots entiers
(commentaires et chaînes compris), et quand aucune définition n’est trouvée
l’éditeur va à la première utilisation du nom dans la fonction actuelle.

## Configurer un serveur de langage

Installez le serveur de votre langage ; LookinFiles le démarre quand vous
ouvrez un fichier de ce langage. Pour changer la commande, ou désactiver un
serveur, écrivez `~/.config/lookinfiles/lsp.ini` :

```ini
[servers]
c=clangd --background-index
python=pyright-langserver --stdio
rust=none
```

Voir [Configuration](../configuration.md#language-servers) (en anglais)
pour la liste des serveurs recherchés. Pour C et C++, `clangd` marche mieux
avec un `compile_commands.json` à la racine du projet.

## Ctags

Quand [Universal Ctags](https://ctags.io) est installé, son index est
utilisé et tenu à jour dans `~/.cache/lookinfiles/tags/`. Il ajoute aussi
Objective-C, PHP, Perl, Kotlin, Swift, Scala, Dart, Haskell, Elixir,
Erlang, Julia, R et Tcl.
