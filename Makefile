CXX = g++
CC = gcc
CPPOBJS = src/main.o src/track.o network/network.o SGP4/libsgp4/CoordGeodetic.o SGP4/libsgp4/CoordTopocentric.o SGP4/libsgp4/DateTime.o SGP4/libsgp4/DecayedException.o SGP4/libsgp4/Eci.o SGP4/libsgp4/Globals.o SGP4/libsgp4/Observer.o SGP4/libsgp4/OrbitalElements.o SGP4/libsgp4/SatelliteException.o SGP4/libsgp4/SGP4.o SGP4/libsgp4/SolarPosition.o SGP4/libsgp4/TimeSpan.o SGP4/libsgp4/Tle.o SGP4/libsgp4/TleException.o SGP4/libsgp4/Util.o SGP4/libsgp4/Vector.o
COBJS = gpiodev/gpiodev.o
EDCFLAGS := -std=gnu11 -O2 $(CFLAGS)
EDCXXFLAGS = -I ./ -I ./include/ -I ./network/ -I ./SGP4/libsgp4/ -I ./SGP4/passpredict/ -I ./SGP4/sattrack/ -Wall -pthread -DGSNID=\"track\" $(CXXFLAGS)
EDLDFLAGS := -lpthread -lm $(LDFLAGS)
TARGET = track.out

all: $(COBJS) $(CPPOBJS)
	$(CXX) $(EDCXXFLAGS) $(COBJS) $(CPPOBJS) -o $(TARGET) $(EDLDFLAGS)
	sudo ./$(TARGET)

%.o: %.cpp
	$(CXX) $(EDCXXFLAGS) -o $@ -c $<

%.o: %.c
	$(CC) $(EDCFLAGS) -o $@ -c $<

.PHONY: clean

clean:
	$(RM) *.out
	$(RM) *.o
	$(RM) src/*.o
	$(RM) network/*.o
