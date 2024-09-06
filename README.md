# libarchivepreload
Demo of abusing https://github.com/libarchive/libarchive to make `LD_PRELOAD`-based overrides of file-related functions

```shell
cc -shared -fPIC libarchivepreload.c -o libarchivepreload.so -ldl

zip libarchivepreload.zip libarchivepreload.c

LD_PRELOAD=$PWD/libarchivepreload.so LIBARCHIVEPRELOAD=libarchivepreload.zip /usr/bin/ls
LD_PRELOAD=$PWD/libarchivepreload.so /usr/bin/ls libarchivepreload.zip

LD_PRELOAD=$PWD/libarchivepreload.so LIBARCHIVEPRELOAD=libarchivepreload.zip /usr/bin/cat libarchivepreload.c
LD_PRELOAD=$PWD/libarchivepreload.so /usr/bin/cat libarchivepreload.zip/libarchivepreload.c
```

# References
- https://github.com/google/fuse-archive/
- https://gist.github.com/vadimkantorov/2a4e092889b7132acd3b7ddfc2f2f907
