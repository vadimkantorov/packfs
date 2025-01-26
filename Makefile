libarchivepreload.so: libarchivepreload.c libarchive/.libs/libarchive.a zlib/libz.a xz/src/liblzma/.libs/liblzma.a
	$(CC) -shared -fPIC $< -o $@ -ldl libarchive/.libs/libarchive.a zlib/libz.a xz/src/liblzma/.libs/liblzma.a -Ilibarchive -Ilibarchive/libarchive && nm $@

libarchive/.libs/libarchive.a:
	cd libarchive && sh ./build/autogen.sh && sh configure --without-bz2lib --without-libb2 --without-iconv --without-lz4  --without-zstd --without-cng  --without-xml2 --without-expat --without-openssl && $(MAKE)

zlib/libz.a:
	cd zlib && CFLAGS=-fPIC sh ./configure --static && $(MAKE) && nm $@

xz/src/liblzma/.libs/liblzma.a:
	cd xz && sh ./autogen.sh && CFLAGS=-fPIC sh ./configure --disable-shared && $(MAKE) && nm $@
