# Native build of the FILTRE engine (no M5 / ESP32 code) + its test suite.
#   make -C tools/host test
ROOT := ../..
SRC  := sampler.cpp deck.cpp sequencer.cpp audio_engine.cpp styles.cpp storage.cpp \
        bounce.cpp wavetable.cpp factory_loop.cpp
OBJ  := $(addprefix build/,$(SRC:.cpp=.o)) build/shim.o build/test_main.o
CXXFLAGS := -std=c++17 -O2 -g -Wall -Wno-misleading-indentation -Wno-parentheses -Ishim -I$(ROOT) -MMD -MP

test: build/filtre_test
	rm -rf build/sd && mkdir -p build/sd && cp -r $(ROOT)/factory-sd/groovebox build/sd/
	cp fixtures/v5.gbx build/sd/groovebox/projects/P7.gbx && cp fixtures/v4.gbx build/sd/groovebox/projects/P8.gbx
	FILTRE_SD=build/sd ./build/filtre_test

build/filtre_test: $(OBJ)
	$(CXX) -o $@ $(OBJ) -lm

build/%.o: $(ROOT)/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@
build/shim.o: shim/shim.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@
build/test_main.o: test_main.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@
build:
	mkdir -p build
clean:
	rm -rf build
# ---- screen previews: the real ui.cpp / lid_ui.cpp drawn with M5GFX sprites ----
GFX   := $(ROOT)/.pio/libdeps/cardputer-adv/M5GFX/src
GFXCC := -O1 -w -DLGFX_LINUX_FB -I$(GFX)
GFXO  := $(addprefix build/gfx/,lgfx_v1.o lgfx_fonts.o lgfx_v1_platforms.o lgfx_v1_panel.o LGFXBase.o)
POBJ  := $(addprefix build/,$(SRC:.cpp=.o)) build/shim.o build/p_ui.o build/p_lid_ui.o build/p_main.o

preview: build/filtre_preview
	rm -rf build/sd build/shots && mkdir -p build/sd build/shots && cp -r $(ROOT)/factory-sd/groovebox build/sd/
	FILTRE_SD=build/sd ./build/filtre_preview
	python3 preview/to_png.py build/shots

build/filtre_preview: $(POBJ) $(GFXO) build/gfx/cfonts.stamp
	$(CXX) -o $@ $(POBJ) $(GFXO) build/gfx/c_*.o -lm
build/gfx/%.o: $(GFX)/lgfx/v1/%.cpp | build
	mkdir -p build/gfx && $(CXX) -std=c++17 $(GFXCC) -c $< -o $@
build/gfx/cfonts.stamp: | build
	mkdir -p build/gfx && for f in $$(find $(GFX)/lgfx/Fonts $(GFX)/lgfx/utility -name '*.c'); do $(CC) $(GFXCC) -c $$f -o build/gfx/c_$$(basename $$f .c).o; done && touch $@
build/p_%.o: $(ROOT)/%.cpp | build
	$(CXX) $(CXXFLAGS) -Ipreview $(GFXCC) -c $< -o $@
build/p_main.o: preview/preview_main.cpp | build
	$(CXX) $(CXXFLAGS) -Ipreview $(GFXCC) -c $< -o $@

-include $(OBJ:.o=.d) $(POBJ:.o=.d)
.PHONY: test preview clean
