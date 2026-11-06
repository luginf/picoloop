// Neutral in-memory picture of a picoloop bank, shared by the picoloop side
// (pico_side.cpp, compiled with picoloop's headers) and the ChooChooTracker
// side (cct_side.cpp, compiled with chipnomad_lib's headers). It must not
// include either project's headers: their macros collide.
#ifndef CCTCONV_PICO_SONG_H
#define CCTCONV_PICO_SONG_H

#include <stdint.h>
#include <string>
#include <vector>

namespace cctconv {

const int PICO_TRACKS = 4;            // TRACK_MAX
const int PICO_STEPS = 128;           // MAX_STEP_PER_TRACK
const int PICO_PATTERNS = 128;        // MAX_PATTERN_BY_PROJECT (desktop/Android)
const int PICO_SONG_LENGTH = 1024;    // MAX_SONG_LENGHT_BY_PROJECT

// picoloop machine ids (Master.h SYNTH_*).
enum PicoMachine {
  PICO_PICOSYNTH = 0, PICO_OPL2 = 1, PICO_PICODRUM = 2, PICO_PBSYNTH = 3,
  PICO_CURSYNTH = 4, PICO_OPEN303 = 5, PICO_TWYTCHSYNTH = 6, PICO_MIDIOUT = 7,
  PICO_MDADRUM = 8, PICO_SIDSYNTH = 9, PICO_LGPTSAMPLER = 10, PICO_ER1 = 11,
};

// The subset of picoloop's per-step data the converter uses. picoloop stores
// every synth parameter on every step (parameter locks); the other ones keep
// their picoloop defaults when writing.
struct PicoStep {
  int trig = 0;        // NOTE_ON (Trig)
  int note = 0;        // NOTE1: 1 = C-1 (16.35 Hz) ... 0 = none
  int velocity = 96;   // VELOCITY 0-127
  int machine = 0;     // MACHINE_TYPE
  int amp = 127;       // AMP
  int cutoff = 112;    // FILTER1_CUTOFF 0-127
  int resonance = 16;  // FILTER1_RESONANCE 0-127
  int attack = 1, decay = 112, sustain = 8, release = 24; // ADSR_AMP_* 0-127
  int osc1Type = 1;    // OSC1_TYPE (picosynth: PICO_WAVETABLE_*)
  int trigTime = 1;    // TRIG_TIME_DURATION
};

struct PicoPattern {
  bool exists = false;
  int size = 16;       // 1-128 steps
  int bpm = 120;
  int divider = 1;     // 1, 2, 4 or 8: one step lasts `divider` 16th notes
  int swing = 50;      // 25-75, 50 is straight
  PicoStep steps[PICO_STEPS];
};

struct PicoBank {
  // patterns[pattern][track]: picoloop stores each (pattern, track) pair in
  // its own dataP<pattern>T<track>.pic file.
  std::vector<std::vector<PicoPattern>> patterns;
  // song[position][track] = pattern number (song.pic).
  std::vector<std::vector<int>> song;
  int songLength = 1;

  PicoBank()
    : patterns(PICO_PATTERNS, std::vector<PicoPattern>(PICO_TRACKS)),
      song(PICO_SONG_LENGTH, std::vector<int>(PICO_TRACKS, 0)) {}
};

// pico_side.cpp
bool picoLoadBank(const std::string& dir, PicoBank* bank, std::string* error);
bool picoSaveBank(const std::string& dir, const PicoBank& bank, bool force, std::string* error);

// cct_side.cpp
struct ConvertOptions {
  int songLength = 0;  // 0 = guess (last song position that is not all 0)
  std::string title;
};
bool cctFromPico(const PicoBank& bank, const ConvertOptions& options, const std::string& cctPath,
                 std::vector<std::string>* warnings, std::string* error);
bool cctToPico(const std::string& cctPath, PicoBank* bank,
               std::vector<std::string>* warnings, std::string* error);

}  // namespace cctconv

#endif
