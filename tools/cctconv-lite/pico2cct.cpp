// pico2cct: standalone picoloop <-> ChooChooTracker (.cct) converter.
//
// One file, no dependency: it reads and writes both text formats itself.
//   g++ -std=c++17 -O2 -o pico2cct pico2cct.cpp
//
// See README.md for the mapping. The heavier tools/cctconv does the same
// with the real picoloop and chipnomad_lib code; this one copies the
// formats as written by picoloop 0.80 and ChooChooTracker module 12.0.
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using std::string;
using std::vector;

// ===========================================================================
// picoloop bank

static const int TRACKS = 4, STEPS = 128, PATTERNS = 128, SONG_LENGTH = 1024;

// Per-step parameters in the order PatternReader writes them, with the
// PatternElement::init() defaults.
static const struct { const char* name; int value; } PARAMS[] = {
  {"LFO1Depth", 0}, {"LFO1Speed", 0}, {"Amp", 127}, {"Note", 0}, {"Trig", 0}, {"VCOMix", 64},
  {"Attack", 1}, {"Decay", 112}, {"Sustain", 8}, {"Release", 24}, {"OscOneType", 1}, {"OscTwoType", 2},
  {"Cutoff", 112}, {"Resonance", 16}, {"PhaseOsc1", 0}, {"AttackFltr", 8}, {"DecayFltr", 64},
  {"SustainFltr", 32}, {"ReleaseFltr", 32}, {"NoteADSR", 1}, {"MachineType", 0}, {"FilterAlgo", 2},
  {"FilterType", 0}, {"Osc1Amp", 32}, {"Osc2Amp", 32}, {"FxDepth", 0}, {"FxSpeed", 0}, {"FmType", 0},
  {"TrigTime", 1}, {"PitchBendDepth", 64}, {"PitchBendSpeed", 0}, {"LfoType", 0}, {"OSC1Detune", 64},
  {"LFO1Waveform", 0}, {"LFO2Waveform", 0}, {"LFO1EnvAmount", 64}, {"LFO2EnvAmount", 64},
  {"LFO2Depth", 0}, {"LFO2Speed", 0}, {"Velocity", 96}, {"OSC1Mod", 0}, {"Osc1Unison", 0},
  {"Osc2Unison", 0}, {"Osc1UnisonDetune", 0}, {"Osc2UnisonDetune", 0}, {"Filter1Saturation", 64},
  {"Filter1Feedback", 64}, {"Osc3Amp", 0}, {"Osc4Amp", 0}, {"OscThreeType", 0}, {"OSC1Scale", 0},
  {"OSC2Scale", 0}, {"OSC3Scale", 0}, {"OSC4Scale", 0}, {"OSC2Detune", 64}, {"Env1Depth", 64},
  {"Env2Depth", 64}, {"Env3Depth", 64}, {"Env4Depth", 64}, {"Keytrack", 64}, {"OSC2Mod", 0},
  {"PhaseOsc2", 0}, {"PhaseOsc3", 0}, {"PhaseOsc4", 0},
};
static const int NPARAMS = sizeof(PARAMS) / sizeof(PARAMS[0]);
enum { P_AMP = 2, P_NOTE = 3, P_TRIG = 4, P_OSC1 = 10, P_CUTOFF = 12, P_RESONANCE = 13,
       P_MACHINE = 20, P_VELOCITY = 39 };

static int paramIndex(const char* name) {
  for (int i = 0; i < NPARAMS; i++)
    if (!strcmp(PARAMS[i].name, name)) return i;
  return -1;
}

enum { M_PICOSYNTH, M_OPL2, M_PICODRUM, M_PBSYNTH, M_CURSYNTH, M_OPEN303, M_TWYTCH, M_MIDIOUT,
       M_MDADRUM, M_SIDSYNTH, M_LGPTSAMPLER, M_ER1, M_COUNT };
static const char* MACHINES[M_COUNT] = {"PicoSynth", "OPL2", "PicoDrum", "PBSynth", "Cursynth", "Open303",
                                        "Twytch", "MIDIOut", "MDADrum", "SIDSynth", "LGPTSampler", "ER1"};

struct Pattern {
  bool exists = false;
  int size = 16, bpm = 120, divider = 1, swing = 50;
  int v[NPARAMS][STEPS];
  Pattern() {
    for (int p = 0; p < NPARAMS; p++)
      for (int s = 0; s < STEPS; s++) v[p][s] = PARAMS[p].value;
  }
};

