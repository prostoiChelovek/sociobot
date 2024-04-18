CC = $(CURDIR)/../lib/musl/inst/bin/musl-gcc
CFLAGS = -fdiagnostics-color=never -fno-diagnostics-show-caret -Wall -Werror -pedantic -ggdb -I../lib
STATIC = -static

AR = ar

BINARIES = main
SRC_MOD = main.c tg.c ev.c
OBJ_MOD = $(SRC_MOD:.c=.o)

LIBS = -I../lib/curl/include \
	   ../lib/curl/lib/.libs/libcurl.a \
	   ../lib/nghttp2/lib/.libs/libnghttp2.a \
	   ../lib/c-ares/src/lib/.libs/libcares.a \
	   ../lib/bearssl/dist/lib/libbearssl.a

LIBDEPS = ../lib/curl/lib/.libs/libcurl.a

all:	$(BINARIES)

%.o:	$(CC) %.c
	$(CC) $(CFLAGS) -c $< -o $@

main:	$(CC) $(OBJ_MOD) $(LIBDEPS)
	$(CC) $(CFLAGS) $(STATIC) $(OBJ_MOD) $(LIBS) -o $@

https_demo: https.c $(LIBDEPS) $(CC)
	$(CC) $(CFLAGS) $(STATIC) https.c $(LIBS) -D SOB_HTTPS_DEMO -o $@

$(CC):
	$(MAKE) -C ../lib musl/inst/bin/musl-gcc

../lib/curl/lib/.libs/libcurl.a: $(CC)
	$(MAKE) -C ../lib curl/lib/.libs/libcurl.a

deps.mk:	$(CC) $(wildcard *.c)
	$(CC) -MM $(wildcard *.c) > $@

clean:
	rm -rf *.o *~ $(BINARIES) deps.mk https_demo

ifneq (clean, $(MAKECMDGOALS))
-include deps.mk
endif

