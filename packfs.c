// TODO: where do path normalization? add comment for every function on path string constraints
// TODO: use safe string functions everywhere
// TODO: prevent re-entrant packfs_init: happens when reading archive mentioned in PACKFS_CONFIG
// TODO: can support doing something like PACKFS_CONFIG=texlive.iso:/packfs/archive/@/packfs/texlive-archive/ ?
// TODO: support working with zip static-linked in the binary (two cases: compressed, uncompressed and maybe even just appended?), e.g. PACKFS_CONFIG=/packfs/my.zip
// TODO: can support something json static-linked in the binary like PACKFS_CONFIG=/packfs/listings/@/mnt/packfs/@/mnt/http/ )

// XXX: dir_entry->d_ino is abused to mean shifted index in dynamic/static, then in corresponding dir-list, then in file-list
// XXX: dir_entry->d_off is abused to mean shifted offset in dynamic dirpath (NULL-terminated strings which are concatenated)

// TODO: counters _num or _len for concatenated path lists are not needed?
// TODO: support reading from decompressed zip/tar-entries by offsets
// TODO: support mmap-reads from uncompressed archives
// TODO: support Emscripten-like concatenated list of lzma-encoded files
// TODO: check and handle various limits. if dir or prefix is considered without trailing slash - specify in varname, check path lens fit in packfs limit, check d_name length: https://unix.stackexchange.com/questions/619625/to-what-extent-does-linux-support-file-names-longer-than-255-bytes

// TODO: maybe use #include <stdbool.h> / bool / true / false - write wrapper in ctypes
// TODO: report error via a global errno / geterrno string
// TODO: unify SPLIT and ITER_PATH

#ifdef PACKFS_ARCHIVE
#ifndef PACKFS_ARCHIVEREADSUPPORTEXT
#define PACKFS_ARCHIVEREADSUPPORTEXT .iso:.zip:.tar:.tar.gz:.tar.xz
#endif
#ifndef PACKFS_ARCHIVEREADSUPPORTFORMAT
#define PACKFS_ARCHIVEREADSUPPORTFORMAT(a) archive_read_support_format_iso9660(a);archive_read_support_format_zip(a);archive_read_support_format_tar(a);archive_read_support_filter_gzip(a);archive_read_support_filter_xz(a);
#endif
#endif

#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdlib.h>
#include <dirent.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>

#ifdef PACKFS_STATIC_PACKER
#include <ftw.h>
#endif

#ifdef PACKFS_ARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif


#define PACKFS_STRING_VALUE_(x) #x
#define PACKFS_STRING_VALUE(x) PACKFS_STRING_VALUE_(x)
#define PACKFS_CONCAT_(X, Y) X ## Y
#define PACKFS_CONCAT(X, Y) PACKFS_CONCAT_(X, Y)

#ifdef  PACKFS_DYNAMIC_LINKING
#define PACKFS_EXTERN_MOD
#define PACKFS_EXTERN_PTR(x)       (*x)
#define PACKFS_WRAP(x)             ( x)
#else
#define PACKFS_EXTERN_MOD extern
#define PACKFS_EXTERN_PTR(x)        (x)
#define PACKFS_WRAP(x) PACKFS_CONCAT(__wrap_, x)
#endif

#define PACKFS_EMPTY(s) ((s) == NULL || (s)[0] == '\0')

#define PACKFS_SPLIT(path, sep, begin, i, islast) for(size_t packfs_split_k = 0, len = 0, prefix_len = 0, (islast) = 0, (i) = 0; packfs_split_k < 1; packfs_split_k++) for(const char* (begin) = (path), *end = strchr((path), (sep)), *prevend = (path), *safeend = (end != NULL ? end : ((begin) + strlen((begin)))); (len = safeend - (begin) ), (prefix_len = safeend - (path) + 1), ((islast) = end == NULL), (prevend != NULL); prevend = end, (begin) = ((end != NULL && *end != '\0') ? (end + 1) : NULL), end = ((end != NULL && *end != '\0') ? strchr(end + 1, (sep)) : NULL), safeend = (end != NULL ? end : ((begin) != NULL ? ((begin) + strlen((begin))) : NULL))) if(safeend != (begin))

#define PACKFS_ITER_PATH(i, entryabspath, entryabspath_len, paths, num) for(const char* (entryabspath) = (paths); (entryabspath) != NULL; (entryabspath) = NULL) for(size_t (i) = 0, offset = 0, (entryabspath_len) = packfs_path_len((paths)); (i) < (num); offset += ((entryabspath_len) + 1), (i)++, (entryabspath) = (paths) + offset, (entryabspath_len) = packfs_path_len((paths) + offset))

#define PACKFS_APPEND_PATH(paths, paths_len, path, path_len)  { if(packfs_dynamic_dirs_paths_len > 0) {(paths)[(paths_len)++] = packfs_pathsep; } strncpy((paths) + (paths_len), (path), (path_len)); (paths_len) += (path_len); }
            

char packfs_default_prefix[] = 
#ifdef PACKFS_PREFIX
PACKFS_STRING_VALUE(PACKFS_PREFIX)
#else
"/packfs/"
#endif
;

char packfs_archives_ext[] =
#ifdef PACKFS_ARCHIVE
PACKFS_STRING_VALUE(PACKFS_ARCHIVEREADSUPPORTEXT)
#else
""
#endif
;

char packfs_listing_ext[] = ".json";
char packfs_sep = '/';
char packfs_pathsep = ':';
char packfs_extsep = '.';
char packfs_atsep = '@';
    
enum
{
    packfs_path_max = 255,
    packfs_dynamic_files_num_max = 65536,
    packfs_fd_min = 1000000000,
    packfs_fd_max = packfs_fd_min + packfs_dynamic_files_num_max,
    packfs_static_ino_offset = 1000000000,
    packfs_dynamic_ino_offset = packfs_static_ino_offset + packfs_static_ino_offset,
    packfs_dirs_ino_offset = 1000000,
    packfs_fd_cnt = packfs_fd_max - packfs_fd_min
};

const char packfs_static_package[] = "packfs_static_package";

#ifdef PACKFS_STATIC
#include "packfs.h"
#else
const char   packfs_static_prefix[1];
const size_t packfs_static_files_num;
const size_t packfs_static_dirs_num;
const char* packfs_static_files_paths; 
const char* packfs_static_dirs_paths; 
const size_t packfs_static_files_offsets[0]; 
const size_t packfs_static_files_sizes[0];
const char* _binary_packfs_static_package_start;
const char* _binary_packfs_static_package_end;
#endif


#ifndef PACKFS_STATIC_PACKER

PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_open)         (const char *path, int flags, ...);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_openat)       (int dirfd, const char *path, int flags, ...);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_close)        (int fd);
PACKFS_EXTERN_MOD ssize_t               PACKFS_EXTERN_PTR(__real_read)         (int fd, void* buf, size_t count);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_access)       (const char *path, int flags);
PACKFS_EXTERN_MOD off_t                 PACKFS_EXTERN_PTR(__real_lseek)        (int fd, off_t offset, int whence);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_stat)         (const char *restrict path, struct stat *restrict statbuf);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_fstat)        (int fd, struct stat * statbuf);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_fstatat)      (int dirfd, const char* path, struct stat * statbuf, int flags);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_statx)        (int dirfd, const char *restrict path, int flags, unsigned int mask, struct statx *restrict statbuf);
PACKFS_EXTERN_MOD FILE*                 PACKFS_EXTERN_PTR(__real_fopen)        (const char *path, const char *mode);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_fclose)       (FILE* stream);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_fileno)       (FILE* stream);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_fcntl)        (int fd, int action, ...);
PACKFS_EXTERN_MOD DIR*                  PACKFS_EXTERN_PTR(__real_opendir)      (const char *path);
PACKFS_EXTERN_MOD DIR*                  PACKFS_EXTERN_PTR(__real_fdopendir)    (int dirfd);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_closedir)     (DIR *dirp);
PACKFS_EXTERN_MOD struct dirent*        PACKFS_EXTERN_PTR(__real_readdir)      (DIR *dirp);

#else

#define __real_open                     open
#define __real_openat                   openat 
#define __real_close                    close
#define __real_read                     read
#define __real_access                   access
#define __real_lseek                    lseek
#define __real_stat                     stat
#define __real_fstat                    fstat 
#define __real_fstatat                  fstatat
#define __real_statx                    statx
#define __real_fopen                    fopen
#define __real_fclose                   fclose
#define __real_fileno                   fileno
#define __real_fcntl                    fcntl
#define __real_opendir                  opendir
#define __real_fdopendir                fdopendir
#define __real_closedir                 closedir
#define __real_readdir                  readdir

#endif

