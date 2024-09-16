libarchivepreload.so: libarchivepreload.c libarchive/.libs/libarchive.a
	$(CC) -shared -fPIC $< -o $@ -ldl libarchive/.libs/libarchive.a -Llibarchive/.libs -Ilibarchive -Ilibarchive/libarchive #-DPACKFS_LOG

libarchive/.libs/libarchive.a:
	cd libarchive && sh build/autogen.sh && sh configure --without-zlib --without-bz2lib  --without-libb2 --without-iconv --without-lz4  --without-zstd --without-lzma --without-cng  --without-xml2 --without-expat --without-openssl && $(MAKE)
