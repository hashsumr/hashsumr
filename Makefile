
UNAME_S	= $(shell uname -s)
CC	= gcc
CFLAGS	= -Wall -g
LDFLAGS	= -pthread -lcrypto -lssl -L./blake3 -lblake3 -L./minibar -lminibar -lm

PROGS	= hashsum
HASHSUM_OBJS	= main.o hashsum.o wrappers-openssl.o wrappers-blake3.o pthread-extra.o

ifeq ($(UNAME_S),Darwin)
OPENSSL3	= $(shell brew --prefix openssl@3)
CFLAGS += -I$(OPENSSL3)/include
LDFLAGS := -L$(OPENSSL3)/lib -Wl,-rpath,$(OPENSSL3)/lib $(LDFLAGS)
endif

all: $(PROGS)

blake3/libblake3.a:
	#git clone https://github.com/BLAKE3-team/BLAKE3.git blake3-src
	mkdir -p blake3-src/c/build
	(cd blake3-src/c/build && cmake .. && make)
	mkdir -p ./blake3
	cp blake3-src/c/blake3.h          ./blake3/
	cp blake3-src/c/build/libblake3.a ./blake3/
	rm -rf blake3-src/c/build

minibar/libminibar.a:
	#git clone https://github.com/chunying/minibar.git minibar-src
	(cd minibar-src && make libminibar.a)
	mkdir ./minibar
	cp minibar-src/minibar.h    ./minibar/
	cp minibar-src/libminibar.a ./minibar/
	make -C minibar-src clean

%.o: %.c
	$(CC) -c $(CFLAGS) $<

hashsum: blake3/libblake3.a minibar/libminibar.a $(HASHSUM_OBJS)
	$(CC) -o $@ $(HASHSUM_OBJS) $(LDFLAGS)

clean:
	rm -f *.o $(PROGS)
	rm -rf ./blake3 ./minibar

