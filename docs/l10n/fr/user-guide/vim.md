# Mode Vim

Le mode Vim est un moteur Vim avec la vraie grammaire, pas une poignée de
raccourcis : `["x][nombre]opérateur[nombre]mouvement`, objets texte,
registres, marques, la répétition par le point, macros, bloc visuel et
commandes Ex sur des plages.

Pour l’utiliser partout, démarrez avec `lookinfiles -e vim` ou choisissez
**Vim** dans la barre d’état ; l’explorateur et l’aperçu se déplacent alors
aussi avec `j` / `k`, `gg` / `G`, `/`, `n` / `N`. Pour l’utiliser dans un
seul onglet, faites un clic droit sur le titre de l’onglet et choisissez
**Vim**.

## La ligne d’état

En bas de l’onglet : le badge du mode (NORMAL, INSERT, VISUAL…), les
touches tapées jusque-là, `recording @x` pendant l’enregistrement d’une
macro, et la règle. Les messages restent en anglais, comme dans Vim.
`:`, `/` et `?` ouvrent la ligne de commande, avec :

- l’historique : `Up` / `Down` ;
- la complétion des commandes et des options de `:set` : `Tab` /
  `Shift+Tab` (toutes les correspondances sont listées, comme le wildmenu
  de Vim) ;
- `Ctrl+R x` insère le registre `x`.

## Ce qui marche

| Domaine | Pris en charge |
|---|---|
| Opérateurs | `d` `c` `y` `>` `<` `=` `g~` `gu` `gU` `g?` `gq` `gw`, doublés pour les lignes (`dd`, `gUU`) |
| Mouvements | Mots, fins de ligne, `f` `t` `F` `T` `;` `,`, `%`, paragraphes, phrases, `H` `M` `L`, `[[` `]]`… |
| Objets texte | `iw` `aw`, `is` `as`, `ip` `ap`, parenthèses et crochets, guillemets, balises (`it` `at`) |
| Registres | `"a`–`"z` (`"A` ajoute), `"0`, `"1`–`"9`, `"-`, `"_`, `"+` / `"*` (presse-papiers du système, aussi celui par défaut) |
| Visuel | `v` `V` `Ctrl+V`, `gv`, `o` ; `I` / `A` / `c` sur chaque ligne d’un bloc |
| Annulation | `u` / `Ctrl+R`, une commande entière par pas |
| Marques et sauts | `m{a-z}`, `` `a `` / `'a`, `Ctrl+O` / `Tab`, `:marks`, `:jumps` |
| Macros | `qa` … `q`, `@a`, `@@`, `@:` |
| Recherche | `/` `?` avec les expressions régulières de Vim, incrémentale, toutes les occurrences surlignées avec un compteur `[n/total]` ; `*` `#`, `:noh` |
| Ex | `:s`, `:g` / `:v`, `:d`, `:y`, `:m`, `:t`, `:normal`, `:sort`, `:retab`, `:center`… sur des plages (`%`, `'<,'>`, `/motif/`…) |
| Fichiers et onglets | `:w`, `:wq`, `:x`, `ZZ`, `:q!`, `:e`, `:saveas`, `gt` / `gT`, `:tabn` / `:tabp` |

## Options

`:set` connaît `ignorecase`, `smartcase`, `hlsearch`, `incsearch`,
`wrapscan`, `expandtab`, `autoindent`, `shiftwidth`, `tabstop`,
`softtabstop`, `textwidth`, `clipboard`, `number` et `wrap`, avec les
formes `no` / `inv` / `!` / `?` / `+=`.

Les options ne sont pas lues dans un `.vimrc` : réglez-les dans l’onglet
avec `:set`.

## Navigation dans le code

`gd` ou `Ctrl+]` va à la définition, `gD` à la déclaration, `K` affiche une
fiche d’aide, `gr` liste les références, `:Symbols` les symboles du
fichier, `:TagbarToggle` (ou `:Tagbar`) les affiche à côté du texte,
`:tag nom` trouve un nom. `Ctrl+O` / `Tab` reviennent en arrière, d’abord
dans le fichier puis d’un fichier à l’autre. Dans les fichiers qui ne sont
pas du code, `gd` garde son sens Vim (première occurrence). Voir
[la navigation dans le code](code-navigation.md).

## Touches de fenêtre

`Ctrl+W w` / `Ctrl+W W` déplacent le focus vers le panneau suivant /
précédent, et `Ctrl+W c` ferme l’onglet. Les onglets Vim gardent `Ctrl+P`
pour Vim : la recherche de fichiers n’est donc pas accessible depuis eux
avec cette touche ; utilisez-la depuis l’explorateur ou l’aperçu.

Chaque touche est listée dans la
[référence du clavier](../keyboard.md#editor-tabs) (section du mode Vim, en
anglais).