struct Bank {
  vector<vector<Pattern>> patterns;  // [pattern][track]
  vector<vector<int>> song;          // [position][track] = pattern
  int songLength = 1;
  Bank() : patterns(PATTERNS, vector<Pattern>(TRACKS)), song(SONG_LENGTH, vector<int>(TRACKS, 0)) {}
};

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static bool readPattern(const string& path, int number, int track, Pattern* pat) {
  FILE* f = fopen(path.c_str(), "r");
  if (!f) return false;
  char line[4096];
  while (fgets(line, sizeof(line), f)) {
    int p, t, n;
    char name[64];
    if (sscanf(line, "Pattern %d Track %d Param %63s%n", &p, &t, name, &n) != 3 || p != number || t != track) continue;
    pat->exists = true;
    char* rest = line + n;
    if (!strcmp(name, "PatternSize")) pat->size = clamp(atoi(rest), 1, STEPS);
    else if (!strcmp(name, "PatternBPM")) pat->bpm = atoi(rest);
    else if (!strcmp(name, "PatternBPMDivider")) pat->divider = atoi(rest);
    else if (!strcmp(name, "PatternSwing")) pat->swing = atoi(rest);
    else {
      int idx = paramIndex(name);
      if (idx < 0) continue;
      for (int s = 0; s < STEPS; s++) {
        char* end;
        long value = strtol(rest, &end, 10);
        if (end == rest) break;
        pat->v[idx][s] = (int)value;
        rest = end;
      }
    }
  }
  fclose(f);
  int d = pat->divider;
  if (d != 1 && d != 2 && d != 4 && d != 8) pat->divider = 1;
  return pat->exists;
}

static bool loadBank(const string& dir, Bank* bank, string* error) {
  int found = 0;
  for (int p = 0; p < PATTERNS; p++)
    for (int t = 0; t < TRACKS; t++) {
      char path[1200];
      snprintf(path, sizeof(path), "%s/dataP%dT%d.pic", dir.c_str(), p, t);
      if (readPattern(path, p, t, &bank->patterns[p][t])) found++;
    }
  if (!found) {
    *error = "no picoloop pattern (dataP<n>T<t>.pic) in " + dir;
    return false;
  }
  // song.pic: for each track, one byte per song position.
  FILE* f = fopen((dir + "/song.pic").c_str(), "rb");
  if (f) {
    unsigned char row[SONG_LENGTH];
    for (int t = 0; t < TRACKS; t++) {
      memset(row, 0, sizeof(row));
      if (fread(row, 1, SONG_LENGTH, f) != (size_t)SONG_LENGTH && t == 0) break;
      for (int pos = 0; pos < SONG_LENGTH; pos++) bank->song[pos][t] = row[pos] < PATTERNS ? row[pos] : 0;
    }
    fclose(f);
  }
  bank->songLength = 1;
  for (int pos = 0; pos < SONG_LENGTH; pos++)
    for (int t = 0; t < TRACKS; t++)
      if (bank->song[pos][t]) bank->songLength = pos + 1;
  return true;
}

static bool isPatternFile(const char* name) {
  int p, t;
  char tail[8];
  return sscanf(name, "dataP%dT%d.%7s", &p, &t, tail) == 3 && !strcmp(tail, "pic");
}

// Same bytes as PatternReader::writePattern().
static bool saveBank(const string& dir, const Bank& bank, bool force, string* error) {
  mkdir(dir.c_str(), 0755);
  DIR* d = opendir(dir.c_str());
  if (!d) {
    *error = "cannot create folder " + dir;
    return false;
  }
  vector<string> stale;
  while (struct dirent* e = readdir(d))
    if (isPatternFile(e->d_name) || !strcmp(e->d_name, "song.pic")) stale.push_back(dir + "/" + e->d_name);
  closedir(d);
  if (!stale.empty()) {
    if (!force) {
      *error = dir + " already holds a picoloop bank (use --force to replace it)";
      return false;
    }
    for (const string& f : stale) unlink(f.c_str());
  }

  for (int p = 0; p < PATTERNS; p++)
    for (int t = 0; t < TRACKS; t++) {
      const Pattern& pat = bank.patterns[p][t];
      if (!pat.exists) continue;
      char path[1200];
      snprintf(path, sizeof(path), "%s/dataP%dT%d.pic", dir.c_str(), p, t);
      FILE* f = fopen(path, "w");
      if (!f) {
        *error = string("cannot write ") + path;
        return false;
      }
      fprintf(f, "Pattern %d Track %d Param PatternSize %d\n", p, t, pat.size);
      fprintf(f, "Pattern %d Track %d Param PatternBPM %d\n", p, t, pat.bpm);
      fprintf(f, "Pattern %d Track %d Param PatternBPMDivider %d\n", p, t, pat.divider);
      fprintf(f, "Pattern %d Track %d Param PatternSwing %d\n", p, t, pat.swing);
      for (int i = 0; i < NPARAMS; i++) {
        fprintf(f, "Pattern %d Track %d Param %s ", p, t, PARAMS[i].name);
        for (int s = 0; s < STEPS; s++) fprintf(f, "%d ", pat.v[i][s]);
        fprintf(f, "\n");
      }
      fprintf(f, "\n\n");
      fclose(f);
    }

  FILE* f = fopen((dir + "/song.pic").c_str(), "wb");
  if (!f) {
    *error = "cannot write song.pic";
    return false;
  }
  for (int t = 0; t < TRACKS; t++)
    for (int pos = 0; pos < SONG_LENGTH; pos++) fputc(bank.song[pos][t] & 0xFF, f);
  fclose(f);
  return true;
}

// ===========================================================================
// ChooChooTracker song (only what the conversion needs)

static const int CCT_TRACKS = 8, CCT_LENGTH = 256, CCT_CHAINS = 255, CCT_PHRASES = 1024,
                 CCT_GROOVES = 32, CCT_INSTRUMENTS = 128, PITCHES = 108;
