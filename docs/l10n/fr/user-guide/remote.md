# Machines distantes

La vue Machines liste cet ordinateur et vos machines SSH, et parcourt les
fichiers des machines SSH : LookinFiles les lit
et les écrit en SFTP par une connexion SSH par machine, si bien que
l’explorateur, l’aperçu et l’éditeur marchent dessus, sans rien monter.
Ouvrez-la avec
`Ctrl+Shift+R` (à nouveau pour revenir aux fichiers) ou le bouton serveur
en haut de l’explorateur.

## Prérequis

- Sous Linux, le client OpenSSH (`ssh`). Sous Windows, les versions compilées
  avec `libssh` se connectent directement sans avoir besoin de `ssh.exe`
  (les versions sans `libssh` se rabattent sur `ssh.exe`).
- Sur la machine distante, un serveur SSH avec le sous-système SFTP (celui
  des serveurs OpenSSH par défaut).
- Un mot de passe, la phrase de passe d’une clé ou la confirmation d’une clé
  d’hôte inconnue est demandé dans un dialogue de LookinFiles ; une clé SSH
  avec `ssh-agent` évite la question.
  L’[onglet Diagnostics](lookinfiles:diagnostics#ssh) indique si `ssh` est
  trouvé, si [libssh](lookinfiles:diagnostics#libssh) est intégré, et quel
  transport chaque machine utilise.

## La liste des machines

La vue liste :

- cet ordinateur, toujours en premier (son nom, puis **Cet ordinateur** et
  votre nom d’utilisateur, en vert tant que l’espace de travail courant le
  parcourt) : `Enter` ou `c` revient à ses fichiers, `t` ouvre un terminal
  dessus. En dessous, **Disques** liste ses disques : le système de
  fichiers, les partitions, les clés USB, les disques optiques et les
  dossiers réseau qui ont un dossier local (les lettres de lecteur sous
  Windows), les disques pas encore montés marqués *non monté*. La liste suit
  les disques branchés ou retirés. Sur un disque, `Enter` le parcourt (en le
  montant d’abord si besoin), `c` le monte ou le démonte (éjecte un disque
  amovible, pour le retirer sans risque), `t` ouvre un terminal dedans ;
- chaque `Host` de votre `~/.ssh/config` (`F5` relit le fichier) ;
- les machines ajoutées à la main.

Pour en ajouter une, appuyez sur `a` et remplissez **Nom affiché**,
**Hôte / adresse IP** (obligatoire), **Utilisateur**, **Port** (22 par
défaut) et **Dossier distant** (`~` par défaut). **Enregistrer et
connecter** se connecte tout de suite.

## Utiliser une machine

| Touche | Action |
|---|---|
| `Enter` | Se connecter et parcourir la machine dans la vue des fichiers |
| `c` | Se connecter sans quitter la vue Machines, ou déconnecter une machine connectée |
| `t` | Ouvrir un terminal SSH vers la machine, sans la connecter |
| `F2`, `r` ou `e` | Modifier la machine |
| `d` ou `Delete` | La supprimer de la liste |
| `F5` | Relire `~/.ssh/config` |

## Conteneurs (Docker, Podman)

Sous cet ordinateur et sous chaque machine connectée, une ligne **Docker**
et une ligne **Podman** listent les conteneurs quand le programme y est
installé (`docker ps -a` / `podman ps -a`, lu en arrière-plan quand la
machine est dépliée) : ceux qui tournent d’abord, avec leur image. Une ligne
le signale quand Docker ne répond pas (démon arrêté, permission refusée).

| Touche | Action |
|---|---|
| `Enter` ou `t` | Ouvrir un terminal dans le conteneur (`docker exec -it`, bash sinon sh ; par SSH sur une machine distante) |
| `c` | Démarrer le conteneur, ou l’arrêter s’il tourne |
| Clic droit | Démarrer, arrêter, redémarrer, ouvrir un terminal |
| `F5` | Relire la liste des conteneurs |

Les conteneurs ne se suppriment pas d’ici, et leurs fichiers ne se
parcourent pas encore.

## Distributions WSL (Windows)

Sous Windows, une ligne **WSL** sous cet ordinateur liste les
distributions WSL installées (sans celles de Docker Desktop), avec leur
version, si elles tournent, et celle par défaut. `Enter` ou `c` parcourt
les fichiers d’une distribution (`\\wsl.localhost\<nom>\`, ce qui la
démarre), `t` ouvre un terminal dedans (`wsl.exe -d <nom>`), et le menu du
clic droit propose **Arrêter** pour une distribution qui tourne
(`wsl --terminate`). `F5` relit la liste. Le fil d’Ariane commence par
**WSL: <nom>** pendant que vous en parcourez une.

## Les disques d’une machine

Dépliez une machine connectée pour voir ses **Disques** : le système de
fichiers `/`, les partitions, les disques USB et les partages réseau
montés dessus (listés avec `findmnt`, ou `df` sous macOS et BSD, en
arrière-plan), avec leur dossier et leur espace libre. Une machine non
connectée affiche *Non connectée — Entrée pour se connecter*. `Enter` ou
`c` sur un disque le parcourt, `t` ouvre un terminal SSH dedans ; `F5`
relit la liste des disques.

Un disque hors du **Dossier distant** de la machine (`/mnt/data` quand il
vaut `~`) s’ouvre par une deuxième session SFTP de la même connexion (sans
nouvelle connexion) : ses fichiers s’écrivent `machine:/mnt/data/...`, et
tout y marche comme dans le dossier distant. Ces sessions de disque se
ferment quand la machine se déconnecte.

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
Les images d’un aperçu Markdown, HTML ou Org (les captures d’un README)
sont copiées de la même façon, jusqu’à 16 Mo chacune ; celles des cellules
Markdown d’un notebook ne s’affichent pas encore.

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

La connexion ouvre une liaison partagée par machine, gardée jusqu’à la
déconnexion ou la fermeture : les fichiers (SFTP), les commandes distantes
(Git, recherche, Deep Search, Occupation disque, Moniteur système), les
noyaux Jupyter et les serveurs de langage passent tous par elle, si bien
qu’un mot de passe n’est demandé qu’une fois. Sous Linux, il s’agit d’une
connexion maître OpenSSH (`ControlMaster`), et les terminaux SSH interactifs
ouvrent leurs propres connexions afin qu’un terminal bloqué ne bloque pas le
reste (un terminal qui a besoin d’un mot de passe le demande dans le
terminal). Sous Windows, les versions compilées avec `libssh` utilisent une
connexion libssh intégrée pour tout — terminaux SSH interactifs compris,
avec contrôle de flux par canal et sans `ssh.exe` (les versions sans
`libssh` se rabattent sur `ssh.exe`, où chaque session se connecte à part ;
une clé SSH avec `ssh-agent` évite les demandes répétées).
L’[onglet Diagnostics](lookinfiles:diagnostics) indique quel transport une
machine utilise ; quand `libssh` est intégré, les Préférences (`Ctrl+,`),
page **Machines distantes**, choisissent entre `libssh` et le programme
`ssh` (au prochain lancement ; la variable `LOOKINFILES_SSH_LIBSSH=1` ou `0`
l’emporte).

Dans LookinFiles, les fichiers d’une machine sont vus sous un dossier privé
vide de `$XDG_RUNTIME_DIR/lookinfiles/remotes/` (en général
`/run/user/1000/…`), jamais affiché : rien n’y est monté. Les montages que
les versions précédentes faisaient avec sshfs sont démontés au démarrage.
Les machines ajoutées à la main sont enregistrées dans
`~/.config/lookinfiles/remotes.xml`.

## Pas encore disponible sur une machine distante

L’ouverture d’un fichier avec une autre application n’est pas
disponible : « Ouvrir avec l’application par défaut » le dit dans la barre
d’état. Les documents LaTeX sont compilés sur la machine, comme les autres
tâches de compilation (plus bas).

## Tâches de compilation et de test

`F7` / `Shift+F7` dans un projet de la machine y lancent sa tâche, par la
connexion de la machine, dans le dossier de la tâche sur la machine : les
fichiers de compilation sont lus par SFTP, `make -j` utilise les
processeurs de la machine, et les outils doivent y être installés (un
outil absent finit par son « command not found » dans l’onglet Build). La
sortie arrive au fur et à mesure ; les fichiers que nomment les messages
(les dossiers de la machine) s’ouvrent là où la machine est vue ici, et un
fichier hors de ce dossier (un en-tête du système) n’est nommé que dans le
texte du message. **Arrêter** (`Ctrl+F7`) demande à la machine de terminer
la tâche et les programmes qu’elle a lancés (SIGTERM, puis SIGKILL). Une
tâche encore en cours quand l’application se ferme peut continuer sur la
machine.

## Serveurs de langage

Les serveurs de langage (complétion, diagnostics, aller à la définition,
renommage) s’exécutent sur la machine, dans le projet : le premier serveur
de la liste du langage (ou celui choisi dans `lsp.ini`) installé sur la
machine est lancé par ssh dans un shell de connexion, pour que les
programmes de `~/.local/bin` et autres soient trouvés. Les chemins de ce
qui lui est dit et de ce qu’il répond sont écrits comme la machine les
connaît. Rien n’est installé sur la machine : un langage sans serveur
là-bas n’a simplement pas de diagnostics.

## Deep Search

Deep Search (`Deep Search…` sur un dossier) s’exécute aussi sur la
machine : `find` y liste les entrées (il faut le `find` GNU, comme sous
Linux), les critères sont testés ici, et pour une recherche dans le contenu
`grep` lit les fichiers qui passent les autres critères, quelques centaines
à la fois, chaque ligne trouvée étant vérifiée ici. Le champ du dossier
affiche `machine:chemin`, et l’accepte tapé ainsi.

## Occupation disque

L’occupation disque lit un dossier distant avec un seul `find` lancé sur
la machine (le `find` GNU, comme sous Linux), qui liste chaque entrée avec
sa taille, ses blocs et sa date ; l’arbre est ensuite additionné ici comme
pour un dossier local (liens physiques comptés une fois, autres systèmes de
fichiers ignorés sur demande, exclusions). Le dossier en cours de listage
s’affiche pendant que `find` tourne ; les dossiers de la répartition
arrivent à la fin, tous ensemble. Dans un dépôt, la vue d’ensemble Git
(suivis, non suivis, ignorés, historique) vient de `git ls-files` lancé sur
la machine, les tailles de ce que `find` a listé.

## Git

Dans un dépôt de la machine, l’explorateur affiche les badges Git des
fichiers modifiés et la barre d’état la branche, avec `*` pour les
modifications non validées et les commits d’avance et de retard sur la
branche amont : `git status` s’exécute sur la machine (il faut `git`) à
chaque relecture du dossier. Un clic sur la branche liste les branches de la
machine et passe à celle choisie (`git checkout` sur la machine).

L’onglet Git affiche l’historique, les statistiques, le dépôt et les remises
et étiquettes à partir d’une copie du dossier `.git` du dépôt gardée dans
`~/.cache/lookinfiles/git-mirrors` : la première fois, tout le dossier
arrive en un seul flux `tar` lancé sur la machine (un gros historique prend
un moment) ; ensuite, seuls les fichiers nouveaux ou modifiés (les
références, l’index, les nouveaux paquets) à chaque relecture du dépôt par
l’onglet. La copie est seulement lue, jamais écrite. La vue Modifications
lance `git status` et `git diff` sur la machine (les nombres de lignes avec
`git diff --numstat`), et « Comparer avec HEAD » lit la dernière version
validée du fichier dans la copie.

## Recherche dans les fichiers et Aller au fichier

La recherche dans les fichiers (`Ctrl+Shift+F`) et Aller au fichier
s’exécutent sur la machine : les fichiers y sont listés (avec
`git ls-files` dans un dépôt, sinon `find`, sans les dossiers cachés et de
compilation, comme sur cet ordinateur), et `grep` trouve les lignes qui
peuvent correspondre. Chaque ligne est ensuite vérifiée ici, donc les
options (respecter la casse, mot entier, expression régulière, lettres dans
l’ordre) donnent les mêmes résultats que pour des fichiers locaux. La
machine a besoin de `grep`, et de `git` pour les dépôts ; une expression
régulière utilise `grep -P` quand la machine l’a, sinon `grep -E`, qui
connaît moins de formes (pas de `\d`, pas d’assertion avant). Au plus
32 Mo de lignes sont lus ; l’onglet Recherche le signale s’il y en avait
plus.

## Vitesse

Chaque lecture de fichier passe par le réseau : les grands dossiers et les
aperçus de gros fichiers sont plus lents que sur un disque local.
