CC = $(CURDIR)/../lib/musl/inst/bin/musl-gcc
CFLAGS = -fdiagnostics-color=never -fno-diagnostics-show-caret \
		 -Wall -Werror -pedantic -ggdb
STATIC = -static

AR = ar

BINARIES = main
SRC_MOD = main.c tg.c wjson.c rjson.c https.c panic.c
OBJ_MOD = $(SRC_MOD:.c=.o)

CURL_INCLUDE = -I../lib/curl/include 
LIBS = ../lib/curl/lib/.libs/libcurl.a \
	   ../lib/nghttp2/lib/.libs/libnghttp2.a \
	   ../lib/c-ares/src/lib/.libs/libcares.a \
	   ../lib/bearssl/dist/lib/libbearssl.a

LIBDEPS = ../lib/curl/lib/.libs/libcurl.a

all:	$(BINARIES)

%.o:	%.c %.h $(CC)
	$(CC) $(CFLAGS) -c $< -o $@

https.o:	https.c https.h $(CC)
	$(CC) $(CFLAGS) $(CURL_INCLUDE) -c $< -o $@

main:	$(OBJ_MOD) $(LIBDEPS) $(CC)
	$(CC) $(CFLAGS) $(STATIC) $(OBJ_MOD) $(LIBS) -o $@

https_demo: https.c https.h panic.o $(LIBDEPS) $(CC)
	$(CC) $(CFLAGS) $(STATIC) https.c $(CURL_INCLUDE) -D SOB_HTTPS_DEMO \
		-o $@ panic.o $(LIBS)

rjson_demo: rjson.c rjson.h panic.o $(CC)
	$(CC) $(CFLAGS) $(STATIC) rjson.c -D SOB_RJSON_DEMO -o $@ panic.o

wjson_demo: wjson.c wjson.h panic.o $(CC)
	$(CC) $(CFLAGS) $(STATIC) wjson.c -D SOB_WJSON_DEMO -o $@ panic.o

rdb_demo: rdb.c rdb.h panic.o $(CC)
	$(CC) $(CFLAGS) $(STATIC) rdb.c -D SOB_RDB_DEMO -o $@ panic.o

wdb_demo: wdb.c wdb.h $(CC)
	$(CC) $(CFLAGS) $(STATIC) wdb.c -D SOB_WDB_DEMO -o $@

tg_demo: tg.c tg.h panic.o https.o rjson.o wjson.o $(LIBDEPS) $(CC)
	$(CC) $(CFLAGS) $(STATIC) tg.c -D SOB_TG_DEMO -o $@ \
		https.o rjson.o wjson.o panic.o $(LIBS)

$(CC):
	$(MAKE) -C ../lib musl/inst/bin/musl-gcc

../lib/curl/lib/.libs/libcurl.a: $(CC)
	$(MAKE) -C ../lib curl/lib/.libs/libcurl.a

deps.mk:	$(CC) $(wildcard *.c)
	$(CC) -MM $(wildcard *.c) > $@

clean:
	rm -rf *.o *~ $(BINARIES) deps.mk https_demo rjson_demo wjson_demo \
		tg_demo rdb_demo wdb_demo

ifneq (clean, $(MAKECMDGOALS))
-include deps.mk
endif

