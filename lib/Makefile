c-ares/src/lib/.libs/libcares.a:
	cd c-ares && ./configure -C \
		--disable-cares-threads \
		--disable-tests \
		--enable-static \
		--disable-shared && \
	$(MAKE) -C src/lib

nghttp2/lib/.libs/libnghttp2.a:
	cd nghttp2 && ./configure -C \
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

mbedtls/library/libmbedtls.a:
	cd mbedtls && $(MAKE) library/libmbedtls.a

curl/lib/.libs/libcurl.a: mbedtls/library/libmbedtls.a \
		nghttp2/lib/.libs/libnghttp2.a \
		c-ares/src/lib/.libs/libcares.a
	cd curl && ./configure -C \
		--disable-docs \
		--disable-shared \
		--disable-pthreads \
		--disable-threaded-resolver \
		--enable-ares \
		--without-ssl \
		--with-mbedtls=$(CURDIR)/mbedtls/ \
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
	$(MAKE) -C mbedtls/library clean
	$(MAKE) -C nghttp2 maintainer-clean
	$(MAKE) -C curl maintainer-clean || :

