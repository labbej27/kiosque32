# Kiosque 32 et Kiosque 16

Terminal Minitel pour Windows, en **telnet** et en **WebSocket** (`ws://`),
en deux versions tirées du même source :

| Programme | Taille | Windows |
|---|---|---|
| `KIOSQUE32.EXE` (32 bits) | 33 Ko | 95, 98, Me, NT 4, 2000, XP et suivants, 32 et 64 bits |
| `KIOSQ16.EXE` (16 bits) | 77 Ko | 3.1 et 3.11 (avec une pile TCP/IP : Trumpet Winsock, Microsoft TCP/IP-32…), 95, 98, Me, NT 4, 2000, XP 32 bits |

Aucune installation ni DLL à ajouter (sous Windows 3.1, il faut seulement
une pile TCP/IP qui fournit `WINSOCK.DLL`). Les Windows 64 bits ne lancent
pas les programmes 16 bits : utiliser `KIOSQUE32.EXE`.

C'est le petit frère de [Kiosque 64](https://github.com/labbej27/kiosque64) (C64), dont il reprend le
décodeur Vidéotex et les couches telnet et WebSocket.

## Utilisation

Au démarrage, la page d'accueil propose les services :

| Touche | Service | Connexion |
|---|---|---|
| 1 | MiniPAVI | telnet `go.minipavi.fr:516` |
| 2 | LABBEJ27 | telnet `minitel.labbej.fr:8180` |
| 3 | 3615 co.de | `ws://3615co.de/ws` |
| 4 | 3611.re | `ws://3611.re/ws` |
| 5 | HACKER | `ws://mntl.joher.com:2018/` |
| 6 | Autre adresse | `hôte:port` (telnet), `ws://hôte:port/chemin` |

On peut aussi passer l'adresse en ligne de commande :
`KIOSQUE32.EXE ws://3611.re/ws`.

### Touches

| PC | Minitel |
|---|---|
| Entrée | Envoi |
| Retour arrière | Correction |
| Échap, F6 | Annulation |
| F1, flèche gauche | Sommaire |
| F2 | Guide |
| F3, flèche haut | Retour |
| F4, flèche droite | Répétition |
| F5, flèche bas | Suite |
| F7 | Connexion/Fin (Maj + Cnx/Fin) |
| F8 | Déconnexion |

La barre de boutons sous l'écran donne les mêmes touches à la souris.
Comme sur un Minitel, les lettres sortent en majuscules tant que le service
n'a pas demandé les minuscules ; Maj inverse la casse. Les lettres
accentuées (é, è, à, ç, ê, ë, î, ô, ù…) sont envoyées en Vidéotex.

### Écho local

Certains serveurs renvoient eux-mêmes ce qu'on tape (3611.re, 3615co.de),
d'autres comptent sur l'écho du Minitel (HACKER). Les services de l'accueil
sont déjà réglés ; pour une autre adresse, *Connexion > Écho local* le
change, et les serveurs qui envoient la commande PRO3 le règlent eux-mêmes.

### Réglages

*Affichage* : couleur ou niveaux de gris (comme un Minitel 1), taille ×1,
×2 ou ×3, barre des touches. Les réglages et la dernière adresse sont gardés
dans `kiosque32.ini`, à côté du programme.

## Limites (version 0.1)

- Pas de `wss://` (WebSocket chiffré) : Windows 2000 ne connaît pas TLS 1.2.
- Les jeux de caractères redéfinis (DRCS) sont ignorés, comme dans Kiosque 64.
- Interface en français seulement.

## Compilation (sur Mac ou Linux)

Il faut MinGW-w64 (`brew install mingw-w64`) et Python 3 ; pour la version
16 bits, Open Watcom v2 (instantané `ow-snapshot.tar.xz` de
github.com/open-watcom/open-watcom-v2, binaires Mac dans `armo64/`) décompressé
dans `~/Library/Caches/kiosque16/ow` (ou `make win16 OW=...`).

    make            # -> ~/Library/Caches/kiosque32/build/kiosque32.exe
    make win16      # -> ~/Library/Caches/kiosque16/build/kiosq16.exe
    make imports    # fonctions Windows utilisées (toutes présentes sous Windows 95)
    make dist       # copie dans ~/Documents/dev/livrables/kiosque32
    make test       # banc d'essai sur Mac (connexion réelle, écran en PPM)

Le source Windows est `src/kiosque.c` ; `-DKIOSQUE16` choisit la version
16 bits (WinMain, rappels `__export`, image d'écran en 16 couleurs rangée de
bas en haut avec `-DFB_4BPP`, pour tenir dans un segment de 64 Ko).

La version 32 bits n'est liée à aucune bibliothèque C : `src/mini.c` fournit les
quelques fonctions de chaîne, le point d'entrée est `entry()` dans
`src/win32.c`. L'en-tête PE annonce la version 4.0 du système, sans quoi
Windows 9x/NT 4/2000 refusent de le lancer. Les sources sont en UTF-8 et sont
converties en Windows-1252 à la compilation.

Banc d'essai :

    ~/Library/Caches/kiosque32/test/headless go.minipavi.fr 516 telnet "" ecran.ppm 6 '\x13D'
    python3 tools/ppm2png.py ecran.ppm ecran.png

## Licence

MIT, voir `LICENSE` et `NOTICE`. Police : unscii de viznut (domaine public).
« Minitel » est une marque d'Orange ; Kiosque 32 n'est pas lié à Orange.
