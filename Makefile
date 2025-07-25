libpackfs.so: packfs.c libarchive/.libs/libarchive.a zlib/libz.a xz/src/liblzma/.libs/liblzma.a
	$(CC) -shared -fPIC -o $@ $^ -DPACKFS_DYNAMIC_LINKING -ldl -Ilibarchive -Ilibarchive/libarchive

libarchive/.libs/libarchive.a: zlib/libz.a xz/src/liblzma/.libs/liblzma.a
	cd libarchive && sh ./build/autogen.sh && LDFLAGS="-L../zlib -L../xz/src/liblzma/.libs" CFLAGS="-I../zlib -I../xz/src/liblzma/ -I../xz/src/liblzma/api" sh configure --without-bz2lib --without-libb2 --without-iconv --without-lz4 --without-zstd --without-cng --without-xml2 --without-expat --without-openssl && $(MAKE)

zlib/libz.a:
	cd zlib && CFLAGS=-fPIC sh ./configure --static && $(MAKE)

xz/src/liblzma/.libs/liblzma.a:
	cd xz && sh ./autogen.sh && CFLAGS=-fPIC sh ./configure --disable-shared && $(MAKE)