void packfs_init__real()
{
#ifdef PACKFS_DYNAMIC_LINKING
    #include <dlfcn.h>
    __real_open      = dlsym(RTLD_NEXT, "open"      );
    __real_openat    = dlsym(RTLD_NEXT, "openat"    );
    __real_close     = dlsym(RTLD_NEXT, "close"     );
    __real_read      = dlsym(RTLD_NEXT, "read"      );
    __real_access    = dlsym(RTLD_NEXT, "access"    );
    __real_lseek     = dlsym(RTLD_NEXT, "lseek"     );
    __real_stat      = dlsym(RTLD_NEXT, "stat"      );
    __real_fstat     = dlsym(RTLD_NEXT, "fstat"     );
    __real_fstatat   = dlsym(RTLD_NEXT, "fstatat"   );
    __real_statx     = dlsym(RTLD_NEXT, "statx"     );
    __real_fopen     = dlsym(RTLD_NEXT, "fopen"     );
    __real_fclose    = dlsym(RTLD_NEXT, "fclose"    );
    __real_fileno    = dlsym(RTLD_NEXT, "fileno"    );
    __real_fcntl     = dlsym(RTLD_NEXT, "fcntl"     );
    __real_opendir   = dlsym(RTLD_NEXT, "opendir"   );
    __real_fdopendir = dlsym(RTLD_NEXT, "fdopendir" );
    __real_closedir  = dlsym(RTLD_NEXT, "closedir"  );
    __real_readdir   = dlsym(RTLD_NEXT, "readdir"   );
#endif
}


int packfs_initialized;
int packfs_enabled;
int             packfs_fd                   [packfs_fd_cnt];
int             packfs_fdrefs               [packfs_fd_cnt];
char            packfs_fileisdir            [packfs_fd_cnt];
void*           packfs_fileptr              [packfs_fd_cnt];
size_t          packfs_filesize             [packfs_fd_cnt];
size_t          packfs_fileino              [packfs_fd_cnt];
struct dirent   packfs_dirent               [packfs_fd_cnt];
char   packfs_dynamic_prefix                [packfs_dynamic_files_num_max * packfs_path_max];
char   packfs_dynamic_archive_paths         [packfs_dynamic_files_num_max * packfs_path_max];
size_t packfs_dynamic_archive_paths_len;
size_t packfs_dynamic_archive_paths_offset  [packfs_dynamic_files_num_max];
char   packfs_dynamic_files_paths           [packfs_dynamic_files_num_max * packfs_path_max];
size_t packfs_dynamic_files_paths_len;
size_t packfs_dynamic_files_sizes           [packfs_dynamic_files_num_max];
size_t packfs_dynamic_files_offsets         [packfs_dynamic_files_num_max];
size_t packfs_dynamic_files_paths_prefixlen [packfs_dynamic_files_num_max];
size_t packfs_dynamic_files_num;

char   packfs_dynamic_dirs_paths            [packfs_dynamic_files_num_max * packfs_path_max];
size_t packfs_dynamic_dirs_paths_len;
size_t packfs_dynamic_dirs_num;

void packfs_normalize_path(char* path_normalized, const char* path)
{
    size_t len = path != NULL ? strlen(path) : 0;
    if(len == 0)
    {
        path_normalized[0] = '\0';
        return;
    }

    // lstrips ./ in the beginning
    for(int i = (path != NULL && len > 2 && path[0] == packfs_extsep && path[1] == packfs_sep) ? 2 : 0, k = 0; len > 0 && i < len; i++)
    {
        if(!(i > 1 && path[i] == packfs_sep && path[i - 1] == packfs_sep))
        {
            //collapses double consecutive slashes
            path_normalized[k++] = path[i];
            path_normalized[k] = '\0';
        }
    }
    size_t path_normalized_len = strlen(path_normalized);
    
    // and rstrips abc/.. at the end of /path/to/abc/asd/.. 
    if(path_normalized_len >= 3 && path_normalized[path_normalized_len - 1] == packfs_extsep && path_normalized[path_normalized_len - 2] == packfs_extsep  && path_normalized[path_normalized_len - 3] == packfs_sep)
    {
        path_normalized[path_normalized_len - 3] = '\0';
        char* trailing_slash = strrchr(path_normalized, packfs_sep);
        if(trailing_slash != NULL)
            *trailing_slash = '\0';
    }
}

int packfs_dump_listing(const char* removeprefix, const char* output_path)
{
    int r = 0;
    FILE* f = fopen(output_path, "w");
    if(!f) { r = 1; fprintf(stderr, "#could not open output file: %s\n", output_path); return r; }
    
    size_t removeprefix_len = strlen(removeprefix);

    fprintf(f, "[\n");
    size_t first = 1;
    PACKFS_SPLIT(packfs_dynamic_dirs_paths, packfs_pathsep, begin, i, islast)
    {
        const char* relative_path = begin + removeprefix_len;
        size_t relative_len = len - removeprefix_len;
        fprintf(f, "%c {\"path\": \"%.*s\"}\n", first ? ' ' : ',', (int)relative_len, relative_path);
        first = 0;
    }
    PACKFS_SPLIT(packfs_dynamic_files_paths, packfs_pathsep, begin, i, islast)
    {
        const char* relative_path = begin + removeprefix_len;
        size_t relative_len = len - removeprefix_len;
        fprintf(f, "%c {\"path\": \"%.*s\", \"size\": %zu, \"offset\": %zu}\n", first ? ' ' : ',', (int)relative_len, relative_path, packfs_dynamic_files_sizes[i], packfs_dynamic_files_offsets[i]);
        first = 0;
    }
    fprintf(f, "]\n\n");

    fclose(f);
    return r;
}

int packfs_dump_static_package(const char* prefix, const char* removeprefix, const char* output_path, const char* ld, const char* input_path)
{
    // TODO: in addition to .h, output .json

    int r = 0;

#ifdef PACKFS_STATIC_PACKER
    if(input_path != NULL && ld != NULL)
    {
        char tmp[1024];
        sprintf(tmp, "\"%s\" -r -b binary -o \"%s.o\" \"%s\"", ld, output_path, input_path);
        r = system(tmp);
        if(r != 0) { fprintf(stderr, "#could not open invoke ld: %s.o\n", output_path); return r; }
    }

    size_t prefix_len = strlen(prefix);
    size_t prefix_len_m1 = (prefix_len >= 1 && prefix[prefix_len - 1] == packfs_sep) ? (prefix_len - 1) : prefix_len;

    size_t removeprefix_len = strlen(removeprefix);
    size_t removeprefix_len_m1 = (removeprefix_len >= 1 && removeprefix[removeprefix_len - 1] == packfs_sep) ? (removeprefix_len - 1) : removeprefix_len;
    removeprefix_len = removeprefix_len > 0 ? (removeprefix_len_m1 + 1) : 0;
        
    FILE* f = fopen(output_path, "w");
    if(!f) { r = 1; fprintf(stderr, "#could not open output file: %s\n", output_path); return r; }

    fprintf(f, "#include <stddef.h>\n\n");
    fprintf(f, "const char packfs_static_prefix[] = \"%.*s%c\";\n\n", (int)prefix_len_m1, prefix, packfs_sep);
    fprintf(f, "const size_t packfs_static_dirs_num = %zu, packfs_static_files_num = %zu;\n\n", packfs_dynamic_dirs_num, packfs_dynamic_files_num);
    fprintf(f, "extern char _binary_%s_start[], _binary_%s_end[];\n\n", packfs_static_package, packfs_static_package);
    fprintf(f, "const char packfs_static_dirs_paths[] =\n");
    PACKFS_SPLIT(packfs_dynamic_dirs_paths, packfs_pathsep, begin, i, islast)
    {
        const char* relative_path = begin + removeprefix_len;
        size_t relative_len = len - removeprefix_len;
        fprintf(f, "\"%.*s%c%.*s\" \"%s\"\n", (int)prefix_len_m1, prefix, packfs_sep, (int)relative_len, relative_path, islast ? "" : ":");
    }
    if(packfs_dynamic_dirs_num == 0)
        fprintf(f, "\"\"\n");
    fprintf(f, ";\n\n");
    
    fprintf(f, "const char packfs_static_files_paths[] =\n");
    PACKFS_SPLIT(packfs_dynamic_files_paths, packfs_pathsep, begin, i, islast)
    {
        const char* relative_path = begin + removeprefix_len;
        size_t relative_len = len - removeprefix_len;
        fprintf(f, "\"%.*s%c%.*s\" \"%s\"\n", (int)prefix_len_m1, prefix, packfs_sep, (int)relative_len, relative_path, islast ? "" : ":");
    }
    if(packfs_dynamic_files_num == 0)
        fprintf(f, "\"\"\n");
    fprintf(f, ";\n\n");
    
    fprintf(f, "\n\nconst size_t packfs_static_files_offsets[] = {\n");
    for(size_t i = 0; i < packfs_dynamic_files_num; i++)
        fprintf(f, "%zu,\n", packfs_dynamic_files_offsets[i]); 
    fprintf(f, "};\n\n");
    
    fprintf(f, "\n\nconst size_t packfs_static_files_sizes[] = {\n");
    for(size_t i = 0; i < packfs_dynamic_files_num; i++)
        fprintf(f, "%zu,\n", packfs_dynamic_files_sizes[i]); 
    fprintf(f, "};\n\n");

    fclose(f);
#endif
    return r;
}

size_t packfs_path_len(const char* path)
{
    if(PACKFS_EMPTY(path) || path[0] == packfs_pathsep)
        return 0;

    const char* sep = strchr(path, packfs_pathsep);
    if(sep == NULL)
        return strlen(path);
    return sep - path;
}

int packfs_match_ext(const char* path, size_t path_len, const char* exts)
{
    if(path_len == 0 || PACKFS_EMPTY(path) || PACKFS_EMPTY(exts))
        return 0;

    const char* path_ext = strrchr(path, packfs_extsep);
    if(path_ext == NULL)
        return 0;
    
    PACKFS_SPLIT(exts, packfs_pathsep, begin, i, islast)
    {
        if(len > 0 && path_len >= len && 0 == strncmp(begin, path + path_len - len, len))
            return 1;
    }
    return 0;
}

