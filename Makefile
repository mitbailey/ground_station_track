CXX = g++
CPPOBJS = src/main.o src/track.o driver/SGP4/Dish.o driver/SGP4/Satellite.o driver/SGP4/CoordGeodetic.o driver/SGP4/CoordTopocentric.o driver/SGP4/Observer.o driver/SGP4/SGP4.o
COBJS = 
CXXFLAGS = -I ./ -I ./include/ -I ./drivers/SGP4/libsgp4/ -Wall -pthread
EDLDFLAGS :=
TARGET = track.out

all: $(COBJS) $(CPPOBJS)
	$(CXX) $(CXXFLAGS) $(COBJS) $(CPPOBJS) -o $(TARGET) $(EDLDFLAGS)
	sudo ./$(TARGET)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

%.o: %.c
	$(CXX) $(CXXFLAGS) -o $@ -c $<

.PHONY: clean

clean:
	$(RM) *.out
	$(RM) *.o
	$(RM) src/*.o
	$(RM) network/*.o