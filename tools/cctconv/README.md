# picoloop2cct — convertisseur picoloop ⇄ ChooChooTracker

Outil en ligne de commande qui convertit une *bank* picoloop en morceau
ChooChooTracker (`.cct`), et un `.cct` en bank picoloop.

```
picoloop2cct to-cct      <dossier bank picoloop> <morceau.cct> [--length N] [--title TEXTE]
picoloop2cct to-picoloop <morceau.cct> <dossier bank picoloop> [--force]
```

Le dossier d'une bank est `<stockage picoloop>/bank/bank<N>` : il contient les
fichiers `dataP<pattern>T<piste>.pic` et `song.pic`.

## Compilation

Il faut une copie du dépôt ChooChooTracker (pour `chipnomad_lib`) :

```
cd tools/cctconv
make CCT_DIR=/chemin/vers/Choochootracker -j8
```

Pas besoin de SDL : le code picoloop est compilé avec un petit stub
(`stub/SDL2/SDL.h`). Testé sous Linux avec g++ 13.

## Conception

L'outil ne réimplémente aucun des deux formats de fichier :

- côté picoloop, il compile les vrais `PatternReader`, `Pattern`,
  `PatternElement` et `SongSequencer` de `picoloop/` et lit/écrit la bank via
  `PatternReader::setCustomRoot()` ;
- côté ChooChooTracker, il lie `chipnomad_lib` et passe par `projectSave()` /
  `projectLoad()`, les fonctions qu'utilise le tracker lui-même : un `.cct`
  écrit par l'outil est forcément accepté par le tracker.

Les deux côtés sont dans des unités de compilation séparées (`pico_side.cpp`,
`cct_side.cpp`) car `Master.h` et `chipnomad_lib` définissent des macros
communes (`TRACK_MAX`, `NOTE_OFF`, `DEFAULT_VOLUME`…). Ils ne se rencontrent
que via la structure neutre de `pico_song.h`.

## Correspondance des modèles

| picoloop | ChooChooTracker |
|---|---|
| 4 pistes | pistes 1 à 4 (sur 8) |
| position de song × piste → numéro de pattern | ligne de song × piste → chaîne |
| pattern (piste, numéro), 1 à 128 pas | chaîne de ⌈taille/16⌉ phrases ; `HOP 00` après le dernier pas si la taille n'est pas un multiple de 16 |
| pas : Trig + Note | ligne de phrase : note (note picoloop *n* = index *n − 1*, même hauteur) |
| Velocity 0-127 | volume de la ligne 0-127 |
| BPM (16ᵉ) | tick rate = 0,4 × BPM, groove 6/6 (6 ticks par ligne) |
| diviseur 1/2/4/8 et swing 25-75 | une groove par couple (diviseur, swing), posée par `GRV` en tête de chaque chaîne s'il y en a plusieurs |
| machine (piste, type) | un instrument par couple, nommé `T<piste> <machine>` |
| pattern absent | 16 pas muets (comme picoloop) |

Machines → instruments :

| picoloop | ChooChooTracker | réglages repris |
|---|---|---|
| PicoSynth, PBSynth, Cursynth, Twytch | aChChid (onde saw/square/Braids selon OSC1) | cutoff, résonance |
| Open303 | aChChid saw (filtre 303) | cutoff, résonance |
| OPL2 | OPL2 | patch par défaut |
| PicoDrum, MDADrum, ER-1 | DrumSynth | moteur par défaut (kick) |
| SIDSynth | SID | par défaut |
| MIDI Out | MIDI Out | canal = piste |
| LGPT Sampler | Sample | sample à choisir dans le tracker |

Dans l'autre sens, le type d'instrument donne la machine (OPL*/OPLL/VRC7 →
OPL2, DrumSynth → PicoDrum, SID → SIDSynth, MIDI → MIDI Out,
Sample → LGPT Sampler, le reste → PicoSynth). Un instrument nommé
`T<n> <machine>` par l'outil retrouve exactement sa machine d'origine.

## Ce qui est perdu ou approché

picoloop → ChooChooTracker :

- **Paramètres par pas** : picoloop enregistre tous les paramètres du synthé sur
  chaque pas (comme des *parameter locks*). Seuls ceux du premier pas joué de
  chaque machine servent à régler l'instrument ; les variations par pas ne sont
  pas converties (piste possible : FX d'instrument comme `ACF`/`ARS`).
- Le son n'est pas identique : les moteurs diffèrent (PicoSynth ≠ aChChid,
  PicoDrum ≠ DrumSynth…). Seuls la structure, les notes, les vélocités, le tempo
  et le groove sont fidèles.
- **Tempo par pattern** : le tempo du premier pattern s'applique à tout le
  morceau (avertissement si d'autres BPM existent).
- **Patterns de longueurs différentes à une même position** : picoloop avance
  toute la song quand la *première* piste boucle, ChooChooTracker laisse chaque
  piste finir sa chaîne. Identique quand les longueurs sont égales,
  avertissement sinon.
- **Swing** quantifié à 1/12 de paire de pas (62 → 58) avec 6 ticks par ligne.
- Song limitée à 256 positions (picoloop : 1024), 255 chaînes, 1024 phrases.
- Les samples du LGPT Sampler ne sont pas retrouvés.

ChooChooTracker → picoloop :

- pistes 5 à 8, FX, tables, instruments (au-delà du type), modulations,
  effets maître : ignorés ;
- les `OFF` sont perdus (picoloop n'a pas de pas « note off ») ;
- une chaîne de plus de 128 lignes est coupée à 128 pas ;
- une seule machine par pattern (celle du premier instrument joué) ;
- le tempo vient de la groove 0, chaque pattern prend le diviseur et le swing de
  la groove active au début de sa chaîne ;
- le nombre de chaînes différentes par piste est limité à 128.

## Vérification

Faite sur les 7 démos livrées avec ChooChooTracker
(`tracker/packaging/common/projects/*.cct`) : `.cct` → bank → `.cct` → bank
redonne des banks identiques octet pour octet, et chaque `.cct` produit est relu
par `projectLoad()`. Un jeu de test picoloop construit à la main (pattern de 12
pas, diviseur 2 + swing, machine SID) donne bien `HOP 00`, `GRV 01`, une groove
`0E 0A` et un instrument `T3 SIDSynth`.

Non vérifié : écoute dans picoloop et dans ChooChooTracker, et banks picoloop
réelles d'utilisateurs (aucune n'est dans le dépôt).

## Suites possibles

- Import intégré au tracker : un `tracker/src/import/import_picoloop.cpp` dans
  ChooChooTracker (même modèle que `import_m8s.cpp`) réutiliserait
  `cct_side.cpp` ; le côté picoloop n'a besoin que de la lecture texte des
  `dataP*.pic`.
- Export depuis picoloop (menu L/S) vers `.cct`.
- Conversion des *parameter locks* en FX de phrase.