size_t packfs_calc_archive_prefixlen(const char* path, const char* exts)
{
    if(PACKFS_EMPTY(path) || PACKFS_EMPTY(exts))
        return 0;

    PACKFS_SPLIT(path, packfs_sep, begin, i, islast)
    {
        if(packfs_match_ext(path, prefix_len - 1, exts))
            return prefix_len;
    }
    return 0;
}

enum packfs_match_path_mode
{
    PACKFS_DIR_EXISTS = 0, 
    PACKFS_FILE_MATCHES = 1, 
    PACKFS_ENTRY_IN_DIR = 2, 
    PACKFS_ENTRY_IN_DIR_RECURSIVELY = 3
};

size_t packfs_match_path(const char* haystack, size_t haystack_len, const char* needle, size_t needle_len, int mode)
{
    //TODO: use switch
    if(mode == PACKFS_DIR_EXISTS)
    {
        if(needle[needle_len - 1] == packfs_sep)
            needle_len--;

        PACKFS_SPLIT(haystack, packfs_pathsep, begin, i, islast)
        {
            if(0 != strncmp(begin, needle, needle_len))
                continue;

            if(((len == needle_len) || (len == needle_len + 1 && begin[needle_len] == packfs_sep && (begin[needle_len + 1] == '\0' || begin[needle_len + 1] == packfs_pathsep))))
                return 1;
        }
        return 0;
    }
    else if(mode == PACKFS_FILE_MATCHES)
    {
        if(0 != strncmp(haystack, needle, needle_len))
            return 0;

        return (haystack[needle_len] == '\0' || haystack[needle_len] == packfs_pathsep);
    }
    else if(mode == PACKFS_ENTRY_IN_DIR_RECURSIVELY)
    {
        if(PACKFS_EMPTY(haystack) || PACKFS_EMPTY(needle))
            return 0;
        size_t needle_len = strlen(needle);
        
        PACKFS_SPLIT(haystack, packfs_pathsep, begin, i, islast)
        {
            int prefix_trailing_slash = begin[len - 1] == packfs_sep;
            int prefix_ok = 0 == strncmp(begin, needle, len - prefix_trailing_slash);
            size_t len_m1 = len - prefix_trailing_slash;
            if(prefix_ok && ((needle_len == len_m1) || (needle_len >= len && needle[len_m1] == packfs_sep)))
                return len;
        }
        return 0;
        
    }
    else if(mode == PACKFS_ENTRY_IN_DIR)
    {
        if(haystack_len == 0 || (haystack_len > 0 && haystack[haystack_len - 1] != packfs_sep))
            return 0;
        int prefix_matches = 0 == strncmp(haystack, needle, haystack_len - 1);
        if(!prefix_matches)
            return 0;
        const char* suffix_slash = strchr(needle + haystack_len, packfs_sep);
        int no_suffix_slash = (NULL == suffix_slash) || (suffix_slash >= needle + needle_len);
        int suffix_without_dirs = no_suffix_slash || (needle + needle_len - 1 == suffix_slash);
        int suffix_not_empty = needle_len - haystack_len > 0;
        return suffix_without_dirs && suffix_not_empty;
    }
    return 0;
}

void packfs_dynamic_add_file(const char* prefix, size_t prefix_len, const char* entrypath, size_t entrypath_len, size_t entrysize, size_t entryoffset, size_t archivepaths_offset)
{
    if(entrypath_len == 0 || prefix_len == 0 || PACKFS_EMPTY(prefix)) return;
    size_t prefix_len_m1 = (prefix[prefix_len - 1] == packfs_sep) ? (prefix_len - 1) : prefix_len;

    PACKFS_APPEND_PATH(packfs_dynamic_files_paths, packfs_dynamic_files_paths_len, prefix, prefix_len_m1)
    packfs_dynamic_files_paths[packfs_dynamic_files_paths_len++] = packfs_sep;

    if(entrypath_len > 0)
    {
        strncpy(packfs_dynamic_files_paths + packfs_dynamic_files_paths_len, entrypath, entrypath_len);
        if(entrypath_len > 0 && entrypath[entrypath_len - 1] == packfs_sep) entrypath_len--;
        packfs_dynamic_files_paths_len += entrypath_len;
    }
                
    packfs_dynamic_files_paths_prefixlen[packfs_dynamic_files_num] = prefix_len_m1 + 1;
    packfs_dynamic_files_sizes[packfs_dynamic_files_num] = entrysize;
    packfs_dynamic_files_offsets[packfs_dynamic_files_num] = entryoffset;
    packfs_dynamic_archive_paths_offset[packfs_dynamic_files_num] = archivepaths_offset;
    packfs_dynamic_files_num++;
}

void packfs_dynamic_add_dirname(const char* prefix, size_t prefix_len, const char* entrypath, size_t entrypath_len, size_t entryisdir)
{
    if(prefix_len == 0 || PACKFS_EMPTY(prefix)) return;
    size_t prefix_len_m1 = (prefix[prefix_len - 1] == packfs_sep) ? (prefix_len - 1) : prefix_len;

    char path[packfs_path_max] = {0};
    strncpy(path, prefix, prefix_len_m1);
    path[prefix_len_m1] = packfs_sep;
    strncpy(path + prefix_len_m1 + 1, entrypath, entrypath_len);
    if(entryisdir && path[prefix_len_m1 + 1 + entrypath_len - 1] != packfs_sep)
        path[prefix_len_m1 + 1 + entrypath_len] = packfs_sep;

     //TODO: 1) extract dirname, 2) test if dir exists, 3) if not exists, invoke dir_add_all which should iterate all dirs and if not exists, register them
    
    PACKFS_SPLIT(path + prefix_len_m1 - 1, packfs_sep, begin, i, islast)
    {
        if(!islast)
        {
            if(packfs_match_path(packfs_dynamic_dirs_paths, packfs_dynamic_dirs_paths_len, path, prefix_len, PACKFS_DIR_EXISTS))
                continue;

            PACKFS_APPEND_PATH(packfs_dynamic_dirs_paths, packfs_dynamic_dirs_paths_len, path, prefix_len)
            
            packfs_dynamic_dirs_num++;
        }
    }
}

void packfs_dynamic_add_prefix(const char* prefix, size_t prefix_len)
{
    if(prefix_len == 0 || PACKFS_EMPTY(prefix)) return;
    size_t prefix_len_m1 = (prefix[prefix_len - 1] == packfs_sep) ? (prefix_len - 1) : prefix_len;
    
    size_t packfs_dynamic_prefix_len = strlen(packfs_dynamic_prefix);
    
    if(packfs_match_path(packfs_dynamic_prefix, packfs_dynamic_prefix_len, prefix, prefix_len_m1, PACKFS_DIR_EXISTS))
        return;

    PACKFS_APPEND_PATH(packfs_dynamic_prefix, packfs_dynamic_prefix_len, prefix, prefix_len_m1)
    packfs_dynamic_prefix[packfs_dynamic_files_paths_len] = packfs_sep;
    packfs_dynamic_prefix[packfs_dynamic_files_paths_len + 1] = '\0';
}


//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

// https://github.com/libarchive/libarchive/blob/master/libarchive/archive_read_open_file.c
// https://github.com/libarchive/libarchive/blob/master/libarchive/archive_read_open_fd.c

struct packfs_archive_data
{
    int fd;
    size_t last_file_offset;
    uint8_t buffer[1024 * 4];
};

void packfs_archive_read_new(void* aptr)
{
#ifdef PACKFS_ARCHIVE
    struct archive* a = aptr;
    PACKFS_ARCHIVEREADSUPPORTFORMAT(a);
#endif
}

int64_t packfs_archive_seek_callback(struct archive *a, void *client_data, int64_t request, int whence)
{
    struct packfs_archive_data *mine = (struct packfs_archive_data *)client_data;
    int64_t seek = request;
    int64_t r;
    int seek_bits = sizeof(seek) * 8 - 1;  /* off_t is a signed type. */

    /* We use off_t here because lseek() is declared that way. */

    /* Do not perform a seek which cannot be fulfilled. */
    if (sizeof(request) > sizeof(seek))
    {
        const int64_t max_seek = (((int64_t)1 << (seek_bits - 1)) - 1) * 2 + 1;
        const int64_t min_seek = ~max_seek;
        if (request < min_seek || request > max_seek)
        {
            errno = EOVERFLOW;
            goto err;
        }
    }

    r = lseek(mine->fd, seek, whence);
    if (r >= 0)
        return r;

err:
    if (errno == ESPIPE)
    {
        archive_set_error(a, errno, "A file descriptor(%d) is not seekable(PIPE)", mine->fd);
        return (ARCHIVE_FAILED);
    }
    else
    {
        /* If the input is corrupted or truncated, fail. */
        archive_set_error(a, errno, "Error seeking in a file descriptor(%d)", mine->fd);
        return (ARCHIVE_FATAL);
    }
}


ssize_t packfs_archive_read_callback(struct archive *a, void *client_data, const void **buff)
{
    // https://github.com/libarchive/libarchive/blob/master/libarchive/archive_read_open_fd.c
    struct packfs_archive_data *mine = (struct packfs_archive_data *)client_data;
    ssize_t bytes_read;
    
    mine->last_file_offset = packfs_archive_seek_callback(a, client_data, 0, SEEK_CUR);

    *buff = mine->buffer;

    for (;;)
    {
        bytes_read = read(mine->fd, mine->buffer, sizeof(mine->buffer));
        if (bytes_read < 0)
        {
            if (errno == EINTR)
                continue;
            archive_set_error(a, errno, "Error reading fd %d", mine->fd);
        }
        return (bytes_read);
    }
    return 0;
}

