CXX=gcc
LD=gcc
CXXFLAGS=`pkg-config uhd libosmodsp --cflags` -Wall -O3 -ffast-math -march=native -pipe -ggdb -pthread -std=gnu++11
LDLIBS=`pkg-config uhd libosmodsp --libs` -ggdb -lstdc++ -pthread

pinger: pinger.o

pinger-old: pinger-old.o

clean:
	rm -f pinger pinger-old *.o