static const int EMPTY8 = 255, EMPTY16 = 32767, NOTE_OFF = 254, VOLUME_MAX = 127;
static const int CUTOFF_MAX_HZ = 20000;

enum { T_SAMPLE = 5, T_ACHCHID = 10, T_DRUMSYNTH = 11, T_MIDI = 16, T_OPLL = 17, T_VRC7 = 18, T_OPL2 = 19,
       T_OPL3 = 20, T_SID = 27, T_AYSAMPLE = 3 };

struct Row {
  int note = EMPTY8, instrument = EMPTY8, volume = EMPTY16;
  string fx[3] = {"---", "---", "---"};
  int fxValue[3] = {0, 0, 0};
  bool operator==(const Row& o) const {
    for (int i = 0; i < 3; i++)
      if (fx[i] != o.fx[i] || fxValue[i] != o.fxValue[i]) return false;
    return note == o.note && instrument == o.instrument && volume == o.volume;
  }
};
struct Phrase { Row rows[16]; };
struct ChainRow { int phrase = EMPTY16, transpose = 0; };
struct Chain { ChainRow rows[16]; };
struct Groove { int speed[16]; Groove() { for (int& s : speed) s = EMPTY8; } };
struct Instrument {
  bool used = false;
  string name;
  int type = 0;
  int wave = 1, model = 0, cutoff = 1000, resonance = 0;  // aChChid
  int channel = 0;                                        // MIDI
};

struct Song {
  string title;
  float tickRate = 50;
  int tracks = CCT_TRACKS;
  int song[CCT_LENGTH][CCT_TRACKS];
  vector<Chain> chains = vector<Chain>(CCT_CHAINS);
  vector<Phrase> phrases = vector<Phrase>(CCT_PHRASES);
  vector<Groove> grooves = vector<Groove>(CCT_GROOVES);
  vector<Instrument> instruments = vector<Instrument>(CCT_INSTRUMENTS);
  vector<string> noteNames;
  int chainCount = 0, phraseCount = 0;
  Song() {
    for (auto& r : song)
      for (int& c : r) c = EMPTY16;
  }
};