int packfs_scan_archive(FILE* f, const char* packfs_archive_filename, const char* prefix)
{
    int r = 0;
#ifdef PACKFS_ARCHIVE
    struct archive *a = archive_read_new();
    packfs_archive_read_new(a);
    
    size_t prefix_len_m1 = prefix != NULL ? strlen(prefix) : 0;
    if(prefix_len_m1 > 0 && prefix[prefix_len_m1 - 1] == packfs_sep) prefix_len_m1--;
    size_t packfs_archive_filename_len = strlen(packfs_archive_filename);

    packfs_dynamic_add_dirname(prefix, prefix_len_m1, "", 0, 1); 
    packfs_dynamic_add_prefix(prefix, prefix_len_m1);
    
    size_t archivepaths_offset = packfs_dynamic_archive_paths_len;
    strncpy(packfs_dynamic_archive_paths + packfs_dynamic_archive_paths_len, packfs_archive_filename, packfs_archive_filename_len);
    packfs_dynamic_archive_paths_len += packfs_archive_filename_len + 1;

    struct archive_entry *entry;
    do
    {
        struct packfs_archive_data client_data;
        client_data.fd = open(packfs_archive_filename, O_RDONLY);
        archive_read_set_seek_callback(a, packfs_archive_seek_callback);
        archive_read_set_read_callback(a, packfs_archive_read_callback);
        archive_read_set_callback_data(a, &client_data);
        r = archive_read_open1(a);
        
        // r = archive_read_open_FILE(a, f);
        if(r != ARCHIVE_OK) { fprintf(stderr, "#%s\n", archive_error_string(a)); break; }
        
        for(;;)
        {
            r = archive_read_next_header(a, &entry);
            if (r == ARCHIVE_EOF)
                break;
            if (r != ARCHIVE_OK)
                break; //{ fprintf(stderr, "#%s\n", archive_error_string(a)); return r; }
                
            int entrytype = archive_entry_filetype(entry);
            size_t entrysize = (size_t)archive_entry_size(entry);
            size_t entryoffset = 0;
            const char* entrypath = archive_entry_pathname(entry);
            size_t entrypath_len = strlen(entrypath);
            
            int entryisdir = entrytype == AE_IFDIR, entryisfile = entrytype == AE_IFREG;
            const char* entrytypestr = entrytype == AE_IFMT ? "AE_IFMT" : entrytype == AE_IFREG ? "AE_IFREG" : entrytype == AE_IFLNK ? "AE_IFLNK" : entrytype == AE_IFSOCK ? "AE_IFSOCK" : entrytype == AE_IFCHR ? "AE_IFCHR" : entrytype == AE_IFBLK ? "AE_IFBLK" : entrytype == AE_IFDIR ? "AE_IFDIR" : entrytype == AE_IFIFO ? "AE_IFIFO" : "archive_entry_pathname(entry) value is unknown";
            
            const void* firstblock_buff;
            size_t firstblock_len;
            int64_t firstblock_offset;
            r = archive_read_data_block(a, &firstblock_buff, &firstblock_len, &firstblock_offset);
            const char* firstblock_buff_charptr = (char*)firstblock_buff;
            const char* curblock_buff_charptr = (const char*)client_data.buffer;
            if(entryisfile && archive_entry_size_is_set(entry) && curblock_buff_charptr <= firstblock_buff_charptr && firstblock_buff_charptr < curblock_buff_charptr + sizeof(client_data.buffer))
                entryoffset = client_data.last_file_offset + (size_t)(firstblock_buff_charptr - curblock_buff_charptr);

            packfs_dynamic_add_dirname(prefix, prefix_len_m1, entrypath, entrypath_len, entryisdir); 
            if(entryisfile)
                packfs_dynamic_add_file(prefix, prefix_len_m1, entrypath, entrypath_len, entrysize, entryoffset, archivepaths_offset);
                
            r = archive_read_data_skip(a);
            if (r == ARCHIVE_EOF)
                break;
            if (r != ARCHIVE_OK)
                break; // { fprintf(stderr, "%s\n", archive_error_string(a)); return r; }
        }
    }
    while(0);
    archive_read_close(a);
    archive_read_free(a);
#endif
    return r;
}

void packfs_extract_archive_entry_from_FILE_to_FILE(FILE* f, const char* entrypath, size_t entrypath_len, FILE* h)
{
#ifdef PACKFS_ARCHIVE
    struct archive *a = archive_read_new();
    packfs_archive_read_new(a);

    struct archive_entry *entry;
    do
    {
        //if(archive_read_open_memory(a, buf, cnt) != ARCHIVE_OK)
        if(archive_read_open_FILE(a, f) != ARCHIVE_OK)
            break;
        
        while (1)
        {
            int r = archive_read_next_header(a, &entry);
            if (r == ARCHIVE_EOF)
                break;
            if (r != ARCHIVE_OK)
                break; //fprintf(stderr, "%s\n", archive_error_string(a));

            const char* path = archive_entry_pathname(entry); size_t path_len = strlen(path);
            if(packfs_match_path(path, path_len, entrypath, entrypath_len, PACKFS_FILE_MATCHES))
            {
                const void *buff;
                size_t size;
                off_t offset;

                while ((r = archive_read_data_block(a, &buff, &size, &offset)) == ARCHIVE_OK)
                {
                    // assert(offset <= output_offset), do not support sparse files just yet, https://github.com/libarchive/libarchive/issues/2299
                    const char* p = buff;
                    while (size > 0)
                    {
                        ssize_t bytes_written = fwrite(p, 1, size, h);
                        p += bytes_written;
                        size -= bytes_written;
                    }
                }
                break;
            }
            else
            {
                r = archive_read_data_skip(a);
                if (r == ARCHIVE_EOF)
                    break;
                if (r != ARCHIVE_OK)
                    break; //fprintf(stderr, "%s\n", archive_error_string(a));
            }
        }
    }
    while(0);
    archive_read_close(a);
    archive_read_free(a);
#endif
}

void packfs_scan_dir(DIR* dirptr, const char* path_normalized, size_t len, const char* prefix)
{
    for(struct dirent* entry = __real_readdir(dirptr); entry != NULL; entry = __real_readdir(dirptr))
    {
        size_t path_prefix_len = packfs_calc_archive_prefixlen(entry->d_name, packfs_archives_ext);
        if(path_prefix_len > 0)
        {
            char _path_normalized[packfs_path_max];
            strcpy(_path_normalized, path_normalized);
            _path_normalized[len] = packfs_sep;
            strcpy(_path_normalized + len + 1, entry->d_name);
        
            FILE* fileptr = __real_fopen(_path_normalized, "rb");
            if(fileptr != NULL)
            {
                packfs_enabled = 1;
                // something below seems to trigger again packfs_init();
                packfs_scan_archive(fileptr, _path_normalized, prefix);
                __real_fclose(fileptr);
            }

        }
    }
}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

void packfs_scan_listing(FILE* fileptr, const char* packfs_listing_filename, const char* prefix, const char* prefix_archive)
{
    size_t prefix_len_m1 = prefix != NULL ? packfs_path_len(prefix) : 0;
    if(prefix_len_m1 > 0 && prefix[prefix_len_m1 - 1] == packfs_sep) prefix_len_m1--;

    const char* packfs_archive_filename = packfs_listing_filename;
    size_t packfs_archive_filename_len = strlen(packfs_listing_filename) - strlen(packfs_listing_ext);
    
    packfs_dynamic_add_dirname(prefix, prefix_len_m1, "", 0, 1); 
    packfs_dynamic_add_prefix(prefix, prefix_len_m1);
    
    size_t archivepaths_offset = packfs_dynamic_archive_paths_len;

    strncpy(packfs_dynamic_archive_paths + packfs_dynamic_archive_paths_len, packfs_archive_filename, packfs_archive_filename_len);
    packfs_dynamic_archive_paths[packfs_dynamic_archive_paths_len + packfs_archive_filename_len] = packfs_pathsep;
    packfs_dynamic_archive_paths_len += packfs_archive_filename_len + 1;
        
    {
        char entrypath[packfs_path_max];
        size_t entrysize, entryoffset, entrypath_len;
        fscanf(fileptr, "[\n");
        for(;;)
        {
            entrysize = entryoffset = 0;

            fscanf(fileptr, "{\n");
            int ret = fscanf(fileptr, "\"path\"\n:\n\"%[^\"]\"", entrypath);
            if(ret != 1) break;
            entrypath_len = strlen(entrypath);
            fscanf(fileptr, ",\n");
            fscanf(fileptr, "\"size\"\n:\n%zu", &entrysize);
            fscanf(fileptr, ",\n");
            fscanf(fileptr, "\"offset\"\n:\n%zu", &entryoffset);
            fscanf(fileptr, "}\n");
            fscanf(fileptr, ",\n");

            int entryisdir = entrypath_len > 0 && entrypath[entrypath_len - 1] == packfs_sep;
            int entryisfile = !entryisdir;
            
            packfs_dynamic_add_dirname(prefix, prefix_len_m1, entrypath, entrypath_len, entryisdir); 
            if(entryisfile)
                packfs_dynamic_add_file(prefix, prefix_len_m1, entrypath, entrypath_len, entrysize, entryoffset, archivepaths_offset);
        }
        fscanf(fileptr, "]\n");
    }
}

