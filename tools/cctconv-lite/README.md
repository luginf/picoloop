# pico2cct — convertisseur picoloop ⇄ ChooChooTracker, version autonome

Même conversion que `../cctconv` (voir son README pour la correspondance des
modèles et ce qui est perdu), mais **un seul fichier sans aucune dépendance** :
ni le code de ChooChooTracker, ni celui de picoloop.

```
g++ -std=c++17 -O2 -o pico2cct pico2cct.cpp

pico2cct to-cct      <dossier bank picoloop> <morceau.cct> [--length N] [--title TEXTE]
pico2cct to-picoloop <morceau.cct> <dossier bank picoloop> [--force]
```

## Différences avec `../cctconv`

`pico2cct` lit et écrit lui-même les deux formats texte :

- **picoloop** : la liste des 64 paramètres par pas, leur ordre et leurs
  valeurs par défaut sont recopiés de `PatternReader` / `PatternElement`
  (picoloop 0.80). Les fichiers écrits sont identiques octet pour octet à ceux
  de picoloop.
- **ChooChooTracker** : il écrit un module 12.0 avec un en-tête minimal (les
  réglages de mixage absents prennent les valeurs par défaut du tracker), la
  table de notes 12TET par défaut, et pour chaque type d'instrument le bloc de
  réglages d'un instrument neuf, recopié d'un fichier sauvé par le tracker.
  En lecture, il ne prend que ce qui sert à la conversion : song, chaînes,
  grooves, phrases, nom/type des instruments et filtre aChChid.

Contrepartie : si un des deux formats change, il faudra mettre à jour ce
fichier à la main, alors que `../cctconv` suit automatiquement le code des
deux projets.

## Vérification

Sur les 7 démos livrées avec ChooChooTracker, comparé à `../cctconv` :

- `.cct` → bank : banks identiques octet pour octet ;
- bank → `.cct` : fichiers identiques à partir de la table de notes (seul
  l'en-tête, plus court, diffère) et relus sans erreur par `projectLoad()` du
  tracker, qui redonne la même bank ;
- aller-retour complet avec `pico2cct` seul : banks identiques.
