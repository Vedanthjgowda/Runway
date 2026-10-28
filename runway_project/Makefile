CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Isrc
SRC      := src/scheduler.cpp src/scenario.cpp src/report.cpp

all: runway_sim run_tests

runway_sim: $(SRC) src/main.cpp $(wildcard src/*.h)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) src/main.cpp

run_tests: $(SRC) tests/test_main.cpp $(wildcard src/*.h)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) tests/test_main.cpp

test: run_tests
	./run_tests

demo: runway_sim
	./runway_sim --scenario data/sample.txt

clean:
	rm -f runway_sim run_tests