void packfs_init(const char* path, const char* packfs_config)
{ 
    if(packfs_initialized != 1)
    {
        packfs_init__real();
        packfs_initialized = 1;
        packfs_enabled = 0;
    }
    
    if(packfs_initialized == 1 && packfs_enabled == 0)
    {
        const char* packfs_disabled = getenv("PACKFS_DISABLED");
        if(packfs_disabled != NULL && packfs_disabled[0] == '1' && packfs_disabled[1] == '\0')
            packfs_enabled = 1;
    }

    if(packfs_initialized == 1 && packfs_enabled == 0)
    {
        if(packfs_config == NULL)
            packfs_config = getenv("PACKFS_CONFIG");
        
        if(packfs_config != NULL && packfs_config[0] != '\0')
        {
            
            PACKFS_SPLIT(packfs_config, packfs_pathsep, begin, i, islast)
            {
                char path_normalized[packfs_path_max] = {0}; strncpy(path_normalized, begin, len);

                char* at_prefix = strchr(path_normalized, packfs_atsep);
                const char* prefix = at_prefix != NULL ? (at_prefix + 1) : packfs_default_prefix;
                size_t path_len = at_prefix == NULL ? len : (at_prefix - path_normalized);
                path_normalized[path_len] = '\0';
                char* at_prefixarchive = at_prefix != NULL ? strchr(prefix, packfs_atsep) : NULL;
                const char* prefix_archive = at_prefixarchive != NULL ? (at_prefixarchive + 1) : ""; 
                if(at_prefixarchive != NULL) at_prefixarchive[0] = '\0';

                size_t path_isdir = path_len >= 1 ? path_normalized[path_len - 1] == packfs_sep : 0;
                if(packfs_match_ext(path_normalized, path_len, packfs_listing_ext))
                {
                    FILE* fileptr = __real_fopen(path_normalized, "r");
                    if(fileptr != NULL)
                    {
                        packfs_enabled = 1;
                        packfs_scan_listing(fileptr, path_normalized, prefix, prefix_archive);
                        __real_fclose(fileptr);
                    }
                }
                else if(path_isdir)
                {
                    DIR* dirptr = __real_opendir(path_normalized);
                    if(dirptr != NULL)
                    {
                        packfs_enabled = 1;
                        packfs_scan_dir(dirptr, path_normalized, len, prefix);
                        __real_closedir(dirptr);
                    }
                }
                else
                {
                    FILE* fileptr = __real_fopen(path_normalized, "rb");
                    if(fileptr != NULL)
                    {
                        packfs_enabled = 1;
                        // something below seems to trigger again packfs_init();
                        packfs_scan_archive(fileptr, path_normalized, prefix);
                        __real_fclose(fileptr);
                    }
                }
            }
        }
        
        if(path != NULL && path[0] != '\0')
        {
            char path_normalized[packfs_path_max] = {0}; packfs_normalize_path(path_normalized, path);
            size_t path_prefix_len = packfs_calc_archive_prefixlen(path_normalized, packfs_archives_ext);
            if(path_prefix_len > 0)
            {
                path_normalized[path_prefix_len - 1] = '\0';
                const char* prefix = path_normalized;
               
                FILE* fileptr = __real_fopen(path_normalized, "rb");
                if(fileptr != NULL)
                {
                    packfs_enabled = 1;
                    // something below seems to trigger again packfs_init();
                    packfs_scan_archive(fileptr, path_normalized, prefix);
                    __real_fclose(fileptr);
                }
            }
        }
    }
}

int packfs_path_in_range(const char* prefix, const char* path)
{
    return packfs_match_path(prefix, 0, path, 0, PACKFS_ENTRY_IN_DIR_RECURSIVELY);
}

int packfs_fd_in_range(int fd)
{
    return fd >= 0 && fd >= packfs_fd_min && fd < packfs_fd_max;
}

void* packfs_find(int fd, void* ptr)
{
    if(ptr != NULL)
    {
        for(size_t k = 0; k < packfs_fd_cnt; k++)
        {
            if(packfs_fileptr[k] == ptr)
                return &packfs_fd[k];
        }
        return NULL;
    }
    else
    {
        if(!packfs_fd_in_range(fd))
            return NULL;
        
        for(size_t k = 0; k < packfs_fd_cnt; k++)
        {
            if(packfs_fd[k] == fd)
                return packfs_fileptr[k];
        }
    }
    return NULL;
}

void packfs_resolve_relative_path(char* dest, int dirfd, const char* path)
{
    #define PACKFS_CONCAT_PATH(dest, entryabspath, entryabspath_len, path) \
    { \
        path = (strlen(path) > 1 && path[0] == packfs_extsep && path[1] == packfs_sep) ? (path + 2) : path; \
        size_t path_len = strlen(path); \
        strncpy(dest, entryabspath, entryabspath_len); \
        strncpy(dest + entryabspath_len, path, path_len); \
        dest[entryabspath_len + path_len] = '\0'; \
    }

    size_t d_ino = 0, found = 0;
    for(size_t k = 0; k < packfs_fd_cnt; k++)
    {
        if(packfs_fd[k] == dirfd)
        {
            d_ino = packfs_fileino[k];
            found = 1;
            break;
        }
    }
    
    PACKFS_ITER_PATH(i, entryabspath, entryabspath_len, packfs_static_dirs_paths, packfs_static_dirs_num)
    {
        if(i == d_ino - packfs_static_ino_offset)
        {
            PACKFS_CONCAT_PATH(dest, entryabspath, entryabspath_len, path)
            return;
        }
    }
    
    PACKFS_ITER_PATH(i, entryabspath, entryabspath_len, packfs_dynamic_dirs_paths, packfs_dynamic_dirs_num)
    {
        if(i == d_ino - packfs_dynamic_ino_offset - packfs_dirs_ino_offset)
        {
            PACKFS_CONCAT_PATH(dest, entryabspath, entryabspath_len, path)
            return;
        }
    }

    strcpy(dest, path);
    #undef PACKFS_CAT_PATH
}

void* packfs_readdir(void* stream)
{
    #define PACKFS_FILL_DIRENT(dir_entry, d_type_val, d_ino_val, entryabspath, entryabspath_len) \
    { \
        char path_normalized[packfs_path_max] = {0}; strncpy(path_normalized, entryabspath, entryabspath_len); \
        int has_trailing_slash = path_normalized[(entryabspath_len) - 1] == packfs_sep; \
        path_normalized[has_trailing_slash ? (entryabspath_len - 1) : (entryabspath_len)] = '\0'; \
        const char* last_slash = strrchr(path_normalized, packfs_sep); \
        size_t basename_offset = last_slash != NULL ? (last_slash - path_normalized + 1) : 0; \
        size_t basename_len = (entryabspath_len - has_trailing_slash) - basename_offset; \
        strncpy(dir_entry->d_name, entryabspath + basename_offset, basename_len); \
        dir_entry->d_name[basename_len] = '\0'; \
        dir_entry->d_type = d_type_val; \
        dir_entry->d_ino = ((ino_t)(d_ino_val)); \
    }
    
    struct dirent* dir_entry = stream;
    size_t d_ino = (size_t)dir_entry->d_ino;

    if(d_ino >= packfs_static_ino_offset && d_ino < packfs_dynamic_ino_offset)
    {
        int check_dirs = (d_ino >= packfs_static_ino_offset + packfs_dirs_ino_offset) && (d_ino < packfs_static_ino_offset + packfs_dirs_ino_offset + packfs_dirs_ino_offset);
        int check_files = (d_ino >= packfs_static_ino_offset) && (d_ino < packfs_static_ino_offset + packfs_dirs_ino_offset);
        const char* dirabspath = packfs_static_dirs_paths + (size_t)dir_entry->d_off;
        size_t dirabspath_len = packfs_path_len(dirabspath);
        
        PACKFS_ITER_PATH(i, entryabspath, entryabspath_len, packfs_static_dirs_paths, packfs_static_dirs_num)
        {
            int match = packfs_match_path(dirabspath, dirabspath_len, entryabspath, entryabspath_len, PACKFS_ENTRY_IN_DIR);
            int i_match = i > (d_ino - packfs_static_ino_offset - packfs_dirs_ino_offset);
            if(i_match && match)
            {
                PACKFS_FILL_DIRENT(dir_entry, DT_DIR, packfs_static_ino_offset + packfs_dirs_ino_offset + i, entryabspath, entryabspath_len)
                return dir_entry;
            }
        }

        if(check_dirs)
        {
            check_files = 1;
            d_ino = packfs_static_ino_offset;
        }
        
        PACKFS_ITER_PATH(i, entryabspath, entryabspath_len, packfs_static_files_paths, packfs_static_files_num)
        {
            int match = packfs_match_path(dirabspath, dirabspath_len, entryabspath, entryabspath_len, PACKFS_ENTRY_IN_DIR);
            int i_match = (i > d_ino - packfs_static_ino_offset) || (i == 0 && check_dirs);
            if(i_match && match)
            {
                PACKFS_FILL_DIRENT(dir_entry, DT_REG, packfs_static_ino_offset + i, entryabspath, entryabspath_len)
                return dir_entry;
            }
        }
    }
    else if(d_ino >= packfs_dynamic_ino_offset)
    {
        int check_dirs = (d_ino >= packfs_dynamic_ino_offset + packfs_dirs_ino_offset) && (d_ino < packfs_dynamic_ino_offset + packfs_dirs_ino_offset + packfs_dirs_ino_offset);
        int check_files = (d_ino >= packfs_dynamic_ino_offset) && (d_ino < packfs_dynamic_ino_offset + packfs_dirs_ino_offset);
        const char* dirabspath = packfs_dynamic_dirs_paths + (size_t)dir_entry->d_off;
        size_t dirabspath_len = packfs_path_len(dirabspath);
        
        PACKFS_ITER_PATH(i, entryabspath, entryabspath_len, packfs_dynamic_dirs_paths, packfs_dynamic_dirs_num)
        {
            int match = packfs_match_path(dirabspath, dirabspath_len, entryabspath, entryabspath_len, PACKFS_ENTRY_IN_DIR);
            int i_match = (i > d_ino - packfs_dynamic_ino_offset - packfs_dirs_ino_offset);
            if(i_match && match)
            {
                PACKFS_FILL_DIRENT(dir_entry, DT_DIR, packfs_dynamic_ino_offset + packfs_dirs_ino_offset + i, entryabspath, entryabspath_len)
                return dir_entry;
            }
        }

        if(check_dirs)
        {
            check_files = 1;
            d_ino = packfs_dynamic_ino_offset;
        }
        
        PACKFS_ITER_PATH(i, entryabspath, entryabspath_len, packfs_dynamic_files_paths, packfs_dynamic_files_num)
        {
            int match = packfs_match_path(dirabspath, dirabspath_len, entryabspath, entryabspath_len, PACKFS_ENTRY_IN_DIR);
            int i_match = (i > d_ino - packfs_dynamic_ino_offset) || (i == 0 && check_dirs);
            if(i_match && match)
            {
                PACKFS_FILL_DIRENT(dir_entry, DT_REG, packfs_dynamic_ino_offset + i, entryabspath, entryabspath_len)
                return dir_entry;
            }
        }
    }
    return NULL;
    #undef PACKFS_FILL_DIRENT
}

