.NOPARALLEL: curl/lib/.libs/libcurl.a

bearssl/dist/lib/libbearssl.a:
	$(MAKE) -C bearssl
	cd bearssl && \
		( rm -r dist/ 2>/dev/null || : ) && \
		mkdir -p dist/lib/ dist/include/ && \
		cp build/libbearssl.a dist/lib  && \
		cp inc/bearssl*.h dist/include

c-ares/src/lib/.libs/libcares.a:
	cd c-ares && ./configure \
		--disable-cares-threads \
		--disable-tests \
		--enable-static \
		--disable-shared && \
	$(MAKE) -C src/lib

nghttp2/lib/.libs/libnghttp2.a:
	cd nghttp2 && ./configure \
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
		--with-libbrotlidec=no && \
	$(MAKE) -C lib

curl/lib/.libs/libcurl.a: nghttp2/lib/.libs/libnghttp2.a \
		c-ares/src/lib/.libs/libcares.a \
		bearssl/dist/lib/libbearssl.a
	cd curl && ./configure \
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
		--without-zlib && \
		$(MAKE) -C lib

clean:
	$(MAKE) -C nghttp2 maintainer-clean
	$(MAKE) -C curl maintainer-clean || :
	$(MAKE) -C bearssl clean && rm -r bearssl/dist/

