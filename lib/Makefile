.NOPARALLEL: curl/lib/.libs/libcurl.a

CC = musl/inst/bin/musl-gcc

$(CC):
	cd musl && \
		./configure \
			--disable-shared \
			--enable-optimize \
			--prefix=$(CURDIR)/musl/inst
	$(MAKE) -C musl
	$(MAKE) -C musl install

bearssl/dist/lib/libbearssl.a: $(CC)
	$(MAKE) CC=$(CURDIR)/$(CC) -C bearssl
	cd bearssl && \
		( rm -r dist/ 2>/dev/null || : ) && \
		mkdir -p dist/lib/ dist/include/ && \
		cp build/libbearssl.a dist/lib  && \
		cp inc/bearssl*.h dist/include

c-ares/src/lib/.libs/libcares.a: $(CC)
	cd c-ares && CC=$(CURDIR)/$(CC) ./configure \
		--disable-cares-threads \
		--disable-tests \
		--enable-static \
		--disable-shared
	$(MAKE) CC=$(CURDIR)/$(CC) -C c-ares/src/lib

nghttp2/lib/.libs/libnghttp2.a: $(CC)
	cd nghttp2 && CC=$(CURDIR)/$(CC) ./configure \
		--disable-shared \
		--enable-static \
		--disable-threads \
		--enable-lib-only \
		--with-libxml2=no \
		--with-jansson=no \
		--with-zlib=no \
		--with-libevent-openssl=no \
		--with-libcares=no \
		--with-openssl=no \
		--with-libev=no \
		--with-jemalloc=no \
		--with-systemd=no \
		--with-mruby=no \
		--with-neverbleed=no \
		--with-libngtcp2=no \
		--with-libnghttp3=no \
		--with-libbpf=no \
		--with-libbrotlienc=no \
		--with-libbrotlidec=no
	$(MAKE) CC=$(CURDIR)/$(CC) -C nghttp2/lib

curl/lib/.libs/libcurl.a: $(CC) \
		nghttp2/lib/.libs/libnghttp2.a \
		c-ares/src/lib/.libs/libcares.a \
		bearssl/dist/lib/libbearssl.a
	cd curl && CC=$(CURDIR)/$(CC) ./configure \
		--with-bearssl=$(CURDIR)/bearssl/dist/ \
		--disable-docs \
		--disable-shared \
		--disable-pthreads \
		--disable-threaded-resolver \
		--enable-ares \
		--disable-dict \
		--disable-ftp \
		--disable-imap \
		--disable-ldap \
		--disable-ldaps \
		--disable-pop3 \
		--disable-proxy \
		--disable-rtsp \
		--disable-shared \
		--disable-smtp \
		--disable-telnet \
		--disable-tftp \
		--disable-zlib \
		--without-gnutls \
		--without-librtmp \
		--without-libssh2 \
		--without-nss \
		--without-libidn \
		--without-libidn2 \
		--without-libpsl \
		--without-zstd \
		--without-brotli \
		--without-zlib
	$(MAKE) CC=$(CURDIR)/$(CC) -C curl/lib

clean:
	$(MAKE) -C musl clean && rm -r musl/inst/
	$(MAKE) -C nghttp2 maintainer-clean
	$(MAKE) -C bearssl clean && rm -r bearssl/dist/
	$(MAKE) -C curl maintainer-clean || :

