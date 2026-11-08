# Thèmes et polices

## Choisir un thème : `Alt+T`

`Alt+T` ouvre le dialogue des **Préférences** sur sa section **Apparence**
(aussi `Ctrl+,` ou le bouton engrenage de la barre du haut). Sélectionner
un thème l’applique aussitôt à toute la fenêtre : panneaux, éditeurs,
aperçus et terminaux. **Appliquer** le garde, **Annuler** revient au
précédent.

Trois thèmes sont intégrés :

- **Dark Modern** (celui par défaut) ;
- **Light Modern** ;
- **High Contrast Dark**.

Sept autres sont intégrés depuis le dossier `themes/` des sources : un par
décennie des années 1970 aux années 2020, et **Dark LightRed**. Copiez-en
un dans `~/.config/lookinfiles/themes/` pour partir de lui et faire le
vôtre.

## Polices

La section Apparence règle aussi deux polices, utilisées par-dessus
n’importe quel thème :

- la **police de l’interface** : menus, panneaux et documents rendus ;
- la **police du code** : contenu des fichiers, code et terminaux.

Chacune est une liste de noms de police séparés par des virgules (la
première installée est utilisée) et une taille.

| Touche | Action |
|---|---|
| `Ctrl+=` | Polices plus grandes (interface et code ensemble) |
| `Ctrl+-` | Polices plus petites |
| `Ctrl+0` | Retour aux tailles du thème |

## Créer votre propre thème

1. Appuyez sur `Alt+T` et choisissez le thème le plus proche de ce que vous
   voulez.
2. Cliquez sur **Enregistrer sous…** et donnez-lui un nom. LookinFiles
   écrit un fichier de thème complet, avec un commentaire qui décrit chaque
   couleur.
3. Cliquez sur **Modifier** : le fichier s’ouvre dans un onglet d’édition.
4. Changez les couleurs et enregistrez : le thème est aussitôt appliqué à
   nouveau, ou un message dit ce qui ne va pas.

### Un thème à partir d’une image

**Depuis une image…** construit un thème à partir des couleurs d’une image
(un fond d’écran, une photo, une capture d’écran) :

1. Sélectionnez l’image dans l’explorateur, puis appuyez sur `Alt+T`.
2. Cliquez sur **Depuis une image…** : le chemin de l’image sélectionnée est
   rempli (tapez-en un autre si vous voulez), et le nom du fichier est
   proposé comme nom du thème.
3. Cliquez sur **Créer** : le thème est enregistré avec vos thèmes et
   appliqué.

Une image surtout sombre donne un thème sombre, une image surtout claire un
thème clair. Les fonds prennent la couleur principale de l’image,
légèrement teintée ; l’accent est sa couleur la plus vive ; les couleurs du
code utilisent ses autres teintes ; les erreurs restent rouges et les
ajouts verts, accordés à l’image. Le texte garde toujours un contraste
lisible. Utilisez ensuite **Modifier** pour ajuster n’importe quelle
couleur.

Un thème peut hériter d’un autre et ne définir que ce qui diffère :

```ini
[Theme]
name=Solar Light
dark=false
inherits=Light Modern

[Colors]
bg_view=#fdf6e3
accent=#268bd2
```

Le format des thèmes et la liste des noms de couleurs sont dans
[Configuration](../configuration.md#themes) (en anglais).

Le thème choisi et vos polices sont enregistrés dans
`~/.config/lookinfiles/theme.ini`.
