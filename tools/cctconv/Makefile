# picoloop <-> ChooChooTracker (.cct) converter.
#
# Reuses the real file I/O of both projects instead of re-implementing it:
#   - picoloop side: PatternReader / Pattern / PatternElement / SongSequencer
#     from ../../picoloop (compiled against a tiny SDL stub, no SDL needed)
#   - ChooChooTracker side: chipnomad_lib (projectLoad / projectSave) from a
#     ChooChooTracker checkout given by CCT_DIR.
#
#   make CCT_DIR=/path/to/Choochootracker -j8
#
# The two sides are compiled in separate translation units: picoloop's
# Master.h and chipnomad_lib define overlapping macros (TRACK_MAX, NOTE_OFF,
# DEFAULT_VOLUME...), they only meet through the neutral pico_song.h.

CCT_DIR ?= ../../../Choochootracker
PICO_DIR = ../../picoloop
CXX ?= g++
BUILD = build
OPT ?= -O1

CNL = $(CCT_DIR)/chipnomad_lib
CNL_INCLUDES = -I$(CNL) -I$(CNL)/chips -I$(CNL)/link -I$(CNL)/external/ayumi \
  -I$(CNL)/external/gb_apu -I$(CNL)/external/mutable -I$(CNL)/external/open303/Source/DSPCode \
  -I$(CNL)/external/signalsmith -I$(CCT_DIR)/tracker/src/import -I$(CCT_DIR)/tracker/src -I$(CCT_DIR)/tracker/src/corelib
CNL_SOURCES = $(wildcard $(CNL)/external/mutable/beads/*.cc) $(wildcard $(CNL)/external/mutable/beads/dsp/*.cc) \
  $(wildcard $(CNL)/external/msfa/*.cc) $(CNL)/external/emu76489/emu76489.c $(CNL)/external/gb_apu/gb_apu.c \
  $(wildcard $(CNL)/external/gb_apu/blargg/*.cpp) $(wildcard $(CNL)/external/ymfm/*.cpp) \
  $(wildcard $(CNL)/*.cpp) $(wildcard $(CNL)/chips/*.cpp) $(wildcard $(CNL)/synth/*.cpp) \
  $(wildcard $(CNL)/export/*.cpp) $(wildcard $(CNL)/midi/*.cpp) $(wildcard $(CNL)/link/*.cpp) \
  $(wildcard $(CNL)/external/open303/Source/DSPCode/*.cpp) $(wildcard $(CNL)/external/ayumi/*.cpp) \
  $(wildcard $(CNL)/external/mutable/braids/*.cc) $(wildcard $(CNL)/external/mutable/stmlib/utils/*.cc) \
  $(wildcard $(CNL)/external/mutable/stmlib/dsp/*.cc) \
  $(wildcard $(CNL)/external/mutable/plaits/*.cc) $(wildcard $(CNL)/external/mutable/plaits/dsp/*.cc) \
  $(wildcard $(CNL)/external/mutable/plaits/dsp/*/*.cc) \
  $(CNL)/external/mutable/plaits_alt/resources.cc $(wildcard $(CNL)/external/mutable/plaits_alt/dsp/*.cc) \
  $(wildcard $(CNL)/external/mutable/plaits_alt/dsp/*/*.cc) \
  $(CCT_DIR)/tracker/src/import/import_common.cpp $(CCT_DIR)/tracker/src/project_utils.cpp \
  $(CCT_DIR)/tracker/src/pitch_table_utils.cpp
CNL_OBJECTS = $(patsubst $(CCT_DIR)/%,$(BUILD)/cct/%.o,$(CNL_SOURCES))

PICO_SOURCES = $(PICO_DIR)/Pattern.cpp $(PICO_DIR)/PatternElement.cpp $(PICO_DIR)/PatternReader.cpp $(PICO_DIR)/SongSequencer.cpp
PICO_OBJECTS = $(patsubst $(PICO_DIR)/%.cpp,$(BUILD)/pico/%.o,$(PICO_SOURCES))
PICO_FLAGS = -std=c++11 -w -D__SDL20__ -DPC_DESKTOP -Istub -I$(PICO_DIR)

TOOL = picoloop2cct

all: $(TOOL)

$(TOOL): $(BUILD)/main.o $(BUILD)/pico_side.o $(BUILD)/cct_side.o $(PICO_OBJECTS) $(BUILD)/libchipnomad.a
	$(CXX) -o $@ $^ -lm -pthread

$(BUILD)/libchipnomad.a: $(CNL_OBJECTS)
	rm -f $@; ar rcs $@ $^

$(BUILD)/cct/%.o: $(CCT_DIR)/%
	@mkdir -p $(dir $@)
	$(CXX) -x $(if $(filter %.c,$<),c,c++) $(if $(filter %.c,$<),,-std=c++17) $(OPT) -w -DTEST $(CNL_INCLUDES) -c $< -o $@

$(BUILD)/pico/%.o: $(PICO_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(OPT) $(PICO_FLAGS) -c $< -o $@

$(BUILD)/pico_side.o: pico_side.cpp pico_song.h
	@mkdir -p $(BUILD)
	$(CXX) $(OPT) $(PICO_FLAGS) -c $< -o $@

$(BUILD)/cct_side.o: cct_side.cpp pico_song.h
	@mkdir -p $(BUILD)
	$(CXX) -std=c++17 $(OPT) -Wall -DTEST $(CNL_INCLUDES) -c $< -o $@

$(BUILD)/main.o: main.cpp pico_song.h
	@mkdir -p $(BUILD)
	$(CXX) -std=c++17 $(OPT) -Wall -c $< -o $@

clean:
	rm -rf $(BUILD) $(TOOL)

.PHONY: all clean
