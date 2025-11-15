# License
MIT

# libarchivepreload
Demo of abusing https://github.com/libarchive/libarchive to make `LD_PRELOAD`-based overrides of file-related functions. The `LD_PRELOAD`-based approach is useful when one doesn't have FUSE kernel module installed or does not have root permissions do use https://github.com/google/fuse-archive/ and it's wasteful to decompress a given archive.

**Limitations:** This demo does not optimize for iterative entry reads or iterative seeks, https://github.com/google/fuse-archive/ makes an attempt in that direction; also see https://github.com/libarchive/libarchive/issues/2306 for future support of fast seeks in ZIP / TAR / CPIO in libarchive

**Note:** for now only single-threaded support

# Syntax of PACKFS
`PACKFS` is a colon-separated list of parts:
- paths to directories, ending with a trailing `/`
- paths to files: archives or `.json`-listings

Each part can have format:
- `path`
- `path@mountpoint`
- `path@mountpoint@listingresolutiondir` - this is useful if listings are built-in, and the resolution-dir is remote, this allows to not scan the `listingresolutiondir`'s archives when not needed

If `listingresolutiondir` is not empty, `mountpoint` can be empty

## Examples:
- `/myarchivesdir@/packfsarchives`
- `/mylistingsdir@/packfslistings@/myarchivesdir`
- `PACKFS_CONFIG=texlive.iso:/packfs/archive/@/packfs/texlive-archive/` (mount ISO file to /packfs/, and mount all package archives as TDS without installation) ?
- zip static-linked in the binary (two cases: compressed, uncompressed and maybe even just appended?), e.g. `PACKFS_CONFIG=/packfs/my.zip`
- can support something json static-linked in the binary like `PACKFS_CONFIG=/packfs/listings/@/mnt/packfs/@/mnt/http/`?
- maybe a way to pack-in packfs0.zip packfs0.zip.json


