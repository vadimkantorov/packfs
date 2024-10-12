libarchivepreload.so: libarchivepreload.c libarchive/.libs/libarchive.a zlib/libz.a
	$(CC) -shared -fPIC $< -o $@ -ldl libarchive/.libs/libarchive.a zlib/libz.a -Ilibarchive -Ilibarchive/libarchive -DPACKFS_LOG 

libarchive/.libs/libarchive.a:
	cd libarchive && sh ./build/autogen.sh && sh configure --without-bz2lib --without-libb2 --without-iconv --without-lz4  --without-zstd --without-lzma --without-cng  --without-xml2 --without-expat --without-openssl && $(MAKE)

zlib/libz.a:
	cd zlib && CFLAGS=-fPIC sh ./configure --static && $(MAKE)
