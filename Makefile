
UNAME_S	= $(shell uname -s)
CC	= gcc
CFLAGS	= -Wall -g
LDFLAGS	= -lcrypto -lssl -L./blake3 -lblake3 -L./minibar -lminibar -L./pthread_compat -lpthread_compat -lm -pthread

PROGS	= hashsum
HASHSUM_OBJS	= main.o loadcheck.o hashsum.o wrappers-openssl.o wrappers-blake3.o

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

minibar/libminibar.a:
	make -C minibar-src libminibar.a
	@-mkdir ./minibar
	cp minibar-src/minibar.h    ./minibar/
	cp minibar-src/libminibar.a ./minibar/
	make -C minibar-src clean

pthread_compat/libpthread_compat.a:
	make -C pthread_compat_src libpthread_compat.a
	@-mkdir pthread_compat
	cp pthread_compat_src/*.h ./pthread_compat/
	cp pthread_compat_src/*.a ./pthread_compat/
	make -C pthread_compat_src clean

%.o: %.c
	$(CC) -c $(CFLAGS) $<

hashsum: blake3/libblake3.a minibar/libminibar.a pthread_compat/libpthread_compat.a $(HASHSUM_OBJS)
	$(CC) -o $@ $(HASHSUM_OBJS) $(LDFLAGS)

clean:
	rm -f *.o $(PROGS)
	rm -rf ./blake3 ./minibar ./pthread_compat

