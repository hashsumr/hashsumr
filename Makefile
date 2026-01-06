
UNAME_S	= $(shell uname -s)
CC	= gcc
CFLAGS	= -Wall -g
LDFLAGS	= -lcrypto -lssl -L./blake3 -lblake3 -lm -pthread

PROGS	= hashsum

HASHSUM_OBJS	= main.o loadcheck.o hashsum.o wrappers-openssl.o wrappers-blake3.o

MINIBAR_OBJS	= minibar/minibar.o
PTHREAD_COMPAT_OBJS	= pthread_compat/pthread_barrier.o pthread_compat/pthread_win32.o

ifeq ($(UNAME_S),Darwin)
OPENSSL3	= $(shell brew --prefix openssl@3)
CFLAGS += -I$(OPENSSL3)/include
LDFLAGS := -L$(OPENSSL3)/lib -Wl,-rpath,$(OPENSSL3)/lib $(LDFLAGS)
endif

all: $(PROGS)

blake3/libblake3.a:
	mkdir -p blake3-src/c/build
	(cd blake3-src/c/build && cmake .. && make)
	@-mkdir ./blake3
	cp blake3-src/c/blake3.h          ./blake3/
	cp blake3-src/c/build/libblake3.a ./blake3/
	rm -rf blake3-src/c/build

%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $<

hashsum: blake3/libblake3.a $(HASHSUM_OBJS) $(MINIBAR_OBJS) $(PTHREAD_COMPAT_OBJS)
	$(CC) -o $@ $(HASHSUM_OBJS) $(MINIBAR_OBJS) $(PTHREAD_COMPAT_OBJS) $(LDFLAGS)

clean:
	rm -f *.o minibar/*.o pthread_compat/*.o $(PROGS)
	rm -rf ./blake3

