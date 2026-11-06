// ChooChooTracker side of the converter: builds or reads a chipnomad_lib
// Project and saves/loads it with the tracker's own projectSave/projectLoad,
// so every .cct written here is one the tracker accepts.
#include <chipnomad_lib.h>
#include <project_utils.h>
#include <import_common.h>
#include <synth/multimode_filter.h>

#include "pico_song.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <memory>
#include <utility>

namespace cctconv {

// ---------------------------------------------------------------------------
// Shared mapping tables

// projectSave/projectLoad write and parse FX by name: the tracker fills the
// name table at startup (chipnomad_lib.cpp), a command line tool must too.
static void initFXNames() {
  static bool done = false;
  if (!done) fillFXNames();
  done = true;
}

// One picoloop step is a 16th note. ChooChooTracker plays one phrase row per
// groove entry; with 6 ticks per row a 16th lasts 6 ticks, so
// tickRate = 6 / (15 / bpm) = 0.4 * bpm.
static const int TICKS_PER_STEP = 6;

static const char* machineName(int machine) {
  static const char* names[] = {"PicoSynth", "OPL2", "PicoDrum", "PBSynth", "Cursynth", "Open303",
                                "Twytch", "MIDIOut", "MDADrum", "SIDSynth", "LGPTSampler", "ER1"};
  return machine >= 0 && machine < (int)(sizeof(names) / sizeof(names[0])) ? names[machine] : "Machine";
}

static InstrumentType instrumentTypeFor(int machine) {
  switch (machine) {
    case PICO_OPL2: return InstrumentType::OPL2;
    case PICO_PICODRUM:
    case PICO_MDADRUM:
    case PICO_ER1: return InstrumentType::DrumSynth;
    case PICO_MIDIOUT: return InstrumentType::Midi;
    case PICO_SIDSYNTH: return InstrumentType::SID;
    case PICO_LGPTSAMPLER: return InstrumentType::Sample;
    default: return InstrumentType::AChChid;  // the subtractive synths and Open303
  }
}

static int machineFor(const Instrument& inst) {
  // Instruments written by this tool are named "T<n> <machine>": keep the
  // exact machine on a round trip.
  for (int m = 0; m <= PICO_ER1; m++) {
    const char* space = strchr(inst.name, ' ');
    if (space && strcmp(space + 1, machineName(m)) == 0) return m;
  }
  switch (inst.type) {
    case InstrumentType::OPL2:
    case InstrumentType::OPL3:
    case InstrumentType::OPLL:
    case InstrumentType::VRC7: return PICO_OPL2;
    case InstrumentType::DrumSynth: return PICO_PICODRUM;
    case InstrumentType::SID: return PICO_SIDSYNTH;
    case InstrumentType::Midi: return PICO_MIDIOUT;
    case InstrumentType::Sample:
    case InstrumentType::AYSample: return PICO_LGPTSAMPLER;
    default: return PICO_PICOSYNTH;
  }
}

// picoloop cutoff 0-127 <-> Hz, exponential like the M8 importer does.
static uint16_t cutoffToHz(int cutoff, int minHz) {
  double ratio = (double)FILTER_CUTOFF_MAX_HZ / minHz;
  return (uint16_t)(minHz * pow(ratio, cutoff / 127.0) + 0.5);
}
static int cutoffFromHz(int hz, int minHz) {
  if (hz <= minHz) return 0;
  double v = log((double)hz / minHz) / log((double)FILTER_CUTOFF_MAX_HZ / minHz) * 127.0;
  return v > 127 ? 127 : (int)(v + 0.5);
}

// picoloop resonance 0-127 <-> aChChid resonance 0-100. The inverse picks the
// smallest picoloop value giving that resonance, so a round trip is stable.
static uint8_t resonanceToAChChid(int resonance) { return (uint8_t)((resonance * 100 + 63) / 127); }
static int resonanceFromAChChid(int resonance) {
  for (int v = 0; v < 128; v++)
    if (resonanceToAChChid(v) >= resonance) return v;
  return 127;
}

static bool sameRow(const PhraseRow& a, const PhraseRow& b) {
  if (a.note != b.note || a.instrument != b.instrument || a.volume != b.volume) return false;
  for (int i = 0; i < 3; i++)
    if (a.fx[i].command != b.fx[i].command || a.fx[i].value != b.fx[i].value) return false;
  return true;
}

// ---------------------------------------------------------------------------
// picoloop -> ChooChooTracker

namespace {
struct Builder {
  const PicoBank& bank;
  Project& p;
  std::vector<std::string>* warnings;
  std::map<std::pair<int, int>, int> grooves;     // (divider, swing) -> groove
  std::map<std::pair<int, int>, int> instruments; // (track, machine) -> slot
  std::map<std::pair<int, int>, int> chains;      // (pattern, track) -> chain
  int phraseCount = 0;
  int chainCount = 0;
  bool multiGroove = false;

