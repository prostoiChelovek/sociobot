mbedtls/library/libmbedtls.a:
	cd mbedtls && $(MAKE) library/libmbedtls.a

curl/lib/.libs/libcurl.a: mbedtls/library/libmbedtls.a
	cd curl && ./configure -C \
		--disable-docs \
		--disable-shared \
		--disable-pthreads \
		--disable-threaded-resolver \
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
		--without-libidn \
		--without-librtmp \
		--without-libssh2 \
		--without-nss \
		--without-zlib && \
		$(MAKE) -C lib

clean:
	$(MAKE) -C mbedtls/library clean
	$(MAKE) -C curl maintainer-clean || :

