# Native build of the FILTRE engine (no M5 / ESP32 code) + its test suite.
#   make -C tools/host test
ROOT := ../..
SRC  := sampler.cpp deck.cpp sequencer.cpp audio_engine.cpp styles.cpp storage.cpp \
        bounce.cpp wavetable.cpp factory_loop.cpp
OBJ  := $(addprefix build/,$(SRC:.cpp=.o)) build/shim.o build/test_main.o
CXXFLAGS := -std=c++17 -O2 -g -Wall -Wno-misleading-indentation -Wno-parentheses -Ishim -I$(ROOT) -MMD -MP

test: build/filtre_test
	rm -rf build/sd && mkdir -p build/sd && cp -r $(ROOT)/factory-sd/groovebox build/sd/
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
-include $(OBJ:.o=.d)
.PHONY: test clean