int packfs_access(const char* path)
{
    char path_normalized[packfs_path_max] = {0}; packfs_normalize_path(path_normalized, path); size_t path_normalized_len = strlen(path_normalized);
    int path_in_range = 0;
    
    if(packfs_path_in_range(packfs_static_prefix, path_normalized))
    {
        path_in_range = 1;
        PACKFS_ITER_PATH(i, entryabspath, entryabspath_len, packfs_static_files_paths, packfs_static_files_num)
        {
            if(packfs_match_path(path_normalized, path_normalized_len, entryabspath, entryabspath_len, PACKFS_FILE_MATCHES))
                return 0;
        }
    }
        
    if(packfs_path_in_range(packfs_dynamic_prefix, path_normalized))
    {
        path_in_range = 1;
        PACKFS_ITER_PATH(i, entryabspath, entryabspath_len, packfs_dynamic_files_paths, packfs_dynamic_files_num)
        {
            if(packfs_match_path(path_normalized, path_normalized_len, entryabspath, entryabspath_len, PACKFS_FILE_MATCHES))
                return 0;
        }
    }

    return path_in_range ? -1 : -2;
}

int packfs_stat(const char* path, int fd, size_t* isdir, size_t* size, size_t* d_ino)
{
    // TODO: null path?
    char path_normalized[packfs_path_max] = {0}; packfs_normalize_path(path_normalized, path); size_t path_normalized_len = strlen(path_normalized);
    int path_in_range = 0;
    int path_is_empty = PACKFS_EMPTY(path);
    
    if(packfs_path_in_range(packfs_static_prefix, path_normalized))
    {
        path_in_range = 1;
        PACKFS_ITER_PATH(i, entryabspath, entryabspath_len, packfs_static_dirs_paths, packfs_static_dirs_num)
        {
            int match = packfs_match_path(entryabspath, entryabspath_len, path_normalized, path_normalized_len, PACKFS_DIR_EXISTS);
            if(match)
            {
                *size = 0;
                *isdir = 1;
                *d_ino = packfs_static_ino_offset + packfs_dirs_ino_offset + i;
                return 0;
            }
        }
        
        PACKFS_ITER_PATH(i, entryabspath, entryabspath_len, packfs_static_files_paths, packfs_static_files_num)
        {
            int match = packfs_match_path(path_normalized, path_normalized_len, entryabspath, entryabspath_len, PACKFS_FILE_MATCHES);
            if(match)
            {
                *size = packfs_static_files_sizes[i]; 
                *isdir = 0;
                *d_ino = packfs_static_ino_offset + i;
                return 0;
            }
        }
    }

    if(packfs_path_in_range(packfs_dynamic_prefix, path_normalized))
    {
        path_in_range = 1;
        PACKFS_ITER_PATH(i, entryabspath, entryabspath_len, packfs_dynamic_dirs_paths, packfs_dynamic_dirs_num)
        {
            int match = packfs_match_path(entryabspath, entryabspath_len, path_normalized, path_normalized_len, PACKFS_DIR_EXISTS);
            if(match)
            {
                *size = 0;
                *isdir = 1;
                *d_ino = packfs_dynamic_ino_offset + packfs_dirs_ino_offset + i;
                return 0;
            }
        }
    
        PACKFS_ITER_PATH(i, entryabspath, entryabspath_len, packfs_dynamic_files_paths, packfs_dynamic_files_num)
        {
            int match = packfs_match_path(path_normalized, path_normalized_len, entryabspath, entryabspath_len, PACKFS_FILE_MATCHES);
            if(match)
            {
                *size = packfs_dynamic_files_sizes[i];
                *isdir = 0;
                *d_ino = packfs_dynamic_ino_offset + i;
                return 0;
            }
        }
    }

    if(path_in_range)
        return -1;
    
    if(packfs_fd_in_range(fd))
    {
        for(size_t k = 0; k < packfs_fd_cnt; k++)
        {
            int match = packfs_fd[k] == fd; 
            if(match)
            {
                *size = packfs_filesize[k];
                *isdir = packfs_fileisdir[k];
                *d_ino = packfs_fileino[k];
                return 0;
            }
        }
        return -1;
    }

    return -2;
}

void* packfs_open(const char* path, int flags)
{
    char path_normalized[packfs_path_max] = {0}; packfs_normalize_path(path_normalized, path); size_t path_normalized_len = strlen(path_normalized);
    int path_in_range = 0;

    void* fileptr = NULL; size_t filesize = 0, d_ino = 0, d_off = 0;
    
    if(packfs_path_in_range(packfs_static_prefix, path_normalized))
    {
        PACKFS_ITER_PATH(i, entryabspath, entryabspath_len, packfs_static_dirs_paths, packfs_static_dirs_num)
        {
            int match = packfs_match_path(entryabspath, entryabspath_len, path_normalized, path_normalized_len, PACKFS_DIR_EXISTS);
            if(match)
            {
                path_in_range = 2;
                d_ino = packfs_static_ino_offset + packfs_dirs_ino_offset + i;
                d_off = offset;
                filesize = 0;
            }
        }
        
        PACKFS_ITER_PATH(i, entryabspath, entryabspath_len, packfs_static_files_paths, packfs_static_files_num)
        {
            int match = packfs_match_path(entryabspath, entryabspath_len, path_normalized, path_normalized_len, PACKFS_FILE_MATCHES);
            if(match)
            {
                path_in_range = 1;
                d_ino = packfs_static_ino_offset + i;
                d_off = 0;
                
                filesize = packfs_static_files_sizes[i];
                fileptr = fmemopen((void*)_binary_packfs_static_package_end, filesize, "r");
            }
        }
    }

    if(packfs_path_in_range(packfs_dynamic_prefix, path_normalized))
    {
        path_in_range = 1;
        PACKFS_ITER_PATH(i, entryabspath, entryabspath_len, packfs_dynamic_dirs_paths, packfs_dynamic_dirs_num)
        {
            int match = packfs_match_path(entryabspath, entryabspath_len, path_normalized, path_normalized_len, PACKFS_DIR_EXISTS);
            if(match)
            {
                path_in_range = 2;
                d_ino = packfs_dynamic_ino_offset + packfs_dirs_ino_offset + i;
                d_off = offset;
                filesize = 0;
            }
        }
        
        PACKFS_ITER_PATH(i, entryabspath, entryabspath_len, packfs_dynamic_files_paths, packfs_dynamic_files_num)
        {
            size_t prefix_len = packfs_dynamic_files_paths_prefixlen[i];
            
            const char* archivepath = packfs_dynamic_archive_paths + packfs_dynamic_archive_paths_offset[i];
            const char* entrypath = entryabspath + prefix_len;
            size_t entrypath_len = packfs_path_len(entrypath);
            
            if(packfs_match_path(path_normalized, path_normalized_len, entryabspath, entryabspath_len, PACKFS_FILE_MATCHES))
            {
                path_in_range = 1;
                d_ino = packfs_dynamic_ino_offset + i;
                d_off = 0;
                filesize = packfs_dynamic_files_sizes[i];

                fileptr = fmemopen(NULL, filesize, "rb+");
                
                char archivepath_[packfs_path_max] = {0}; strncpy(archivepath_, archivepath, packfs_path_len(archivepath));

                FILE* packfs_archive_fileptr = __real_fopen(archivepath_, "rb");//packfs_archive_fileptr;
                if(packfs_archive_fileptr != NULL)
                {
                    packfs_extract_archive_entry_from_FILE_to_FILE(packfs_archive_fileptr, entrypath, entrypath_len, (FILE*)fileptr);
                    __real_fclose(packfs_archive_fileptr);
                }
                fseek((FILE*)fileptr, 0, SEEK_SET);
                break;
            }
        }
    }

    for(size_t k = 0; path_in_range && k < packfs_fd_cnt; k++)
    {
        if(packfs_fd[k] == 0)
        {
            int fd = packfs_fd_min + k;
            
            if(path_in_range == 2)
            {
                packfs_dirent[k] = (struct dirent){0};
                packfs_dirent[k].d_ino = (ino_t)d_ino;
                packfs_dirent[k].d_off = (off_t)d_off;
                fileptr = &packfs_dirent[k];
            }

            packfs_fileisdir[k] = path_in_range == 2;
            packfs_fdrefs[k] = 1;
            packfs_fd[k] = fd;
            packfs_fileptr[k] = fileptr;
            packfs_filesize[k] = filesize;
            packfs_fileino[k] = d_ino;
            return fileptr;
        }
    }

    return NULL;
}

