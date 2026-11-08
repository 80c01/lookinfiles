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
| Barre de symboles (à côté du texte) | `Ctrl+K Ctrl+O` | `M-x imenu-list` | `:TagbarToggle` |

- Redemander la définition sur une définition passe au candidat suivant ;
  la ligne de message dit lequel est affiché et combien il y en a.
- L’onglet **Références**, à côté de **Commits Git**, liste les
  utilisations regroupées par fichier. `Enter` ou un double-clic en ouvre
  une ; la croix ferme l’onglet.
- Les autres utilisations du nom sous le curseur sont surlignées.
- `Ctrl+clic` marche aussi dans l’aperçu d’un fichier source.

La **barre de symboles** affiche les symboles d’un fichier de code à côté
du texte, regroupés par sorte (fonctions, types…) et imbriqués dans leur
struct ou leur classe, ou les titres d’un fichier Markdown. Elle suit le
curseur et se rafraîchit peu après chaque modification. Un clic ou `Enter`
va au symbole (retour avec la touche habituelle), `s` bascule entre le
regroupement par sorte et l’ordre du fichier, `Esc` revient au texte, `q`
la masque. Elle est affichée dans tous les onglets de code et Markdown ou
dans aucun (gardé pour le prochain démarrage), et sa largeur est partagée
par les onglets.

Dans un fichier Markdown, la liste des symboles et la barre de symboles
affichent les titres.

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

Avec un serveur de langage, les erreurs sont soulignées en rouge et les
avertissements en jaune, comptés dans la barre de l’onglet d’édition, et
expliqués quand la souris reste dessus.

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