  Builder(const PicoBank& b, Project& project, std::vector<std::string>* w) : bank(b), p(project), warnings(w) {}

  int groove(int divider, int swing) {
    auto key = std::make_pair(divider, swing);
    auto it = grooves.find(key);
    if (it != grooves.end()) return it->second;
    int idx = (int)grooves.size();
    if (idx >= PROJECT_MAX_GROOVES) return 0;
    // picoloop swing delays every second 16th: swing% of a pair of steps.
    int first = (2 * TICKS_PER_STEP * swing + 50) / 100;
    Groove& g = p.grooves[idx];
    for (int i = 0; i < 16; i++) g.speed[i] = EMPTY_VALUE_8;
    g.speed[0] = (uint8_t)(first * divider);
    g.speed[1] = (uint8_t)((2 * TICKS_PER_STEP - first) * divider);
    grooves[key] = idx;
    return idx;
  }

  int instrument(int track, const PicoStep& step) {
    auto key = std::make_pair(track, step.machine);
    auto it = instruments.find(key);
    if (it != instruments.end()) return it->second;
    int idx = (int)instruments.size();
    if (idx >= PROJECT_MAX_INSTRUMENTS) return 0;
    Instrument* inst = &p.instruments[idx];
    InstrumentType type = instrumentTypeFor(step.machine);
    getInstrumentFunctions(type).init(inst);
    inst->type = type;
    snprintf(inst->name, sizeof(inst->name), "T%d %s", track + 1, machineName(step.machine));
    // Sound parameters come from the first step that plays this machine;
    // the per-step parameter locks of picoloop are not converted.
    if (type == InstrumentType::AChChid) {
      InstrumentAChChid* a = &inst->chip.achchid;
      switch (step.osc1Type) {
        case 1: case 5: a->wave = AChChidWave::saw; break;      // SAW, SMSAW
        case 2: case 6: a->wave = AChChidWave::square; break;   // PULSE, SMPULSE
        case 8: case 9: a->wave = AChChidWave::braids; a->model = 41; break; // noise
        default: a->wave = AChChidWave::braids; a->model = 3; break;         // sine/triangle
      }
      if (step.machine == PICO_OPEN303) a->wave = AChChidWave::saw;
      a->cutoff = cutoffToHz(step.cutoff, 200);
      a->resonance = resonanceToAChChid(step.resonance);
    } else if (type == InstrumentType::Midi) {
      inst->chip.midi.channel = (uint8_t)track;
    } else if (type == InstrumentType::Sample) {
      warnings->push_back(std::string(inst->name) + ": picoloop sampler, pick the sample in ChooChooTracker");
    }
    instruments[key] = idx;
    return idx;
  }

  int addPhrase(const Phrase& phrase) {
    for (int i = 0; i < phraseCount; i++) {
      bool same = true;
      for (int r = 0; r < 16 && same; r++) same = sameRow(p.phrases[i].rows[r], phrase.rows[r]);
      if (same) return i;
    }
    if (phraseCount >= PROJECT_MAX_PHRASES) return -1;
    p.phrases[phraseCount] = phrase;
    return phraseCount++;
  }

