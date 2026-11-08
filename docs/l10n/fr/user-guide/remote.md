# Machines distantes

La vue distante parcourt les fichiers de machines SSH comme s’ils étaient
locaux : LookinFiles les monte avec SSHFS, et l’explorateur, l’aperçu,
l’éditeur et la recherche marchent tous dessus. Ouvrez-la avec
`Ctrl+Shift+R` (à nouveau pour revenir aux fichiers) ou le bouton serveur
en haut de l’explorateur.

## Prérequis

- `sshfs` installé (`sudo apt install sshfs` ; Fedora `fuse-sshfs`). Sans
  lui, la connexion explique comment l’installer.
- Un accès SSH qui marche sans taper de mot de passe (une clé SSH, avec
  `ssh-agent` si la clé a une phrase de passe).

## La liste des machines

La vue liste :

- chaque `Host` de votre `~/.ssh/config` (`F5` relit le fichier) ;
- les machines ajoutées à la main.

Pour en ajouter une, appuyez sur `a` et remplissez **Nom affiché**,
**Hôte / adresse IP** (obligatoire), **Utilisateur**, **Port** (22 par
défaut) et **Dossier distant** (`~` par défaut). **Enregistrer et
connecter** se connecte tout de suite.

## Utiliser une machine

| Touche | Action |
|---|---|
| `Enter` ou `c` | Se connecter et parcourir la machine dans la vue des fichiers |
| `c` sur une machine connectée | Se déconnecter |
| `t` | Ouvrir un terminal SSH vers la machine |
| `F2`, `r` ou `e` | Modifier la machine |
| `d` ou `Delete` | La supprimer de la liste |
| `F5` | Relire `~/.ssh/config` |

Pendant que vous parcourez une machine connectée, le fil d’Ariane commence
par une icône de serveur et le nom de la machine, et le bouton **Terminal**
de la barre du haut ouvre un terminal SSH vers elle au lieu d’un shell
local.

Les machines sont montées sous `~/.cache/lookinfiles/remotes/`. Les
machines ajoutées à la main sont enregistrées dans
`~/.config/lookinfiles/remotes.xml`.

## Vitesse

Chaque lecture de fichier passe par le réseau : les grands dossiers et les
aperçus de gros fichiers sont plus lents que sur un disque local. Les
badges git et la recherche dans les fichiers marchent, mais lisent le dépôt
et les fichiers à travers le montage.
