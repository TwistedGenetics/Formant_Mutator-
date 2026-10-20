# Offline test harness for FORMANT MUTATOR (desktop build, any C++11 compiler).
# Usage (from repo root):  make -C test run        # full run, 600 s soak
#                          make -C test quick      # 60 s soak
CXX ?= g++
CXXFLAGS ?= -O2 -std=c++14 -Wall -Wextra -ffp-contract=off

harness: harness.cc ../formant_mutator/formant_engine.h ../formant_mutator/fm_params.h
	$(CXX) $(CXXFLAGS) -o $@ harness.cc -lm

run: harness
	mkdir -p out && ./harness out 600

quick: harness
	mkdir -p out && ./harness out 60

clean:
	rm -rf harness out
