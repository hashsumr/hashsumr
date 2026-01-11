
UNAME_S	= $(shell uname -s)
CC	= gcc
CFLAGS	= -Wall -g
LDFLAGS	= -lssl -lcrypto -L./blake3 -lblake3 -lm -pthread

PROGS	= hashsumr

HASHSUMR_OBJS	= main.o loadcheck.o hashsumr.o wrappers-openssl.o wrappers-blake3.o

MINIBAR_OBJS	= minibar.o
PTHREAD_COMPAT_OBJS	= pthread_barrier.o pthread_win32.o

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

minibar.o: minibar/minibar.c
	$(CC) -c -o $@ $(CFLAGS) $<

pthread_%.o: minibar/pthread_compat/pthread_%.c
	$(CC) -c -o $@ $(CFLAGS) $<

%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $<

hashsumr: blake3/libblake3.a $(HASHSUMR_OBJS) $(MINIBAR_OBJS) $(PTHREAD_COMPAT_OBJS)
	$(CC) -o $@ $(HASHSUMR_OBJS) $(MINIBAR_OBJS) $(PTHREAD_COMPAT_OBJS) $(LDFLAGS)

# for alpine build
hashsumr-static: blake3/libblake3.a $(HASHSUMR_OBJS) $(MINIBAR_OBJS) $(PTHREAD_COMPAT_OBJS)
	$(CC) -o $@ $(HASHSUMR_OBJS) $(MINIBAR_OBJS) $(PTHREAD_COMPAT_OBJS) $(LDFLAGS) -static-pie

clean:
	rm -f *.o $(PROGS) hashsumr-static
	rm -rf ./blake3

