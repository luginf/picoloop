# pico2cct — convertisseur picoloop ⇄ ChooChooTracker, version autonome

Même conversion que `../cctconv` (voir son README pour la correspondance des
modèles et ce qui est perdu), mais **un seul fichier sans aucune dépendance** :
ni le code de ChooChooTracker, ni celui de picoloop.

```
make                 # ou : g++ -std=c++17 -O2 -o pico2cct pico2cct.cpp
make install         # copie dans /usr/local/bin (PREFIX=... pour changer)

pico2cct to-cct      <dossier bank picoloop> <morceau.cct> [--length N] [--title TEXTE]
pico2cct to-picoloop <morceau.cct> <dossier bank picoloop> [--force]
```

## Différences avec `../cctconv`

`pico2cct` lit et écrit lui-même les deux formats texte :

- **picoloop** : la liste des 64 paramètres par pas, leur ordre et leurs
  valeurs par défaut sont recopiés de `PatternReader` / `PatternElement`
  (picoloop 0.80). Les fichiers écrits sont identiques octet pour octet à ceux
  de picoloop.
- **ChooChooTracker** : il écrit un module 6.0 (7.0 s'il y a un instrument
  SID), la plus ancienne version qui suffit, pour que les versions publiées du
  tracker l'ouvrent (elles lisent jusqu'à la 9.0 ; la version actuelle lit
  jusqu'à la 12.0 et lit ces fichiers de la même façon). L'en-tête est minimal (les
  réglages de mixage absents prennent les valeurs par défaut du tracker), la
  table de notes 12TET par défaut, et pour chaque type d'instrument le bloc de
  réglages d'un instrument neuf, recopié d'un fichier sauvé par le tracker.
  En lecture, il ne prend que ce qui sert à la conversion : song, chaînes,
  grooves, phrases, nom/type des instruments et filtre aChChid.

Contrepartie : si un des deux formats change, il faudra mettre à jour ce
fichier à la main, alors que `../cctconv` suit automatiquement le code des
deux projets.

Les fichiers de `../cctconv` suivent le code actuel du tracker (module 12.0,
en-tête complet) : ils ne s'ouvrent pas dans les versions publiées jusqu'à la
release `appimage-test-2` (« Incorrect module version »). Avec ces versions,
utiliser `pico2cct`. La release `midi-2026-09-30` (module 5.0, ni OPL2 ni SID)
n'est pas gérée.

## Batteries PicoDrum

Contrairement à `../cctconv`, `pico2cct` règle le DrumSynth d'après chaque son
PicoDrum. Dans picoloop, les réglages PicoDrum peuvent changer à chaque pas :
chaque son différent devient donc son propre instrument.

- **Moteur** choisi selon la part de bruit dans le son : ≥ 75 % → charley
  (filtre passe-haut/bande ou coupure > 6 kHz ; cymbale si le son dure plus de
  0,35 s) ou bruit filtré ; 20 à 75 % → caisse claire ; sinon grosse caisse
  (pitch bend et note ≤ C-2) ou tom. La part de bruit vient du mélange des deux
  oscillateurs ; quand le LFO1 est actif (vitesse et profondeur non nulles),
  PicoDrum ne joue que l'oscillateur 2, modulé en hauteur par l'oscillateur 1,
  et c'est lui qui compte.
- **Durée** : PicoDrum joue un son de (attaque + 10 + release) × 256
  échantillons quelle que soit la longueur de la note ; le decay DrumSynth est
  calculé pour durer autant.
- **Filtre** : passe-bas AMSynth à (coupure/128)² × 8192 Hz, ou biquad à
  (coupure/256)² × 44100 Hz (les types picoloop 1 et 2 du biquad sont en fait
  passe-haut et passe-bande) ; résonance reprise.
- Le balayage de hauteur, la FM et la saturation du DrumSynth, absents de
  PicoDrum, sont coupés (sauf un peu de FM pour imiter la modulation du LFO).

Le son reste celui du moteur DrumSynth : proche en caractère, pas identique.
MDADrum et ER-1 gardent un DrumSynth par défaut.

## Vérification

Sur les 7 démos livrées avec ChooChooTracker, comparé à `../cctconv` :

- `.cct` → bank : banks identiques octet pour octet ;
- bank → `.cct` : fichiers identiques à partir de la table de notes (seul
  l'en-tête, plus court, diffère) et relus sans erreur par `projectLoad()` du
  tracker, qui redonne la même bank ;
- aller-retour complet avec `pico2cct` seul : banks identiques.
- ouverture avec le chargeur de la release `appimage-test-2` (module 9.0),
  recompilé depuis son tag : les 7 fichiers et un fichier avec SID s'ouvrent ;
  sauvés par cette version puis reconvertis, ils redonnent les mêmes banks.
- batteries : sur une bank réelle (65 coups PicoDrum, 10 sons différents),
  chaque son donne un instrument DrumSynth réglé, le fichier s'ouvre dans la
  release 9.0. Non vérifié à l'écoute.