int packfs_close(int fd)
{
    if(!packfs_fd_in_range(fd))
        return -2;

    for(size_t k = 0; k < packfs_fd_cnt; k++)
    {
        if(packfs_fd[k] == fd)
        {
            packfs_fdrefs[k]--;
            if(packfs_fdrefs[k] > 0)
                return 0;

            int res = (!packfs_fileisdir[k]) ? __real_fclose(packfs_fileptr[k]) : 0;
            packfs_dirent[k]  = (struct dirent){0};
            packfs_fileisdir[k] = 0;
            packfs_fd[k] = 0;
            packfs_filesize[k] = 0;
            packfs_fileptr[k] = NULL;
            packfs_fileino[k] = 0;
            return res;
        }
    }
    return -1;
}

ssize_t packfs_read(int fd, void* buf, size_t count)
{
    FILE* ptr = packfs_find(fd, NULL);
    if(!ptr)
        return -1;
    return fread(buf, 1, count, ptr);
}

int packfs_seek(int fd, long offset, int whence)
{
    FILE* ptr = packfs_find(fd, NULL);
    if(!ptr)
        return -1;
    return fseek(ptr, offset, whence);
}

int packfs_dup(int oldfd, int newfd)
{
    int K = -1;
    if(oldfd >= 0 && packfs_fd_min <= oldfd && oldfd < packfs_fd_max)
    {
        for(size_t k = 0; k < packfs_fd_cnt; k++)
        {
            if(packfs_fd[k] == oldfd)
            {
                K = k;
                break;
            }
        }
    }
    for(size_t k = 0; K >= 0 && k < packfs_fd_cnt; k++)
    {
        int fd = packfs_fd_min + k;
        if(packfs_fd[k] == 0 && (newfd < packfs_fd_min || newfd >= fd))
        {
            packfs_fdrefs[K]++;
            
            packfs_fileisdir[k] = packfs_fileisdir[K];
            packfs_fd[k]        = fd;
            packfs_fdrefs[k]    = 1;
            packfs_filesize[k]  = packfs_filesize[K];
            packfs_fileino[k]   = packfs_fileino[K];
            packfs_dirent[k]    = packfs_dirent[K];
            packfs_fileptr[k]   = packfs_fileptr[K];
            return fd;
        }
    }
    return -1;
    
}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

FILE* PACKFS_WRAP(fopen)(const char *path, const char *mode)
{
    packfs_init(path, NULL);
    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path) || packfs_path_in_range(packfs_dynamic_prefix, path)))
    {
        FILE* res = packfs_open(path, 0);
        if(res != NULL)
            return res;
    }
    return __real_fopen(path, mode);
}

int PACKFS_WRAP(fileno)(FILE *stream)
{
    packfs_init(NULL, NULL);
    int res = __real_fileno(stream);
    if(packfs_enabled && res < 0)
    {        
        int* ptr = packfs_find(-1, stream);
        res = ptr == NULL ? -1 : (*ptr);
    }
    return res;
}

int PACKFS_WRAP(fclose)(FILE* stream)
{
    packfs_init(NULL, NULL);
    if(packfs_enabled && packfs_find(-1, stream) != NULL)
    {
        int* ptr = packfs_find(-1, stream);
        int fd = ptr == NULL ? -1 : *ptr;
        int res = packfs_close(fd);
        if(res >= -1)
            return res;
    }
    return __real_fclose(stream);
}

int PACKFS_WRAP(open)(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if((flags & O_CREAT) != 0 || (flags & O_TMPFILE) != 0)
    {
        va_list arg;
        va_start(arg, flags);
        mode = va_arg(arg, mode_t);
        va_end(arg);
    }
    
    packfs_init(path, NULL);
    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path) || packfs_path_in_range(packfs_dynamic_prefix, path)))
    {
        void* stream = packfs_open(path, (flags & O_DIRECTORY) != 0);
        if(stream != NULL)
        {
            int* ptr = packfs_find(-1, stream);
            int res = ptr == NULL ? -1 : (*ptr);
            return res;
        }
    }

    return __real_open(path, flags, mode);
}

int PACKFS_WRAP(openat)(int dirfd, const char *path, int flags, ...)
{
    mode_t mode = 0;
    if((flags & O_CREAT) != 0 || (flags & O_TMPFILE) != 0)
    {
        va_list arg;
        va_start(arg, flags);
        mode = va_arg(arg, mode_t);
        va_end(arg);
    }

    packfs_init(path, NULL);
    char path_normalized[packfs_path_max]; packfs_resolve_relative_path(path_normalized, dirfd, path);
    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path_normalized) || packfs_path_in_range(packfs_dynamic_prefix, path_normalized)))
    {
        void* stream = packfs_open(path_normalized, (flags & O_DIRECTORY) != 0);
        if(stream != NULL)
        {
            int* ptr = packfs_find(-1, stream);
            int res = ptr == NULL ? -1 : (*ptr);
            return res;
        }
    }
    
    return __real_openat(dirfd, path, flags, mode);
}

int PACKFS_WRAP(close)(int fd)
{
    packfs_init(NULL, NULL);
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        int res = packfs_close(fd);
        if(res >= -1)
            return res;
    }
    return __real_close(fd);
}

ssize_t PACKFS_WRAP(read)(int fd, void* buf, size_t count)
{
    packfs_init(NULL, NULL);
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        ssize_t res = packfs_read(fd, buf, count);
        if(res >= 0)
            return res;
    }
    return __real_read(fd, buf, count);
}

off_t PACKFS_WRAP(lseek)(int fd, off_t offset, int whence)
{
    packfs_init(NULL, NULL);
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        int res = packfs_seek(fd, (long)offset, whence);
        if(res >= 0)
            return res;
    }
    return __real_lseek(fd, offset, whence);
}

int PACKFS_WRAP(access)(const char *path, int flags)
{
    packfs_init(path, NULL);
    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path) || packfs_path_in_range(packfs_dynamic_prefix, path)))
    {
        int res = packfs_access(path);
        if(res >= -1)
            return res;
    }
    return __real_access(path, flags); 
}

int PACKFS_WRAP(stat)(const char *restrict path, struct stat *restrict statbuf)
{
    packfs_init(path, NULL);
    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path) || packfs_path_in_range(packfs_dynamic_prefix, path)))
    {
        *statbuf = (struct stat){0};
        size_t size = 0, isdir = 0, d_ino = 0;
        int res = packfs_stat(path, -1, &isdir, &size, &d_ino);
        if(res == 0)
        {
            statbuf->st_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->st_size = size;
            statbuf->st_ino = (ino_t)d_ino;
        }
        if(res >= -1)
            return res;
    }

    return __real_stat(path, statbuf);
}

int PACKFS_WRAP(fstat)(int fd, struct stat * statbuf)
{
    packfs_init(NULL, NULL);
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        *statbuf = (struct stat){0};
        size_t size = 0, isdir = 0, d_ino = 0;
        int res = packfs_stat(NULL, fd, &isdir, &size, &d_ino);
        if(res == 0)
        {
            statbuf->st_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->st_size = size;
            statbuf->st_ino = (ino_t)d_ino;
        }
        if(res >= -1)
            return res;
    }
    
    return __real_fstat(fd, statbuf);
}

int PACKFS_WRAP(fstatat)(int dirfd, const char* path, struct stat * statbuf, int flags)
{
    packfs_init(path, NULL);
    char path_normalized[packfs_path_max]; packfs_resolve_relative_path(path_normalized, dirfd, path);

    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path_normalized) || packfs_path_in_range(packfs_dynamic_prefix, path_normalized)))
    {
        *statbuf = (struct stat){0};
        size_t size = 0, isdir = 0, d_ino = 0;
        int res = packfs_stat(path_normalized, -1, &isdir, &size, &d_ino);
        if(res == 0)
        {
            statbuf->st_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->st_size = size;
            statbuf->st_ino = (ino_t)d_ino;
        }

        if(res >= -1)
            return res;
    }
    
    return __real_fstatat(dirfd, path, statbuf, flags);
}

