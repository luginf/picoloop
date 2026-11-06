// picoloop side of the converter: reads and writes a picoloop bank folder
// (dataP<pattern>T<track>.pic + song.pic) with picoloop's own PatternReader,
// so the file format stays exactly the one picoloop writes and reads.
#include "Master.h"
#include "Pattern.h"
#include "PatternElement.h"
#include "PatternReader.h"
#include "SongSequencer.h"

#include "pico_song.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>

// SYSTEM.h hooks normally provided by SYSTEM<platform>.cpp. PatternReader only
// uses them through setCustomRoot(), which points it at the bank folder.
static char g_storage[1024] = ".";
char* GETPICOLOOPUSERSTORAGE() { return g_storage; }
int MKDIR(const char* pathname) { mkdir(pathname, 0755); return 0; }

namespace cctconv {

static bool isDir(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool isPatternFile(const char* name) {
  int p, t;
  char tail[8];
  return sscanf(name, "dataP%dT%d.%7s", &p, &t, tail) == 3 && strcmp(tail, "pic") == 0;
}

static void toPico(const PicoPattern& src, Pattern* dst) {
  dst->init();
  dst->setSize(src.size);
  dst->setBPM((float)src.bpm);
  dst->setBPMDivider(src.divider);
  dst->setSwing(src.swing);
  for (int i = 0; i < PICO_STEPS; i++) {
    const PicoStep& s = src.steps[i];
    PatternElement& e = dst->getPatternElement(i);
    e.set(NOTE_ON, s.trig);
    e.set(NOTE1, s.note);
    e.set(VELOCITY, s.velocity);
    e.set(MACHINE_TYPE, s.machine);
    e.set(AMP, s.amp);
    e.set(FILTER1_CUTOFF, s.cutoff);
    e.set(FILTER1_RESONANCE, s.resonance);
    e.set(ADSR_AMP_ATTACK, s.attack);
    e.set(ADSR_AMP_DECAY, s.decay);
    e.set(ADSR_AMP_SUSTAIN, s.sustain);
    e.set(ADSR_AMP_RELEASE, s.release);
    e.set(OSC1_TYPE, s.osc1Type);
    e.set(TRIG_TIME_DURATION, s.trigTime);
  }
}

static void fromPico(Pattern& src, PicoPattern* dst) {
  dst->exists = true;
  dst->size = src.getSize();
  dst->bpm = (int)src.getBPM();
  dst->divider = src.getBPMDivider();
  dst->swing = src.getSwing();
  for (int i = 0; i < PICO_STEPS; i++) {
    PatternElement& e = src.getPatternElement(i);
    PicoStep& s = dst->steps[i];
    s.trig = e.get(NOTE_ON);
    s.note = e.get(NOTE1);
    s.velocity = e.get(VELOCITY);
    s.machine = e.get(MACHINE_TYPE);
    s.amp = e.get(AMP);
    s.cutoff = e.get(FILTER1_CUTOFF);
    s.resonance = e.get(FILTER1_RESONANCE);
    s.attack = e.get(ADSR_AMP_ATTACK);
    s.decay = e.get(ADSR_AMP_DECAY);
    s.sustain = e.get(ADSR_AMP_SUSTAIN);
    s.release = e.get(ADSR_AMP_RELEASE);
    s.osc1Type = e.get(OSC1_TYPE);
    s.trigTime = e.get(TRIG_TIME_DURATION);
  }
}

bool picoLoadBank(const std::string& dir, PicoBank* bank, std::string* error) {
  if (!isDir(dir)) {
    *error = "not a folder: " + dir;
    return false;
  }
  snprintf(g_storage, sizeof(g_storage), "%s", dir.c_str());
  std::unique_ptr<PatternReader> reader(new PatternReader());
  reader->init();
  reader->setCustomRoot(dir.c_str());

  SongSequencer song;
  reader->loadSong(song);
  for (int pos = 0; pos < PICO_SONG_LENGTH; pos++)
    for (int t = 0; t < PICO_TRACKS; t++) bank->song[pos][t] = song.songVector[pos][t];

  int found = 0;
  Pattern pattern;
  for (int p = 0; p < PICO_PATTERNS; p++) {
    for (int t = 0; t < PICO_TRACKS; t++) {
      char path[1200];
      snprintf(path, sizeof(path), "%s/dataP%dT%d.pic", dir.c_str(), p, t);
      if (access(path, R_OK) != 0) continue;
      pattern.init();
      if (!reader->readPatternData(p, t, pattern)) continue;
      fromPico(pattern, &bank->patterns[p][t]);
      found++;
    }
  }
  if (!found) {
    *error = "no picoloop pattern (dataP<n>T<t>.pic) in " + dir;
    return false;
  }

  bank->songLength = 1;
  for (int pos = 0; pos < PICO_SONG_LENGTH; pos++)
    for (int t = 0; t < PICO_TRACKS; t++)
      if (bank->song[pos][t] != 0) bank->songLength = pos + 1;
  return true;
}

bool picoSaveBank(const std::string& dir, const PicoBank& bank, bool force, std::string* error) {
  mkdir(dir.c_str(), 0755);
  if (!isDir(dir)) {
    *error = "cannot create folder " + dir;
    return false;
  }
  // PatternReader::writePattern() keeps the other lines of an existing file
  // and nothing removes the patterns this song does not use: start from a
  // folder without patterns so that no stale pattern stays in the bank.
  DIR* d = opendir(dir.c_str());
  std::vector<std::string> stale;
  if (d) {
    while (struct dirent* entry = readdir(d))
      if (isPatternFile(entry->d_name) || strcmp(entry->d_name, "song.pic") == 0)
        stale.push_back(dir + "/" + entry->d_name);
    closedir(d);
  }
  if (!stale.empty()) {
    if (!force) {
      *error = dir + " already holds a picoloop bank (use --force to replace it)";
      return false;
    }
    for (const std::string& f : stale) unlink(f.c_str());
  }

  snprintf(g_storage, sizeof(g_storage), "%s", dir.c_str());
  std::unique_ptr<PatternReader> reader(new PatternReader());
  reader->init();
  reader->setCustomRoot(dir.c_str());

  Pattern pattern;
  for (int p = 0; p < PICO_PATTERNS; p++)
    for (int t = 0; t < PICO_TRACKS; t++) {
      if (!bank.patterns[p][t].exists) continue;
      toPico(bank.patterns[p][t], &pattern);
      reader->writePattern(p, t, pattern);
    }

  SongSequencer song;
  for (int pos = 0; pos < PICO_SONG_LENGTH; pos++)
    for (int t = 0; t < PICO_TRACKS; t++) song.songVector[pos][t] = bank.song[pos][t];
  reader->saveSong(song);
  return true;
}

}  // namespace cctconv