// The default 12TET linear pitch table of ChooChooTracker: C-0 .. B-8,
// values in cents from MIDI note 0.
static vector<string> defaultNoteNames() {
  static const char* names[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
  vector<string> out;
  for (int i = 0; i < PITCHES; i++) out.push_back(string(names[i % 12]) + (char)('0' + i / 12));
  return out;
}

// ---------------------------------------------------------------------------
// .cct writer

static string hex2(int v) {
  char b[8];
  snprintf(b, sizeof(b), "%02X", v & 0xFF);
  return b;
}
static string hex2OrEmpty(int v, int empty) { return v == empty ? "--" : hex2(v); }

// Chip data as written by ChooChooTracker 12.0 for a freshly initialized
// instrument of each type.
static string chipData(const Instrument& inst) {
  char b[512];
  switch (inst.type) {
    case T_ACHCHID:
      snprintf(b, sizeof(b),
               "- Wave: %d\n- Fine: 0\n- Model: %d\n- Timbre: 16384\n- Color: 16384\n- Saturation: 0\n"
               "- SID shape: 4\n- SID pulse: 2048\n- Cutoff: %d\n- Resonance: %d\n- Env mod: 25\n"
               "- Decay: 1000\n- Accent: 100\n",
               inst.wave, inst.model, inst.cutoff, inst.resonance);
      return b;
    case T_DRUMSYNTH:
      return "- Engine: 0\n- Drum decay: 72\n- Tone: 128\n- Sweep: 150\n- Noise: 24\n- FM: 32\n- Drive: 28\n"
             "- Filter enabled: 1\n- Filter character: 2\n- Filter mode: 0\n- Filter slope: 0\n"
             "- Filter cutoff: 20000\n- Filter resonance: 0\n- Filter color: 128\n";
    case T_OPL2:
      return "- OPL base: 1,0,0,0,0,0,3,3,0,0,0,0,60,0,0,0,0,0,0,0,0,0,0,0\n- OPL name: Soft FM Keys\n"
             "- OPL op0: 1,24,15,4,6,5,0,0,0,0,0,1\n- OPL op1: 1,0,15,3,5,5,0,0,0,0,0,1\n"
             "- OPL op2: 0,0,0,0,0,0,0,0,0,0,0,0\n- OPL op3: 0,0,0,0,0,0,0,0,0,0,0,0\n";
    case T_SID:
      return "- SID identity: 1,600\n- SID name: Moving Pulse 1\n"
             "- SID parameters: 0,4,1,6,10,5,512,800,8,1,0,0,2,50,0,0,0,0,0,128,3,0,0,65535,0\n";
    case T_MIDI:
      snprintf(b, sizeof(b),
               "- Channel: %d\n- Program: 255\n- Bank high: 255\n- Bank low: 255\n- CC1 number: 255\n"
               "- CC2 number: 255\n- CC3 number: 255\n- CC4 number: 255\n",
               inst.channel);
      return b;
    default:  // T_SAMPLE
      return "- Sample path: \n- Sample pitch: 0\n- Sample speed: 100\n- Sample start: 0\n- Sample end: 255\n"
             "- Sample loop: 0\n- Sample slice: 0\n- Sample stretch: 0\n- Speed algo: 0\n- Filter enabled: 1\n"
             "- Filter character: 2\n- Filter mode: 0\n- Filter slope: 0\n- Filter cutoff: 20000\n"
             "- Filter resonance: 0\n- Filter color: 128\n- Attack: 0\n- Decay: 0\n- Sustain: 255\n"
             "- Release: 0\n- Envelope shape: 128\n";
  }
}

static bool writeCct(const Song& s, const string& path, string* error) {
  FILE* f = fopen(path.c_str(), "w");
  if (!f) {
    *error = "cannot write " + path;
    return false;
  }
  fprintf(f, "# ChooChooTracker Module 12.0\n\n- Title: %s\n- Author: \n- Frame rate: %f\n- Chips count: 8\n",
          s.title.c_str(), s.tickRate);
  fprintf(f, "- Track volumes: 100,100,100,100,100,100,100,100\n- Linear pitch: 1\n- Signed track speed: 1\n"
             "- Perceptual effects: 1\n- Chip type: AY8910\n- *AY8910* Clock: 1773400\n- *AY8910* AY/YM: 0\n"
             "- *AY8910* Stereo: ABC\n- *AY8910* Stereo separation: 50\n- *AY8910* PWM range: 0\n");

  fprintf(f, "\n## Pitch table\n\n- Title: 12TET Linear\n\n```\n");
  for (int i = 0; i < PITCHES; i++) fprintf(f, "%s %d\n", s.noteNames[i].c_str(), (i + 12) * 100);
  fprintf(f, "```\n");

  fprintf(f, "\n## Song\n\n```\n");
  int length = 0;
  for (int r = 0; r < CCT_LENGTH; r++)
    for (int t = 0; t < CCT_TRACKS; t++)
      if (s.song[r][t] != EMPTY16) length = r + 1;
  for (int r = 0; r < length; r++)
    for (int t = 0; t < CCT_TRACKS; t++)
      fprintf(f, "%s%s", hex2OrEmpty(s.song[r][t], EMPTY16).c_str(), t < CCT_TRACKS - 1 ? " " : "\n");
  fprintf(f, "```\n");

  fprintf(f, "\n## Chains\n");
  for (int c = 0; c < s.chainCount; c++) {
    fprintf(f, "\n### Chain %X\n\n```\n", c);
    for (const ChainRow& row : s.chains[c].rows) {
      if (row.phrase == EMPTY16) fprintf(f, "--- %s\n", hex2(row.transpose).c_str());
      else fprintf(f, "%03X %s\n", row.phrase, hex2(row.transpose).c_str());
    }
    fprintf(f, "```\n");
  }

  fprintf(f, "\n## Grooves\n");
  for (int g = 0; g < CCT_GROOVES; g++) {
    if (s.grooves[g].speed[0] == EMPTY8) continue;
    fprintf(f, "\n### Groove %X\n\n```\n", g);
    for (int v : s.grooves[g].speed) fprintf(f, "%s\n", hex2OrEmpty(v, EMPTY8).c_str());
    fprintf(f, "```\n");
  }

  fprintf(f, "\n## Phrases\n\n");
  for (int p = 0; p < s.phraseCount; p++) {
    bool empty = true;  // the tracker does not save empty phrases either
    for (const Row& row : s.phrases[p].rows) empty = empty && row == Row();
    if (empty) continue;
    fprintf(f, "### Phrase %X\n\n```\n", p);
    for (const Row& row : s.phrases[p].rows) {
      string note = row.note == EMPTY8 ? "---" : row.note == NOTE_OFF ? "OFF" : s.noteNames[row.note];
      fprintf(f, "%s %s %s", note.c_str(), hex2OrEmpty(row.instrument, EMPTY8).c_str(),
              hex2OrEmpty(row.volume, EMPTY16).c_str());
      for (int i = 0; i < 3; i++) fprintf(f, " %s %s", row.fx[i].c_str(), hex2(row.fxValue[i]).c_str());
      fprintf(f, "\n");
    }
    fprintf(f, "```\n");
  }

  fprintf(f, "\n## Instruments\n");
  for (int i = 0; i < CCT_INSTRUMENTS; i++) {
    const Instrument& inst = s.instruments[i];
    if (!inst.used) continue;
    fprintf(f, "\n### Instrument %X\n\n- Name: %s\n- Type: %d\n- Table speed: 1\n- Volume: 255\n- Pan: 128\n"
               "- Transpose: 1\n- Modulation:\n- Mod1: 0,0,0,0,0,0,0,0\n- Mod2: 1,0,0,0,0,0,0,0\n"
               "- Mod3: 2,0,0,0,0,0,0,0\n- Mod4: 2,0,0,0,0,0,0,0\n- Chip data:\n%s",
            i, inst.name.c_str(), inst.type, chipData(inst).c_str());
  }
  fprintf(f, "\n## Tables\nEOF\n");
  bool ok = fclose(f) == 0;
  if (!ok) *error = "cannot write " + path;
  return ok;
}

// ---------------------------------------------------------------------------
// .cct reader

static int scanByte(const char* s, int empty) {
  if (s[0] == '-' && s[1] == '-') return empty;
  char b[3] = {s[0], s[1], 0};
  char* end;
  long v = strtol(b, &end, 16);
  return end == b ? empty : (int)v;
}

static void chomp(char* line) { line[strcspn(line, "\r\n")] = 0; }

static bool readCct(const string& path, Song* s, string* error) {
  FILE* f = fopen(path.c_str(), "r");
  if (!f) {
    *error = "cannot open " + path;
    return false;
  }
  char line[1024];
  if (!fgets(line, sizeof(line), f)) line[0] = 0;
  chomp(line);
  const char* v = nullptr;
  if (!strncmp(line, "# ChooChooTracker Module", 24)) v = line + 24;
  else if (!strncmp(line, "# ChipNomad Tracker Module", 26)) v = line + 26;
  if (!v) {
    fclose(f);
    *error = path + " is not a ChooChooTracker module";
    return false;
  }
  int version = *v ? atoi(v) : 1;

  enum { NONE, PITCH, SONG, CHAIN, GROOVE, PHRASE, INSTRUMENT } section = NONE;
  int index = 0, row = 0, songRow = 0;
  bool inBlock = false;
  while (fgets(line, sizeof(line), f)) {
    chomp(line);
    if (!strncmp(line, "## ", 3)) {
      inBlock = false;
      if (!strcmp(line, "## Pitch table")) section = PITCH;
      else if (!strcmp(line, "## Song")) section = SONG;
      else if (!strcmp(line, "## Tables")) break;  // nothing more needed
      else section = NONE;
      continue;
    }
    if (!strncmp(line, "### ", 4)) {
      inBlock = false;
      row = 0;
      if (sscanf(line, "### Chain %X", &index) == 1) section = CHAIN;
      else if (sscanf(line, "### Groove %X", &index) == 1) section = GROOVE;
      else if (sscanf(line, "### Phrase %X", &index) == 1) section = PHRASE;
      else if (sscanf(line, "### Instrument %X", &index) == 1) section = INSTRUMENT;
      else section = NONE;
      continue;
    }
    if (!strncmp(line, "```", 3)) {
      inBlock = !inBlock;
      continue;
    }
    if (section == NONE && s->noteNames.empty()) {
      float rate;
      if (sscanf(line, "- Frame rate: %f", &rate) == 1) s->tickRate = rate;
      if (!strncmp(line, "- Title: ", 9)) s->title = line + 9;
    }
    if (section == INSTRUMENT && index < CCT_INSTRUMENTS) {
      Instrument& inst = s->instruments[index];
      inst.used = true;
      if (!strncmp(line, "- Name: ", 8)) inst.name = line + 8;
      sscanf(line, "- Type: %d", &inst.type);
      sscanf(line, "- Cutoff: %d", &inst.cutoff);
      sscanf(line, "- Resonance: %d", &inst.resonance);
      continue;
    }
    if (!inBlock) continue;
    if (section == PITCH) {
      char name[8];
      if (sscanf(line, "%7s", name) == 1) s->noteNames.push_back(name);
    } else if (section == SONG && songRow < CCT_LENGTH) {
      int t = 0;
      for (char* cell = strtok(line, " *"); cell && t < CCT_TRACKS; cell = strtok(nullptr, " *"), t++)
        s->song[songRow][t] = scanByte(cell, EMPTY16);
      s->tracks = t;
      songRow++;
    } else if (section == CHAIN && index < CCT_CHAINS && row < 16 && strlen(line) >= 6) {
      ChainRow& r = s->chains[index].rows[row++];
      r.phrase = line[0] == '-' ? EMPTY16 : (int)strtol(std::string(line, 3).c_str(), nullptr, 16);
      r.transpose = scanByte(line + 4, 0);
    } else if (section == GROOVE && index < CCT_GROOVES && row < 16) {
      s->grooves[index].speed[row++] = scanByte(line, EMPTY8);
    } else if (section == PHRASE && index < CCT_PHRASES && row < 16 && strlen(line) >= 21) {
      Row& r = s->phrases[index].rows[row++];
      string note(line, 3);
      r.note = EMPTY8;
      if (note == "OFF") r.note = NOTE_OFF;
      for (size_t n = 0; n < s->noteNames.size(); n++)
        if (s->noteNames[n] == note) r.note = (int)n;
      r.instrument = scanByte(line + 4, EMPTY8);
      r.volume = scanByte(line + 7, EMPTY16);
      if (r.volume != EMPTY16)
        r.volume = version < 6 ? (std::min(r.volume, 15) * VOLUME_MAX + 7) / 15 : std::min(r.volume, VOLUME_MAX);
      for (int i = 0; i < 3; i++) {
        size_t at = 10 + i * 7;
        if (strlen(line) < at + 3) break;
        r.fx[i] = string(line + at, 3);
        if (strlen(line) >= at + 6) r.fxValue[i] = scanByte(line + at + 4, 0);
      }
    }
  }
  fclose(f);
  if (s->noteNames.empty()) s->noteNames = defaultNoteNames();
  return true;
}

// ===========================================================================
// Conversion

static const int TICKS_PER_STEP = 6;  // tickRate = 6 / (15 / bpm) = 0.4 * bpm

static int instrumentTypeFor(int machine) {
  switch (machine) {
    case M_OPL2: return T_OPL2;
    case M_PICODRUM: case M_MDADRUM: case M_ER1: return T_DRUMSYNTH;
    case M_MIDIOUT: return T_MIDI;
    case M_SIDSYNTH: return T_SID;
    case M_LGPTSAMPLER: return T_SAMPLE;
    default: return T_ACHCHID;
  }
}

static int machineFor(const Instrument& inst) {
  size_t space = inst.name.find(' ');
  if (space != string::npos)
    for (int m = 0; m < M_COUNT; m++)
      if (inst.name.substr(space + 1) == MACHINES[m]) return m;
  switch (inst.type) {
    case T_OPL2: case T_OPL3: case T_OPLL: case T_VRC7: return M_OPL2;
    case T_DRUMSYNTH: return M_PICODRUM;
    case T_SID: return M_SIDSYNTH;
    case T_MIDI: return M_MIDIOUT;
    case T_SAMPLE: case T_AYSAMPLE: return M_LGPTSAMPLER;
    default: return M_PICOSYNTH;
  }
}

static int cutoffToHz(int c) { return (int)(200 * pow((double)CUTOFF_MAX_HZ / 200, c / 127.0) + 0.5); }
static int cutoffFromHz(int hz) {
  if (hz <= 200) return 0;
  return clamp((int)(log(hz / 200.0) / log((double)CUTOFF_MAX_HZ / 200) * 127.0 + 0.5), 0, 127);
}
static int resonanceTo(int r) { return (r * 100 + 63) / 127; }
static int resonanceFrom(int r) {
  for (int v = 0; v < 128; v++)
    if (resonanceTo(v) >= r) return v;
  return 127;
}

struct Options {
  int length = 0;
  string title;
  bool force = false;
};

static bool toCct(const Bank& bank, const Options& opt, Song* s, vector<string>* warn, string* error) {
  s->noteNames = defaultNoteNames();
  s->title = opt.title.substr(0, 24);
  int length = std::min(opt.length > 0 ? opt.length : bank.songLength, SONG_LENGTH);
  if (length > CCT_LENGTH) {
    warn->push_back("song cut to 256 positions (picoloop song has " + std::to_string(length) + ")");
    length = CCT_LENGTH;
  }

  std::map<std::pair<int, int>, int> grooves, instruments, chains;
  auto groove = [&](int divider, int swing) {
    auto key = std::make_pair(divider, swing);
    if (grooves.count(key)) return grooves[key];
    int idx = (int)grooves.size();
    if (idx >= CCT_GROOVES) return 0;
    int first = (2 * TICKS_PER_STEP * swing + 50) / 100;  // swing% of a pair of steps
    s->grooves[idx].speed[0] = first * divider;
    s->grooves[idx].speed[1] = (2 * TICKS_PER_STEP - first) * divider;
    return grooves[key] = idx;
  };

  int bpm = 0;
  bool tempoChanges = false, lengthsDiffer = false;
  groove(1, 50);
  for (int pos = 0; pos < length; pos++) {
    int stepsAtPos = -1;
    for (int t = 0; t < TRACKS; t++) {
      const Pattern& pat = bank.patterns[bank.song[pos][t]][t];
      int steps = pat.exists ? pat.size * pat.divider : 16;
      if (stepsAtPos >= 0 && steps != stepsAtPos) lengthsDiffer = true;
      stepsAtPos = steps;
      if (!pat.exists) continue;
      groove(pat.divider, pat.swing);
      if (!bpm) bpm = pat.bpm;
      else if (pat.bpm != bpm) tempoChanges = true;
    }
  }
  if (!bpm) bpm = 120;
  s->tickRate = TICKS_PER_STEP * bpm / 15.0f;
  bool multiGroove = grooves.size() > 1;
  if (tempoChanges) warn->push_back("several BPM values in the song: all converted at " + std::to_string(bpm) + " BPM");
  if (lengthsDiffer)
    warn->push_back("patterns of different lengths at the same song position: picoloop moves the whole song on "
                    "when the first track loops, ChooChooTracker lets each track finish its chain");

  auto instrument = [&](int track, const Pattern& pat, int step) {
    int machine = clamp(pat.v[P_MACHINE][step], 0, M_COUNT - 1);
    auto key = std::make_pair(track, machine);
    if (instruments.count(key)) return instruments[key];
    int idx = (int)instruments.size();
    if (idx >= CCT_INSTRUMENTS) return 0;
    Instrument& inst = s->instruments[idx];
    inst.used = true;
    inst.type = instrumentTypeFor(machine);
    inst.name = "T" + std::to_string(track + 1) + " " + MACHINES[machine];
    // Settings of the first step that plays this machine; the per-step
    // parameter locks are not converted.
    if (inst.type == T_ACHCHID) {
      int osc = pat.v[P_OSC1][step];
      inst.wave = osc == 1 || osc == 5 ? 1 : osc == 2 || osc == 6 ? 0 : 2;  // saw, square, Braids
      if (machine == M_OPEN303) inst.wave = 1;
      inst.model = inst.wave != 2 ? 0 : osc == 8 || osc == 9 ? 41 : 3;    // Braids noise / sine-triangle
      inst.cutoff = cutoffToHz(pat.v[P_CUTOFF][step]);
      inst.resonance = resonanceTo(pat.v[P_RESONANCE][step]);
    } else if (inst.type == T_MIDI) {
      inst.channel = track;
    } else if (inst.type == T_SAMPLE) {
      warn->push_back(inst.name + ": picoloop sampler, pick the sample in ChooChooTracker");
    }
    return instruments[key] = idx;
  };

  auto addPhrase = [&](const Phrase& ph) {
    for (int i = 0; i < s->phraseCount; i++) {
      bool same = true;
      for (int r = 0; r < 16 && same; r++) same = s->phrases[i].rows[r] == ph.rows[r];
      if (same) return i;
    }
    if (s->phraseCount >= CCT_PHRASES) return -1;
    s->phrases[s->phraseCount] = ph;
    return s->phraseCount++;
  };

  // A pattern is a chain of ceil(size / 16) phrases, HOP 00 after the last
  // step. A missing pattern plays 16 silent steps in picoloop.
  auto chain = [&](int number, int track) {
    auto key = std::make_pair(number, track);
    if (chains.count(key)) return chains[key];
    if (s->chainCount >= CCT_CHAINS) return -1;
    static const Pattern silent;
    const Pattern& pat = bank.patterns[number][track].exists ? bank.patterns[number][track] : silent;
    Chain& c = s->chains[s->chainCount];
    for (int k = 0; k < (pat.size + 15) / 16; k++) {
      Phrase ph;
      for (int r = 0; r < 16; r++) {
        int step = k * 16 + r;
        Row& row = ph.rows[r];
        if (step >= pat.size) {
          row.fx[0] = "HOP";
          break;
        }
        if (pat.v[P_TRIG][step] && pat.v[P_NOTE][step] > 0) {
          row.note = std::min(pat.v[P_NOTE][step] - 1, PITCHES - 1);
          row.instrument = instrument(track, pat, step);
          row.volume = clamp(pat.v[P_VELOCITY][step], 0, VOLUME_MAX);
        }
      }
      if (k == 0 && multiGroove) {  // GRV is persistent per track
        ph.rows[0].fx[2] = "GRV";
        ph.rows[0].fxValue[2] = groove(pat.divider, pat.swing);
      }
      int idx = addPhrase(ph);
      if (idx < 0) return -1;
      c.rows[k].phrase = idx;
    }
    chains[key] = s->chainCount;
    return s->chainCount++;
  };

  for (int pos = 0; pos < length; pos++)
    for (int t = 0; t < TRACKS; t++) {
      int c = chain(bank.song[pos][t], t);
      if (c < 0) {
        *error = "too many chains or phrases for ChooChooTracker";
        return false;
      }
      s->song[pos][t] = c;
    }
  warn->insert(warn->begin(), std::to_string(length) + " song rows, " + std::to_string(s->chainCount) + " chains, " +
                                  std::to_string(s->phraseCount) + " phrases, " + std::to_string(instruments.size()) +
                                  " instruments, " + std::to_string(grooves.size()) + " grooves, " +
                                  std::to_string(bpm) + " BPM");
  return true;
}

static void grooveTiming(const Groove& g, double* rowTicks, int* swing) {
  int ticks[16], count = 0, sum = 0;
  for (int v : g.speed)
    if (v != EMPTY8 && v > 0) sum += ticks[count++] = v;
  *rowTicks = count ? (double)sum / count : TICKS_PER_STEP;
  *swing = clamp(count == 2 ? (100 * ticks[0] + sum / 2) / sum : 50, 25, 75);
}

static bool toPico(const Song& s, Bank* bank, vector<string>* warn, string* error) {
  double baseTicks;
  int baseSwing;
  grooveTiming(s.grooves[0], &baseTicks, &baseSwing);
  int bpm = clamp((int)(s.tickRate * 15.0 / baseTicks + 0.5), 20, 300);

  int tracks = std::min(s.tracks, TRACKS);
  for (int t = TRACKS; t < s.tracks; t++)
    for (int r = 0; r < CCT_LENGTH; r++)
      if (s.song[r][t] != EMPTY16) {
        warn->push_back("track " + std::to_string(t + 1) + " dropped: picoloop has 4 tracks");
        break;
      }
  int length = 0;
  for (int r = 0; r < CCT_LENGTH; r++)
    for (int t = 0; t < tracks; t++)
      if (s.song[r][t] != EMPTY16) length = r + 1;
  if (!length) {
    *error = "the song is empty";
    return false;
  }
  bank->songLength = length;

  bool truncated = false, mixed = false, offs = false;
  for (int t = 0; t < tracks; t++) {
    std::map<int, int> patternOf;
    int next = 0, silent = -1, groove = 0;
    for (int r = 0; r < length; r++) {
      int c = s.song[r][t];
      if (c == EMPTY16 || c >= CCT_CHAINS) {
        if (silent < 0) silent = next++;  // a pattern with no file plays 16 silent steps
        bank->song[r][t] = silent;
        continue;
      }
      for (const ChainRow& cr : s.chains[c].rows) {  // GRV at the start of the chain
        if (cr.phrase == EMPTY16) continue;
        const Row& first = s.phrases[cr.phrase].rows[0];
        for (int i = 0; i < 3; i++)
          if (first.fx[i] == "GRV") groove = first.fxValue[i] & (CCT_GROOVES - 1);
        break;
      }
      if (patternOf.count(c)) {
        bank->song[r][t] = patternOf[c];
        continue;
      }
      if (next >= PATTERNS) {
        warn->push_back("track " + std::to_string(t + 1) + ": more than 128 chains, extra ones are silent");
        bank->song[r][t] = std::max(silent, 0);
        continue;
      }
      int number = next++;
      patternOf[c] = bank->song[r][t] = number;

      Pattern& pat = bank->patterns[number][t];
      pat = Pattern();
      pat.exists = true;
      pat.bpm = bpm;
      double rowTicks;
      grooveTiming(s.grooves[groove], &rowTicks, &pat.swing);
      pat.divider = 1;
      for (int d = 2; d <= 8; d *= 2)
        if (fabs(rowTicks / baseTicks - d) < fabs(rowTicks / baseTicks - pat.divider)) pat.divider = d;

      int steps = 0, machine = -1, last = 0;
      for (const ChainRow& cr : s.chains[c].rows) {
        if (cr.phrase == EMPTY16 || steps > STEPS) continue;
        for (const Row& row : s.phrases[cr.phrase].rows) {
          if (row.fx[0] == "HOP" || row.fx[1] == "HOP" || row.fx[2] == "HOP") break;
          if (steps >= STEPS) {
            truncated = true;
            steps = STEPS + 1;
            break;
          }
          int step = steps++;
          if (row.note == NOTE_OFF) offs = true;
          if (row.note == EMPTY8 || row.note == NOTE_OFF) continue;
          if (row.instrument != EMPTY8 && row.instrument < CCT_INSTRUMENTS) last = row.instrument;
          const Instrument& inst = s.instruments[last];
          int m = machineFor(inst);
          if (machine < 0) machine = m;
          else if (m != machine) mixed = true;
          pat.v[P_TRIG][step] = 1;
          pat.v[P_NOTE][step] = clamp(row.note + 1 + (int8_t)cr.transpose, 1, 108);
          pat.v[P_VELOCITY][step] = row.volume == EMPTY16 ? 127 : std::min(row.volume, 127);
          if (inst.type == T_ACHCHID) {
            pat.v[P_CUTOFF][step] = cutoffFromHz(inst.cutoff);
            pat.v[P_RESONANCE][step] = resonanceFrom(inst.resonance);
          }
        }
      }
      pat.size = steps > STEPS ? STEPS : steps > 0 ? steps : 16;
      if (machine < 0) machine = M_PICOSYNTH;
      bool anyTrig = false;
      for (int i = 0; i < STEPS; i++) {
        pat.v[P_MACHINE][i] = machine;  // one machine per pattern
        anyTrig |= i < pat.size && pat.v[P_TRIG][i];
      }
      if (!anyTrig && pat.size == 16) {  // same as a missing pattern
        pat.exists = false;
        if (silent < 0) silent = number;
      }
    }
  }
  if (truncated) warn->push_back("chains longer than 128 rows were cut to 128 steps");
  if (mixed) warn->push_back("some chains use several instrument kinds: picoloop keeps one machine per pattern");
  if (offs) warn->push_back("note offs (OFF) dropped: picoloop has no note off step");
  warn->insert(warn->begin(), std::to_string(length) + " song positions, " + std::to_string(bpm) + " BPM, swing " +
                                  std::to_string(baseSwing));
  return true;
}

// ===========================================================================

static int usage() {
  fprintf(stderr,
          "usage:\n"
          "  pico2cct to-cct <picoloop bank folder> <song.cct> [--length N] [--title TEXT]\n"
          "  pico2cct to-picoloop <song.cct> <picoloop bank folder> [--force]\n"
          "\n"
          "A picoloop bank folder is <picoloop storage>/bank/bank<N>, holding\n"
          "dataP<pattern>T<track>.pic files and song.pic.\n");
  return 2;
}

int main(int argc, char** argv) {
  if (argc < 4) return usage();
  string command = argv[1], from = argv[2], to = argv[3];
  Options opt;
  for (int i = 4; i < argc; i++) {
    if (!strcmp(argv[i], "--length") && i + 1 < argc) opt.length = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--title") && i + 1 < argc) opt.title = argv[++i];
    else if (!strcmp(argv[i], "--force")) opt.force = true;
    else return usage();
  }
  std::unique_ptr<Bank> bank(new Bank());
  std::unique_ptr<Song> song(new Song());
  vector<string> messages;
  string error;
  bool ok;
  if (command == "to-cct")
    ok = loadBank(from, bank.get(), &error) && toCct(*bank, opt, song.get(), &messages, &error) &&
         writeCct(*song, to, &error);
  else if (command == "to-picoloop")
    ok = readCct(from, song.get(), &error) && toPico(*song, bank.get(), &messages, &error) &&
         saveBank(to, *bank, opt.force, &error);
  else
    return usage();
  if (!ok) {
    fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }
  for (size_t i = 0; i < messages.size(); i++) printf(i ? "warning: %s\n" : "%s\n", messages[i].c_str());
  printf("wrote %s\n", to.c_str());
  return 0;
}