# Overridden libc / posix functions
- [`open`](https://man7.org/linux/man-pages/man2/open.2.html)
- [`openat`](https://man7.org/linux/man-pages/man2/openat.2.html)
- [`close`](https://man7.org/linux/man-pages/man2/close.2.html)
- [`read`](https://man7.org/linux/man-pages/man2/read.2.html)
- [`access`](https://man7.org/linux/man-pages/man2/access.2.html)
- [`lseek`](https://man7.org/linux/man-pages/man2/lseek.2.html)
- [`stat`](https://man7.org/linux/man-pages/man2/stat.2.html)
- [`fstat`](https://man7.org/linux/man-pages/man2/fstat.2.html)
- [`fstatat`](https://man7.org/linux/man-pages/man2/fstatat.2.html)
- [`statx`](https://man7.org/linux/man-pages/man2/statx.2.html)
- [`fopen`](https://en.cppreference.com/w/c/io/fopen)
- [`fclose`](https://en.cppreference.com/w/c/io/fclose)
- [`fileno`](https://man7.org/linux/man-pages/man3/fileno.3.html)
- [`fcntl`](https://man7.org/linux/man-pages/man2/fcntl.2.html) - only `F_DUPFD` and `F_DUPFD_CLOEXEC` is emulated
- [`opendir`](https://man7.org/linux/man-pages/man3/opendir.3.html)
- [`fdopendir`](https://man7.org/linux/man-pages/man3/fdopendir.3p.html)
- [`readdir`](https://man7.org/linux/man-pages/man3/readdir.3.html)
- [`closedir`](https://man7.org/linux/man-pages/man3/closedir.3.html)

# Need to override 64-bit functions for python
```

sure:
stat64
fstat64
open64
lstat64

suresure:
nm -D -u $(which python) | grep '64@'
U fcntl64@GLIBC_2.28
U fopen64@GLIBC_2.2.5
U fstat64@GLIBC_2.33
U fstatat64@GLIBC_2.33
U fstatvfs64@GLIBC_2.2.5
U ftruncate64@GLIBC_2.2.5
U getrlimit64@GLIBC_2.2.5
U lockf64@GLIBC_2.2.5
U lseek64@GLIBC_2.2.5
U lstat64@GLIBC_2.33
U mmap64@GLIBC_2.2.5
U open64@GLIBC_2.2.5
U openat64@GLIBC_2.4
U posix_fadvise64@GLIBC_2.2.5
U posix_fallocate64@GLIBC_2.2.5
U pread64@GLIBC_2.2.5
U pwrite64@GLIBC_2.2.5
U readdir64@GLIBC_2.2.5
U sendfile64@GLIBC_2.3
U setrlimit64@GLIBC_2.2.5
U stat64@GLIBC_2.33
U statvfs64@GLIBC_2.2.5
U truncate64@GLIBC_2.2.5

all:
int stat64 (const char *filename, struct stat64 *buf)
int fstat64 (int filedes, struct stat64 *buf)
int lstat64 (const char *filename, struct stat64 *buf)
void * mmap64 (void *address, size_t length, int protect, int flags, int filedes, off64_t offset)
struct dirent64 * readdir64 (DIR *dirstream)
int posix_fallocate64 (int fd, off64_t offset, off64_t length)
int open64 (const char *filename, int flags[, mode_t mode])
int getrlimit64 (int resource, struct rlimit64 *rlp)
int setrlimit64 (int resource, const struct rlimit64 *rlp)
int aio_read64 (struct aiocb64 *aiocbp)
int aio_write64 (struct aiocb64 *aiocbp)
int lio_listio64 (int mode, struct aiocb64 *const list[], int nent, struct sigevent *sig)
void globfree64 (glob64_t *pglob)
off64_t lseek64 (int filedes, off64_t offset, int whence)
ssize_t pread64 (int filedes, void *buffer, size_t size, off64_t offset)
ssize_t pwrite64 (int filedes, const void *buffer, size_t size, off64_t offset)
int truncate64 (const char *name, off64_t length)
int ftruncate64 (int id, off64_t length)
int ftw64 (const char *filename, __ftw64_func_t func, int descriptors)
int nftw64 (const char *filename, __nftw64_func_t func, int descriptors, int flag)
off64_t ftello64 (FILE *stream)
int fseeko64 (FILE *stream, off64_t offset, int whence)
int aio_error64 (const struct aiocb64 *aiocbp)
ssize_t aio_return64 (struct aiocb64 *aiocbp)
int aio_cancel64 (int fildes, struct aiocb64 *aiocbp)
FILE * fopen64 (const char *filename, const char *opentype)
FILE * freopen64 (const char *filename, const char *opentype, FILE *stream)
int scandir64 (const char *dir, struct dirent64 ***namelist, int (*selector) (const struct dirent64 *), int (*cmp) (const struct dirent64 **, const struct dirent64 **))
int alphasort64 (const struct dirent64 **a, const struct dirent **b)
int versionsort64 (const struct dirent64 **a, const struct dirent64 **b)
int fgetpos64 (FILE *stream, fpos64_t *position)
int fsetpos64 (FILE *stream, const fpos64_t *position)
FILE * tmpfile64 (void)
int glob64 (const char *pattern, int flags, int (*errfunc) (const char *filename, int error-code), glob64_t *vector-ptr)
int aio_fsync64 (int op, struct aiocb64 *aiocbp)
int aio_suspend64 (const struct aiocb64 *const list[], int nent, const struct timespec *timeout)
```

# References
- https://mropert.github.io/2018/02/02/pic_pie_sanitizers/
- https://github.com/google/fuse-archive/
- https://gist.github.com/vadimkantorov/2a4e092889b7132acd3b7ddfc2f2f907
- https://github.com/libarchive/libarchive/issues/2306
- https://github.com/coreutils/coreutils/blob/master/src/ls.c
- https://github.com/coreutils/coreutils/blob/master/src/cat.c
- https://git.busybox.net/busybox/tree/coreutils/ls.c
- https://git.busybox.net/busybox/tree/coreutils/cat.c
- https://git.musl-libc.org/cgit/musl/tree/src/dirent/opendir.c
- https://git.musl-libc.org/cgit/musl/tree/src/dirent/readdir.c

- https://stackoverflow.com/questions/77100299/implementing-a-simple-version-of-readdir-with-system-call
- https://stackoverflow.com/questions/74890409/what-happens-in-c-function-readdir-from-dirent-h
- https://stackoverflow.com/questions/24103009/is-it-possible-to-implement-readdir-in-ubuntu-12-10-kernel-3-5
- https://unix.stackexchange.com/questions/625899/correctly-implementing-seeking-in-fuse-readdir-operation
- https://github.com/facebook/sapling/blob/5cc682e8ff24ef182be2dbe07e484396539e80f4/eden/fs/inodes/TreeInode.cpp#L1798-L1833
- https://github.com/facebook/sapling/blob/main/eden/fs/docs/InodeLifetime.md
- https://lwn.net/Articles/544520/
- https://lwn.net/Articles/544298/
- https://github.com/TheAssassin/AppImageLauncher/issues/361
- https://github.com/sholtrop/ldpfuse/
- https://github.com/google/fuse-archive/blob/main/src/main.cc
- https://superuser.com/questions/1601311/fuse-fs-without-root-privileges-e-g-a-ld-preload-gateway-or-a-proot-plugin
- https://theses.liacs.nl/pdf/2021-2022-HoltropS.pdf
- https://github.com/fritzw/ld-preload-open
- https://github.com/strace/strace/blob/8399328f628182cf236a1c39a074d601babdeaa4/src/fcntl.c#L102
- https://git.savannah.gnu.org/gitweb/?p=gnulib.git;a=blob;f=lib/fcntl.c;h=7cd3a0f976d9f9c9fbb1b5cab5b57780a72cec58;hb=HEAD
- https://savannah.gnu.org/bugs/?48169
- https://github.com/coreutils/gnulib/blob/master/lib/fcntl.c
- https://github.com/coreutils/gnulib/blob/master/lib/cloexec.c
- https://git.savannah.gnu.org/gitweb/?p=gnulib.git;a=blob;f=lib/fcntl.c
- https://git.savannah.gnu.org/gitweb/?p=gnulib.git;a=blob;f=lib/cloexec.c
- https://man7.org/linux/man-pages/man2/fcntl.2.html
- https://github.com/emscripten-core/emscripten/blob/main/tools/file_packager.py
- https://github.com/6over3/zeroperl/blob/main/tools/sfs.js
