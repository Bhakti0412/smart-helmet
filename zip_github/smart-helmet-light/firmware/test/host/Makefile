# Host-side tests for the crash / hard-brake logic (no ESP32 needed).
#   make test                       -> synthetic scenarios
#   make replay                     -> replays ../../../sample-data/*.csv
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

test_detectors: test_detectors.cpp ../../src/detectors.h ../../src/config.h
	$(CXX) $(CXXFLAGS) -o $@ test_detectors.cpp

test: test_detectors
	./test_detectors

replay: test_detectors
	./test_detectors --replay ../../../sample-data/imu_crash_trace.csv
	./test_detectors --replay ../../../sample-data/imu_crash_trace.csv --cancel-after 4000
	./test_detectors --replay ../../../sample-data/imu_hard_brake.csv
	./test_detectors --replay ../../../sample-data/imu_pothole_false_alarm.csv

clean:
	rm -f test_detectors
