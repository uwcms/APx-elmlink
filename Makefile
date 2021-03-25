CFLAGS := -std=c++11

all: build

build: elmlinkd elmlink-lowlevel-send

elmlinkd: elmlinkd.o elmlink_protocol.o crc32.o base64.o tty_noncanonical.o
	$(CXX) $(CFLAGS) -o $@ $^

elmlink-lowlevel-send: elmlink-lowlevel-send.o elmlink_protocol.o crc32.o base64.o tty_noncanonical.o
	$(CXX) $(CFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CFLAGS) -c -o $@ $^

clean:
	rm -f *.o elmlinkd elmlink-lowlevel-send *.rpm
