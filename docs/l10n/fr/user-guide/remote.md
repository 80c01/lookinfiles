# Machines distantes

La vue distante parcourt les fichiers de machines SSH : LookinFiles les lit
et les écrit en SFTP par une connexion SSH par machine, si bien que
l’explorateur, l’aperçu et l’éditeur marchent dessus, sans rien monter.
Ouvrez-la avec
`Ctrl+Shift+R` (à nouveau pour revenir aux fichiers) ou le bouton serveur
en haut de l’explorateur.

## Prérequis

- Le client OpenSSH (`ssh`), et le sous-système SFTP sur la machine (celui
  des serveurs OpenSSH par défaut). Un mot de passe ou la phrase de passe d’une
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
SSH déjà ouverte (sans nouvelle authentification) ; les fichiers texte sont
prévisualisés et ouverts dans l’éditeur de la même façon. Un réseau lent fait attendre le listage ou le fichier, pas
toute la fenêtre. L’enregistrement, les nouveaux fichiers et dossiers, les
renommages et les suppressions passent aussi en SFTP. Une machine distante
n’a pas de corbeille : y supprimer un fichier ou un dossier (après une
confirmation qui le dit) est définitif, et `u` ne l’annule pas.

Les images, PDF, archives, documents, vidéos et sons sont copiés en SFTP
dans le dossier de cache (`~/.cache/lookinfiles/remote-files`) pour être
prévisualisés, une fois par version du fichier (64 Mo au plus chacun,
512 Mo pour une vidéo ou un son, 1 Go en tout, les plus anciens retirés
d’abord). Une grande vidéo prend le temps de sa copie avant de démarrer.

Un fichier d’une machine distante ouvert dans l’éditeur est vérifié toutes
les 3 secondes, en SFTP, pour les modifications faites sur la machine :
l’éditeur réagit alors comme pour un fichier local (rechargement,
avertissement ou bandeau, selon son mode). De la même façon, les dossiers
affichés dans l’explorateur et le fichier prévisualisé sont vérifiés toutes
les 3 secondes : les fichiers créés, supprimés ou renommés sur la machine
apparaissent dans l’explorateur dans ce délai.

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
`serveur:~/projets`, de même que le
dialogue **Enregistrer** des notebooks, l’invite `C-x C-w` d’Emacs et les
infobulles des onglets. Tapez les chemins de la même façon (`serveur:~/a`,
ou `serveur:docs` depuis le **Dossier distant**) ; un chemin hors du
dossier distant est refusé.

La connexion ouvre une liaison (multiplexage OpenSSH, pas sous Windows),
gardée jusqu’à la déconnexion ou la fermeture : les fichiers, les noyaux
Jupyter et le Moniteur système passent tous par elle, si bien qu’un mot de
passe n’est demandé qu’une fois. Les terminaux SSH ont leurs propres
connexions ; un terminal qui a besoin d’un mot de passe le demande dans le
terminal.

Dans LookinFiles, les fichiers d’une machine sont vus sous un dossier privé
vide de `$XDG_RUNTIME_DIR/lookinfiles/remotes/` (en général
`/run/user/1000/…`), jamais affiché : rien n’y est monté. Les montages que
les versions précédentes faisaient avec sshfs sont démontés au démarrage.
Les machines ajoutées à la main sont enregistrées dans
`~/.config/lookinfiles/remotes.xml`.

## Pas encore disponible sur une machine distante

Ces fonctions s’exécutent sur cet ordinateur et ont besoin de fichiers
locaux : Git (pas de badges ni d’onglet Git), la recherche dans les
fichiers, Deep Search, Aller au fichier, l’occupation disque, les serveurs
de langage (complétion, diagnostics) et la compilation LaTeX. L’ouverture
d’un fichier avec une autre application n’est pas disponible non plus. Les
fonctions de recherche et l’ouverture avec l’application par défaut le
disent dans la barre d’état ; les autres restent éteintes.

## Vitesse

Chaque lecture de fichier passe par le réseau : les grands dossiers et les
aperçus de gros fichiers sont plus lents que sur un disque local.