int PACKFS_WRAP(statx)(int dirfd, const char *restrict path, int flags, unsigned int mask, struct statx *restrict statbuf)
{
    packfs_init(path, NULL);
    char path_normalized[packfs_path_max]; packfs_resolve_relative_path(path_normalized, dirfd, path);

    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path_normalized) || packfs_path_in_range(packfs_dynamic_prefix, path_normalized)))
    {
        size_t size = 0, isdir = 0, d_ino = 0;
        int res = packfs_stat(path_normalized, -1, &isdir, &size, &d_ino);
        if(res == 0)
        {
            *statbuf = (struct statx){0};
            statbuf->stx_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->stx_size = size;
            statbuf->stx_ino = d_ino;
        }
        return res;
    }

    return __real_statx(dirfd, path, flags, mask, statbuf);
}

DIR* PACKFS_WRAP(opendir)(const char *path)
{
    packfs_init(path, NULL);
    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path) || packfs_path_in_range(packfs_dynamic_prefix, path)))
    {
        void* stream = packfs_open(path, 1);
        if(stream != NULL)
        {
            int* ptr = packfs_find(-1, stream);
            int fd = ptr == NULL ? -1 : *ptr;
            return (DIR*)stream;
        }
    }
    return __real_opendir(path);
}

DIR* PACKFS_WRAP(fdopendir)(int dirfd)
{
    packfs_init(NULL, NULL);
    if(packfs_enabled && packfs_fd_in_range(dirfd))
    {
        DIR* stream = packfs_find(dirfd, NULL);
        if(stream != NULL)
            return stream;
    }
    return __real_fdopendir(dirfd);
}

struct dirent* PACKFS_WRAP(readdir)(DIR* stream)
{
    packfs_init(NULL, NULL);
    if(packfs_enabled && packfs_find(-1, stream) != NULL)
    {
        int* ptr = packfs_find(-1, stream);
        if(ptr != NULL)
            return (struct dirent*)packfs_readdir(stream);
    }
    return __real_readdir(stream);
}

int PACKFS_WRAP(closedir)(DIR* stream)
{
    packfs_init(NULL, NULL);
    if(packfs_enabled && packfs_find(-1, stream) != NULL)
    {        
        int* ptr = packfs_find(-1, stream);
        int fd = ptr == NULL ? -1 : *ptr;
        int res = packfs_close(fd);
        if(res >= -1)
            return res;
    }
    return __real_closedir(stream);
}

int PACKFS_WRAP(fcntl)(int fd, int action, ...)
{
    int intarg = -1;
    void* ptrarg = NULL;
    char argtype = ' ';
    va_list arg;
    va_start(arg, action);
    switch(action)
    {
        case F_GETFD:
        case F_GETFL:
        case F_GETLEASE:
        case F_GETOWN:
        case F_GETPIPE_SZ:
        case F_GETSIG:
        case F_GET_SEALS: // linux-specific
        {
            argtype = ' ';
            break;
        }
        case F_ADD_SEALS: // linux-specific
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
        case F_NOTIFY:
        case F_SETFD:
        case F_SETFL:
        case F_SETLEASE:
        case F_SETOWN:
        case F_SETPIPE_SZ:
        case F_SETSIG:
        {
            intarg = va_arg(arg, int);
            argtype = '0';
            break;
        }
        default:
        {
            ptrarg = va_arg(arg, void*);
            argtype = '*';
            break;
        }
    }
    va_end(arg);

    packfs_init(NULL, NULL);
    
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        int res = (argtype == '0' && (action == F_DUPFD || action == F_DUPFD_CLOEXEC)) ? packfs_dup(fd, intarg) : -1;
        if(res >= -1)
            return res;
    }
    
    return (argtype == '0' ? __real_fcntl(fd, action, intarg) : argtype == '*' ? __real_fcntl(fd, action, ptrarg) : __real_fcntl(fd, action));
}

int packfs_pack_files_and_fill_offsets(const char* output_path)
{
    int r = 0; 
    FILE* fileptr = fopen(output_path, "w");
    if(!fileptr) { r = 1; fprintf(stderr, "#could not open output file: %s\n", packfs_static_package); return r; }
    size_t offset = 0, size = 0;
    PACKFS_SPLIT(packfs_dynamic_files_paths, packfs_pathsep, begin, i, islast)
    {
        char tmp[1024];
        sprintf(tmp, "%.*s", (int)len, begin);

        size = 0;
        FILE* h = fopen(tmp, "r");
        for(;;)
        {
            size_t bytes_read = fread(tmp, 1, sizeof(tmp), h);
            if(bytes_read == 0)
                break;
            size += bytes_read;
            fwrite(tmp, 1, bytes_read, fileptr);
        }
        fclose(h);
        packfs_dynamic_files_offsets[i] = offset;
        packfs_dynamic_files_sizes[i] = size;
        offset += size;
    }
    fclose(fileptr);
    return r;
}

#ifdef PACKFS_STATIC_PACKER

int packfs_list_files_dirs(const char *path, const struct stat *statptr, int fileflags, struct FTW *pftw)
{
    size_t path_len = strlen(path);

    if(fileflags == FTW_F)
    {
        PACKFS_APPEND_PATH(packfs_dynamic_files_paths, packfs_dynamic_files_paths_len, path, path_len)
        packfs_dynamic_files_num++;
    }
    else if(fileflags == FTW_D)
    {
        PACKFS_APPEND_PATH(packfs_dynamic_dirs_paths, packfs_dynamic_dirs_paths_len, path, path_len)
        if(packfs_dynamic_dirs_paths[packfs_dynamic_dirs_paths_len - 1] != packfs_sep)
            packfs_dynamic_dirs_paths[packfs_dynamic_dirs_paths_len++] = packfs_sep;
        packfs_dynamic_dirs_num++;
    }
    return 0;
}

int packfs_scan_path(const char* input_path, size_t isfile)
{
    int r = 0;
    if(isfile)
    {
        const char* last_slash = strrchr(input_path, packfs_sep);
        size_t basename_offset = last_slash != NULL ? (last_slash - input_path + 1) : 0;
        size_t basename_len = strlen(input_path) - basename_offset;
        PACKFS_APPEND_PATH(packfs_dynamic_files_paths, packfs_dynamic_files_paths_len, input_path + basename_offset, basename_len)
        packfs_dynamic_files_num++;
    }
    else
    {
        int fd_limit = 20;
        int flags = 0;
        r = nftw(input_path, packfs_list_files_dirs, fd_limit, flags);
        if(r != 0) { fprintf(stderr, "#could not list: %s\n", input_path); return r; }
    }
    return r;
}

int main(int argc, const char **argv)
{
    // https://github.com/libarchive/libarchive/issues/2283
    
    const char* input_path = NULL;
    const char* output_path = "packfs.h";
    const char* prefix = packfs_default_prefix;
    const char* ld = "ld";
    const char* include = NULL;
    const char* exclude = NULL;
    int index = 0, object = 0;

    for(int i = 1; i < argc; i++)
    {
        if(0 == strcmp("--input-path", argv[i]))
            input_path = argv[++i];
        else if(0 == strcmp("--output-path", argv[i]))
            output_path = argv[++i];
        else if(0 == strcmp("--prefix", argv[i]))
            prefix = argv[++i];
        else if(0 == strcmp("--ld", argv[i]))
            ld = argv[++i];
        else if(0 == strcmp("--include", argv[i]))
            include = argv[++i];
        else if(0 == strcmp("--exclude", argv[i]))
            exclude = argv[++i];
        else if(0 == strcmp("--index", argv[i]))
            index = 1;
        else if(0 == strcmp("--object", argv[i]))
            object = 1;
    }
    
    int r = 0;
    struct stat path_stat;
    if(input_path == NULL || 0 != stat(input_path, &path_stat)) { r = 1; fprintf(stderr, "Input path not specified or does not exist\n"); return r; }
    int isfile = S_ISREG(path_stat.st_mode), isdir = S_ISDIR(path_stat.st_mode);
    if(!isfile && !isdir) { r = 1; fprintf(stderr, "Input path not file or dir\n"); return r; }
    
    char tmp[1024];
    
    const char* removeprefix = "";
    int removepackage = 0;
    const char* header_path = NULL;
    const char* package_path = NULL;
    
    if(index)
    {
        FILE* fileptr = fopen(output_path, "w");
        if(!fileptr) { r = 1; fprintf(stderr, "#could not open output file: %s\n", output_path); return r; }
        r = packfs_scan_archive(fileptr, input_path, prefix); removeprefix = input_path;
        fclose(fileptr);

        packfs_dump_listing(removeprefix, output_path);

        if(object)
        {
            sprintf(tmp, "%s.h", output_path);

            removepackage = 0;
            package_path = input_path;
            header_path = tmp;
        }
    }
    else if(object)
    {
        r = packfs_scan_path(input_path, isfile); removeprefix = isdir ? input_path : "";
        r = packfs_pack_files_and_fill_offsets(packfs_static_package);

        removepackage = 1;
        package_path = packfs_static_package;
        header_path = output_path;
    }

    if(object)
    {
        r = packfs_dump_static_package(prefix, removeprefix, header_path, ld, package_path);
        fprintf(stderr, "%s\n", package_path);
        fprintf(stderr, "%s.o\n", header_path);

        if(removepackage)
        {
            r = unlink(package_path);
            if(r != 0) { fprintf(stderr, "#could not delete: %s\n", package_path); return r; }
        }
    }
    return r;
}

#endif
