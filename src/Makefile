CXX = g++
CXXFLAGS = -std=c++17 -pthread $(shell pkg-config --cflags opencv4)
LDFLAGS = $(shell pkg-config --libs opencv4) -lwiringPi

all: dashcam watchdog

dashcam: dashcam.cpp
	$(CXX) $(CXXFLAGS) -o dashcam dashcam.cpp $(LDFLAGS)

watchdog: watchdog.cpp
	$(CXX) $(CXXFLAGS) -o watchdog watchdog.cpp $(LDFLAGS)

clean:
	rm -f dashcam watchdog