  // A picoloop pattern becomes a chain of ceil(size / 16) phrases. A HOP on
  // the row after the last step ends a pattern whose length is not a
  // multiple of 16. A missing pattern plays as 16 silent steps in picoloop.
  int chain(int pattern, int track) {
    auto key = std::make_pair(pattern, track);
    auto it = chains.find(key);
    if (it != chains.end()) return it->second;
    if (chainCount >= PROJECT_MAX_CHAINS) return -1;

    static const PicoPattern silent;
    const PicoPattern& src = bank.patterns[pattern][track].exists ? bank.patterns[pattern][track] : silent;
    int grooveIdx = groove(src.divider, src.swing);
    int phrases = (src.size + 15) / 16;
    Chain* c = &p.chains[chainCount];
    chainClear(c);
    for (int k = 0; k < phrases; k++) {
      Phrase ph;
      for (int r = 0; r < 16; r++) initEmptyPhraseRow(&ph.rows[r]);
      for (int r = 0; r < 16; r++) {
        int s = k * 16 + r;
        PhraseRow& row = ph.rows[r];
        if (s >= src.size) {
          row.fx[0].command = fxHOP;
          row.fx[0].value = 0;
          break;
        }
        const PicoStep& step = src.steps[s];
        if (step.trig && step.note > 0) {
          int note = step.note - 1;
          if (note >= p.pitchTable.length) note = p.pitchTable.length - 1;
          row.note = (uint8_t)note;
          row.instrument = (uint8_t)instrument(track, step);
          row.volume = (uint16_t)(step.velocity > PHRASE_VOLUME_MAX ? PHRASE_VOLUME_MAX : step.velocity);
        }
      }
      // GRV is persistent per track: set it at the start of every pattern
      // once more than one groove (divider / swing) is in use.
      if (k == 0 && multiGroove) {
        ph.rows[0].fx[2].command = fxGRV;
        ph.rows[0].fx[2].value = (uint8_t)grooveIdx;
      }
      int idx = addPhrase(ph);
      if (idx < 0) return -1;
      c->rows[k].phrase = (uint16_t)idx;
      c->rows[k].transpose = 0;
    }
    chains[key] = chainCount;
    return chainCount++;
  }

};
}  // namespace

bool cctFromPico(const PicoBank& bank, const ConvertOptions& options, const std::string& cctPath,
                 std::vector<std::string>* warnings, std::string* error) {
  initFXNames();
  auto owned = std::make_unique<Project>();
  Project& p = *owned;
  projectInitAY(&p);

  int length = options.songLength > 0 ? options.songLength : bank.songLength;
  if (length > PICO_SONG_LENGTH) length = PICO_SONG_LENGTH;
  if (length > PROJECT_MAX_LENGTH) {
    warnings->push_back("song cut to " + std::to_string(PROJECT_MAX_LENGTH) + " positions (picoloop song has " +
                        std::to_string(length) + ")");
    length = PROJECT_MAX_LENGTH;
  }

  // Tempo: picoloop takes the BPM of the loaded pattern; use the first one.
  int bpm = 0;
  bool tempoChanges = false, lengthsDiffer = false;
  for (int pos = 0; pos < length; pos++) {
    int stepsAtPos = -1;
    for (int t = 0; t < PICO_TRACKS; t++) {
      const PicoPattern& pat = bank.patterns[bank.song[pos][t]][t];
      int steps = pat.exists ? pat.size * pat.divider : 16;
      if (stepsAtPos >= 0 && steps != stepsAtPos) lengthsDiffer = true;
      stepsAtPos = steps;
      if (!pat.exists) continue;
      if (!bpm) bpm = pat.bpm;
      else if (pat.bpm != bpm) tempoChanges = true;
    }
  }
  if (!bpm) bpm = 120;
  p.tickRate = (float)(TICKS_PER_STEP * bpm / 15.0);
  if (tempoChanges) warnings->push_back("several BPM values in the song: all converted at " + std::to_string(bpm) + " BPM");
  if (lengthsDiffer)
    warnings->push_back("patterns of different lengths at the same song position: picoloop moves the whole song on "
                        "when the first track loops, ChooChooTracker lets each track finish its chain");

  std::string title = options.title;
  snprintf(p.title, sizeof(p.title), "%s", title.c_str());

  Builder b(bank, p, warnings);
  b.groove(1, 50);  // groove 0: the default 6/6
  for (int pos = 0; pos < length; pos++)
    for (int t = 0; t < PICO_TRACKS; t++) {
      const PicoPattern& pat = bank.patterns[bank.song[pos][t]][t];
      if (pat.exists) b.groove(pat.divider, pat.swing);
    }
  b.multiGroove = b.grooves.size() > 1;
  for (int pos = 0; pos < length; pos++) {
    for (int t = 0; t < PICO_TRACKS; t++) {
      int chain = b.chain(bank.song[pos][t], t);
      if (chain < 0) {
        *error = "too many chains or phrases for ChooChooTracker";
        projectFree(&p);
        return false;
      }
      p.song[pos][t] = (uint16_t)chain;
    }
  }

  if (projectSave(&p, cctPath.c_str())) {
    *error = "cannot write " + cctPath + ": " + projectFileError;
    projectFree(&p);
    return false;
  }
  char info[160];
  snprintf(info, sizeof(info), "%d song rows, %d chains, %d phrases, %d instruments, %d grooves, %d BPM",
           length, b.chainCount, b.phraseCount, (int)b.instruments.size(), (int)b.grooves.size(), bpm);
  warnings->insert(warnings->begin(), info);
  projectFree(&p);
  return true;
}

// ---------------------------------------------------------------------------
// ChooChooTracker -> picoloop

// Mean ticks per row and picoloop swing (25-75) of a groove.
static void grooveTiming(const Groove& g, double* rowTicks, int* swing) {
  int ticks[16], count = 0, sum = 0;
  for (int i = 0; i < 16; i++)
    if (g.speed[i] != EMPTY_VALUE_8 && g.speed[i] > 0) sum += ticks[count++] = g.speed[i];
  *rowTicks = count ? (double)sum / count : TICKS_PER_STEP;
  *swing = count == 2 ? (100 * ticks[0] + sum / 2) / sum : 50;
  if (*swing < 25) *swing = 25;
  if (*swing > 75) *swing = 75;
}

static int dividerFor(double rowTicks, double baseTicks) {
  double ratio = rowTicks / baseTicks;
  int best = 1;
  for (int d = 2; d <= 8; d *= 2)
    if (fabs(ratio - d) < fabs(ratio - best)) best = d;
  return best;
}

bool cctToPico(const std::string& cctPath, PicoBank* bank, std::vector<std::string>* warnings, std::string* error) {
  initFXNames();
  auto owned = std::make_unique<Project>();
  Project& p = *owned;
  projectInit(&p);
  if (projectLoad(&p, cctPath.c_str())) {
    *error = "cannot read " + cctPath + ": " + projectFileError;
    return false;
  }

  // Tempo from groove 0. Another groove set by GRV at the start of a chain
  // gives that pattern its divider (ticks per row relative to groove 0) and
  // swing.
  double baseTicks;
  int swing;
  grooveTiming(p.grooves[0], &baseTicks, &swing);
  int bpm = (int)(p.tickRate * 15.0 / baseTicks + 0.5);
  if (bpm < 20) bpm = 20;
  if (bpm > 300) bpm = 300;

  int tracks = p.tracksCount < PICO_TRACKS ? p.tracksCount : PICO_TRACKS;
  for (int t = PICO_TRACKS; t < p.tracksCount; t++)
    for (int r = 0; r < PROJECT_MAX_LENGTH; r++)
      if (p.song[r][t] != EMPTY_VALUE_16) {
        warnings->push_back("track " + std::to_string(t + 1) + " dropped: picoloop has 4 tracks");
        break;
      }

  int length = 0;
  for (int r = 0; r < PROJECT_MAX_LENGTH; r++)
    for (int t = 0; t < tracks; t++)
      if (p.song[r][t] != EMPTY_VALUE_16) length = r + 1;
  if (!length) {
    *error = "the song is empty";
    projectFree(&p);
    return false;
  }
  bank->songLength = length;

  bool truncated = false, mixedMachines = false, offs = false;
  for (int t = 0; t < tracks; t++) {
    std::map<int, int> patternOf;  // chain -> picoloop pattern
    int next = 0, silent = -1;
    int groove = 0;                // GRV is persistent per track
    for (int r = 0; r < length; r++) {
      int chain = p.song[r][t];
      if (chain == EMPTY_VALUE_16) {
        // A pattern number with no file plays 16 silent steps.
        if (silent < 0) silent = next++;
        bank->song[r][t] = silent;
        continue;
      }
      for (int cr = 0; cr < 16; cr++) {
        uint16_t first = p.chains[chain].rows[cr].phrase;
        if (first == EMPTY_VALUE_16) continue;
        for (int f = 0; f < 3; f++)
          if (p.phrases[first].rows[0].fx[f].command == fxGRV)
            groove = p.phrases[first].rows[0].fx[f].value & (PROJECT_MAX_GROOVES - 1);
        break;
      }
      auto it = patternOf.find(chain);
      if (it != patternOf.end()) {
        bank->song[r][t] = it->second;
        continue;
      }
      if (next >= PICO_PATTERNS) {
        warnings->push_back("track " + std::to_string(t + 1) + ": more than 128 chains, extra ones are silent");
        bank->song[r][t] = silent >= 0 ? silent : 0;
        continue;
      }
      int number = next++;
      patternOf[chain] = number;
      bank->song[r][t] = number;

      PicoPattern& pat = bank->patterns[number][t];
      pat = PicoPattern();
      pat.exists = true;
      pat.bpm = bpm;
      double rowTicks;
      grooveTiming(p.grooves[groove], &rowTicks, &pat.swing);
      pat.divider = dividerFor(rowTicks, baseTicks);
      int steps = 0, machine = -1, lastInstrument = 0;
      for (int cr = 0; cr < 16 && steps < PICO_STEPS + 1; cr++) {
        const ChainRow& crow = p.chains[chain].rows[cr];
        if (crow.phrase == EMPTY_VALUE_16) continue;
        int transpose = (int8_t)crow.transpose;
        const Phrase& ph = p.phrases[crow.phrase];
        for (int pr = 0; pr < 16; pr++) {
          const PhraseRow& row = ph.rows[pr];
          bool hop = false;
          for (int f = 0; f < 3; f++) hop |= row.fx[f].command == fxHOP;
          if (hop) break;
          if (steps >= PICO_STEPS) { truncated = true; steps++; break; }
          PicoStep& s = pat.steps[steps++];
          if (row.note == NOTE_OFF) offs = true;
          if (row.note == EMPTY_VALUE_8 || row.note == NOTE_OFF) continue;
          if (row.instrument != EMPTY_VALUE_8) lastInstrument = row.instrument;
          const Instrument& inst = p.instruments[lastInstrument];
          int m = machineFor(inst);
          if (machine < 0) machine = m;
          else if (m != machine) mixedMachines = true;
          int note = row.note + 1 + (inst.transposeEnabled ? transpose : 0);
          s.trig = 1;
          s.note = note < 1 ? 1 : note > 108 ? 108 : note;
          s.velocity = row.volume == EMPTY_VALUE_16 ? 127 : (row.volume > 127 ? 127 : row.volume);
          if (inst.type == InstrumentType::AChChid) {
            s.cutoff = cutoffFromHz(inst.chip.achchid.cutoff, 200);
            s.resonance = resonanceFromAChChid(inst.chip.achchid.resonance);
          }
        }
      }
      if (steps > PICO_STEPS) steps = PICO_STEPS;
      pat.size = steps > 0 ? steps : 16;
      if (machine < 0) machine = PICO_PICOSYNTH;
      for (int i = 0; i < PICO_STEPS; i++) pat.steps[i].machine = machine;  // one machine per pattern
      // 16 silent steps is what picoloop plays for a missing pattern file.
      bool anyTrig = false;
      for (int i = 0; i < pat.size; i++) anyTrig |= pat.steps[i].trig != 0;
      if (!anyTrig && pat.size == 16) {
        pat.exists = false;
        if (silent < 0) silent = number;
      }
    }
  }
  if (truncated) warnings->push_back("chains longer than 128 rows were cut to 128 steps");
  if (mixedMachines) warnings->push_back("some chains use several instrument kinds: picoloop keeps one machine per pattern");
  if (offs) warnings->push_back("note offs (OFF) dropped: picoloop has no note off step");
  warnings->insert(warnings->begin(), std::to_string(length) + " song positions, " + std::to_string(bpm) +
                                          " BPM, swing " + std::to_string(swing));
  projectFree(&p);
  return true;
}

}  // namespace cctconv
