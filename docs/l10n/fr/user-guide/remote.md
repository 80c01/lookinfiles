# Machines distantes

La vue distante parcourt les fichiers de machines SSH comme s’ils étaient
locaux : LookinFiles les monte avec SSHFS, et l’explorateur, l’aperçu,
l’éditeur et la recherche marchent tous dessus. Ouvrez-la avec
`Ctrl+Shift+R` (à nouveau pour revenir aux fichiers) ou le bouton serveur
en haut de l’explorateur.

## Prérequis

- `sshfs` installé (`sudo apt install sshfs` ; Fedora `fuse-sshfs`). Sans
  lui, la connexion explique comment l’installer.
- Le client OpenSSH (`ssh`). Un mot de passe ou la phrase de passe d’une
  clé, quand ssh en a besoin, est demandé dans un dialogue de LookinFiles ;
  une clé SSH avec `ssh-agent` évite la question.

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

Les dossiers d’une machine connectée sont listés en SFTP, par la connexion
SSH déjà ouverte (sans nouvelle authentification), plutôt qu’à travers le
montage ; les fichiers texte sont prévisualisés et ouverts dans l’éditeur de
la même façon. Un réseau lent fait attendre le listage ou le fichier, pas
toute la fenêtre. L’enregistrement, les nouveaux fichiers et dossiers, les
renommages et les suppressions passent aussi en SFTP. Une machine distante
n’a pas de corbeille : y supprimer un fichier ou un dossier (après une
confirmation qui le dit) est définitif, et `u` ne l’annule pas.

Pendant que vous parcourez une machine connectée, le fil d’Ariane commence
par une icône de serveur et le nom de la machine, et le bouton **Terminal**
de la barre du haut ouvre un terminal SSH vers elle au lieu d’un shell
local.

Les notebooks Jupyter de la machine exécutent leur noyau sur la machine
elle-même (voir [L’éditeur](editor.md)), et l’onglet Moniteur système montre
la machine (voir [Le panneau d’aperçu](preview-panel.md#moniteur-système)),
tous deux par la liaison ouverte en se connectant.

Les chemins de la machine s’écrivent comme sur la machine, `nom:chemin` :
les dialogues **Nouveau fichier** et **Nouveau dossier** affichent
`serveur:~/projets` (et non le point de montage local), de même que le
dialogue **Enregistrer** des notebooks, l’invite `C-x C-w` d’Emacs et les
infobulles des onglets. Tapez les chemins de la même façon (`serveur:~/a`,
ou `serveur:docs` depuis le **Dossier distant**) ; un chemin hors du
dossier distant est refusé.

La connexion ouvre une liaison pour les commandes exécutées sur la machine
(noyaux Jupyter ; multiplexage OpenSSH, pas sous Windows), gardée jusqu’à la
déconnexion ou la fermeture ; les fichiers et les terminaux SSH ont leurs
propres connexions. Un mot de passe tapé en se connectant sert aussi à la
connexion des fichiers ; un terminal qui en a besoin le demande dans le
terminal.

Les machines sont montées sous `$XDG_RUNTIME_DIR/lookinfiles/remotes/`
(en général `/run/user/1000/…` ; sans `$XDG_RUNTIME_DIR`,
`/tmp/lookinfiles-<uid>/remotes/`), un dossier privé que les nettoyeurs de
cache et les sauvegardes laissent de côté. Si LookinFiles s’arrête sans se
déconnecter (un plantage), le démarrage suivant démonte ce qui était resté
monté ; rien n’est touché sur la machine. Les machines ajoutées à la main
sont enregistrées dans `~/.config/lookinfiles/remotes.xml`.

## Vitesse

Chaque lecture de fichier passe par le réseau : les grands dossiers et les
aperçus de gros fichiers sont plus lents que sur un disque local. Les
badges git et la recherche dans les fichiers marchent, mais lisent le dépôt
et les fichiers à travers le montage.
