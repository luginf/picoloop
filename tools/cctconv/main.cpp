// picoloop2cct: converts a picoloop bank to a ChooChooTracker song and back.
#include "pico_song.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>

using namespace cctconv;

static int usage() {
  fprintf(stderr,
          "usage:\n"
          "  picoloop2cct to-cct <picoloop bank folder> <song.cct> [--length N] [--title TEXT]\n"
          "  picoloop2cct to-picoloop <song.cct> <picoloop bank folder> [--force]\n"
          "\n"
          "A picoloop bank folder is <picoloop storage>/bank/bank<N>, holding\n"
          "dataP<pattern>T<track>.pic files and song.pic.\n"
          "--length N  number of song positions to convert (default: up to the\n"
          "            last position that is not pattern 0 on every track)\n"
          "--force     replace the patterns already in the destination bank\n");
  return 2;
}

static void report(const std::vector<std::string>& messages) {
  for (size_t i = 0; i < messages.size(); i++) printf(i ? "warning: %s\n" : "%s\n", messages[i].c_str());
}

int main(int argc, char** argv) {
  if (argc < 4) return usage();
  std::string command = argv[1], from = argv[2], to = argv[3];
  ConvertOptions options;
  bool force = false;
  for (int i = 4; i < argc; i++) {
    if (!strcmp(argv[i], "--length") && i + 1 < argc) options.songLength = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--title") && i + 1 < argc) options.title = argv[++i];
    else if (!strcmp(argv[i], "--force")) force = true;
    else return usage();
  }

  std::unique_ptr<PicoBank> bank(new PicoBank());
  std::vector<std::string> messages;
  std::string error;

  if (command == "to-cct") {
    if (!picoLoadBank(from, bank.get(), &error) || !cctFromPico(*bank, options, to, &messages, &error)) {
      fprintf(stderr, "error: %s\n", error.c_str());
      return 1;
    }
  } else if (command == "to-picoloop") {
    if (!cctToPico(from, bank.get(), &messages, &error) || !picoSaveBank(to, *bank, force, &error)) {
      fprintf(stderr, "error: %s\n", error.c_str());
      return 1;
    }
  } else {
    return usage();
  }
  report(messages);
  printf("wrote %s\n", to.c_str());
  return 0;
}
