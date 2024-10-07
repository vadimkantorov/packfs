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

#include <dlfcn.h>

#include <archive.h>
#include <archive_entry.h>

enum
{
    packfs_filefd_min = 1000000000, 
    packfs_filefd_max = 1000001000, 
    packfs_entries_name_maxlen = 128, 
    packfs_archive_entries_nummax = 1024,  
    packfs_pathsep = '/'
};

struct packfs_context
{
    int (*orig_open)(const char *path, int flags);
    int (*orig_openat)(int dirfd, const char *path, int flags);
    int (*orig_close)(int fd);
    ssize_t (*orig_read)(int fd, void* buf, size_t count);
    int (*orig_access)(const char *path, int flags);
    off_t (*orig_lseek)(int fd, off_t offset, int whence);
    int (*orig_stat)(const char *restrict path, struct stat *restrict statbuf);
    int (*orig_fstat)(int fd, struct stat * statbuf);
    int (*orig_fstatat)(int dirfd, const char* path, struct stat * statbuf, int flags);
    int (*orig_statx)(int dirfd, const char *restrict path, int flags, unsigned int mask, struct statx *restrict statbuf);
    FILE* (*orig_fopen)(const char *path, const char *mode);
    int (*orig_fclose)(FILE* stream);
    int (*orig_fileno)(FILE* stream);
    DIR* (*orig_opendir)(const char *path);
    DIR* (*orig_fdopendir)(int dirfd);
    struct dirent* (*orig_readdir)(DIR *dirp);
    int (*orig_closedir)(DIR *dirp);
    int (*orig_fcntl)(int fd, int action, ...);
    int (*orig_fchdir)(int fd);
    
    int initialized, disabled;
    
    int packfs_filefd          [packfs_filefd_max - packfs_filefd_min];
    int packfs_fileisdir       [packfs_filefd_max - packfs_filefd_min];
    void* packfs_fileptr       [packfs_filefd_max - packfs_filefd_min];
    size_t packfs_filesize     [packfs_filefd_max - packfs_filefd_min];
    size_t packfs_fileino      [packfs_filefd_max - packfs_filefd_min];
    struct dirent packfs_dirent[packfs_filefd_max - packfs_filefd_min];
    
    size_t packfs_archive_entries_num;
    char packfs_archive_prefix[packfs_entries_name_maxlen];
    void* packfs_archive_fileptr;
    size_t packfs_archive_entries_names_lens[packfs_archive_entries_nummax], packfs_archive_sizes[packfs_archive_entries_nummax];
    char packfs_archive_entries_names[packfs_archive_entries_nummax * packfs_entries_name_maxlen];
    int packfs_archive_entries_isdir[packfs_archive_entries_nummax];
};

void packfs_sanitize_path(char* dest, const char* path)
{
    if(path == NULL)
    {
        strcpy(dest, "");
        return;
    }
    
    const char* newpath = (strlen(path) > 2 && path[0] == '.' && path[1] == packfs_pathsep) ? (path + 2) : path;
    const size_t newpath_len = strlen(newpath);

    strcpy(dest, newpath);

    if(newpath_len >= 3 && newpath[newpath_len - 1] == '.' && newpath[newpath_len - 2] == '.'  && newpath[newpath_len - 3] == packfs_pathsep)
    {
        dest[newpath_len - 3] = '\0';
        char* last_slash = strrchr(dest, packfs_pathsep);
        if(last_slash != NULL)
            *last_slash = '\0';
    }

    return;
}

void packfs_join_path(char* dest, const char* archive_prefix, const char* dirpath, const char* path)
{
    if(strlen(dirpath) > 0)
        sprintf(dest, "%s%c%s%c%s", archive_prefix, (char)packfs_pathsep, dirpath, (char)packfs_pathsep, path);
    else if(0 == strcmp(archive_prefix, path))
        strcpy(dest, path);
    else
        sprintf(dest, "%s%c%s", archive_prefix, (char)packfs_pathsep, path);
}

const char* packfs_basename(const char* path)
{
    const char* last_slash = strrchr(path, packfs_pathsep);
    const char* basename = last_slash ? (last_slash + 1) : path;
    return basename;
}

const char* packfs_lstrip_prefix(const char* path, const char* prefix)
{
    if(path == NULL || prefix == NULL)
        return NULL;
    
    size_t prefix_len = strlen(prefix);

    if(prefix_len == 0)
        return path;

    if(0 == strncmp(prefix, path, prefix_len))
    {
        const char* path_without_prefix = path + prefix_len;
        if(path_without_prefix[0] == packfs_pathsep)
            path_without_prefix++;
        return path_without_prefix;
    }
    
    return NULL;
}

struct archive* packfs_archive_read_new()
{
    struct archive *a = archive_read_new();
    archive_read_support_format_tar(a);
    archive_read_support_format_iso9660(a);
    archive_read_support_format_zip(a);
    return a;
}

size_t packfs_archive_prefix_extract(const char* path)
{
    const char* packfs_archive_suffixes[] = {".tar", ".iso", ".zip"};
    
    if(path == NULL)
        return 0;

    for(const char* res = strchr(path, packfs_pathsep); ; res = strchr(res, packfs_pathsep))
    {
        size_t prefix_len = res == NULL ? strlen(path) : (res - path);
        
        for(size_t i = 0; i < sizeof(packfs_archive_suffixes) / sizeof(packfs_archive_suffixes[0]); i++)
        {
            size_t suffix_len = strlen(packfs_archive_suffixes[i]);
            if(prefix_len >= suffix_len && 0 == strncmp(packfs_archive_suffixes[i], path + prefix_len - suffix_len, suffix_len))
                return prefix_len;
        }

        if(res == NULL)
            break;
        res++;
    }
    return 0;
}

int packfs_fd_in_range(int fd)
{
    return fd >= 0 && fd >= packfs_filefd_min && fd < packfs_filefd_max;
}

int packfs_path_in_range(const char* packfs_archive_prefix, const char* path)
{
    // 1 "/a/b"  "/a/b/c"
    // 0 "/a/b"  "/a/bb"
    // 1 "/a/b"  "/a/b"
    // 1 "/a/b"  "/a/b/"
    // 1 "/a/b/" "/a/b"
    // 0 "/a/bc" "/a/b"

    if(packfs_archive_prefix == NULL || packfs_archive_prefix[0] == '\0' || path == NULL || path[0] == '\0')
        return 0;

    size_t prefix_len = strlen(packfs_archive_prefix);
    size_t path_len = strlen(path);
    int prefix_endswith_slash = packfs_archive_prefix[prefix_len - 1] == packfs_pathsep;
    int prefix_ok = 0 == strncmp(packfs_archive_prefix, path, prefix_len - (prefix_endswith_slash ? 1 : 0));
    size_t prefix_len_m1 = prefix_endswith_slash ? (prefix_len - 1) : prefix_len;

    return prefix_ok && ((path_len == prefix_len_m1) || (path_len >= prefix_len && path[prefix_len_m1] == packfs_pathsep));
}

int packfs_indir(const char* dir_path, const char* path)
{
    size_t dir_path_len = strlen(dir_path);
    const char* last_slash = strrchr(path, packfs_pathsep);
    if(dir_path_len == 0 && last_slash == NULL)
        return 1;

    if(0 == strncmp(dir_path, path, dir_path_len) && last_slash == (path + dir_path_len))
        return 1;
    
    return 0;
}

struct packfs_context* packfs_ensure_context(const char* path)
{
    static struct packfs_context packfs_ctx = {0};

    if(packfs_ctx.initialized != 1)
    {
        packfs_ctx.orig_open      = dlsym(RTLD_NEXT, "open");
        packfs_ctx.orig_openat    = dlsym(RTLD_NEXT, "openat");
        packfs_ctx.orig_read      = dlsym(RTLD_NEXT, "read");
        packfs_ctx.orig_access    = dlsym(RTLD_NEXT, "access");
        packfs_ctx.orig_lseek     = dlsym(RTLD_NEXT, "lseek");
        packfs_ctx.orig_stat      = dlsym(RTLD_NEXT, "stat");
        packfs_ctx.orig_fstat     = dlsym(RTLD_NEXT, "fstat");
        packfs_ctx.orig_fstatat   = dlsym(RTLD_NEXT, "fstatat");
        packfs_ctx.orig_statx     = dlsym(RTLD_NEXT, "statx");
        packfs_ctx.orig_close     = dlsym(RTLD_NEXT, "close");
        packfs_ctx.orig_opendir   = dlsym(RTLD_NEXT, "opendir");
        packfs_ctx.orig_fdopendir = dlsym(RTLD_NEXT, "fdopendir");
        packfs_ctx.orig_readdir   = dlsym(RTLD_NEXT, "readdir");
        packfs_ctx.orig_closedir  = dlsym(RTLD_NEXT, "closedir");
        packfs_ctx.orig_fopen     = dlsym(RTLD_NEXT, "fopen");
        packfs_ctx.orig_fileno    = dlsym(RTLD_NEXT, "fileno");
        packfs_ctx.orig_fclose    = dlsym(RTLD_NEXT, "fclose");
        packfs_ctx.orig_fcntl     = dlsym(RTLD_NEXT, "fcntl");
        packfs_ctx.orig_fchdir    = dlsym(RTLD_NEXT, "fchdir");
        
        strcpy(packfs_ctx.packfs_archive_prefix, "");
        packfs_ctx.packfs_archive_entries_num = 0;
        packfs_ctx.packfs_archive_fileptr = NULL;
        
        packfs_ctx.initialized = 1;
        packfs_ctx.disabled = 1;
    }

    if(packfs_ctx.initialized == 1 && packfs_ctx.disabled == 1)
    {
#ifdef PACKFS_LOG 
        fprintf(stderr, "packfs: enabling\n");
#endif
        const char* packfs_archive_filename = NULL;
        if(path != NULL)
        {
            //const char* path_sanitized = packfs_sanitize_path(path);
            char path_sanitized[packfs_entries_name_maxlen]; packfs_sanitize_path(path_sanitized, path);
            size_t path_prefix_len = packfs_archive_prefix_extract(path_sanitized);
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: path_prefix_len: \"%s\" %zu \n", path_sanitized, path_prefix_len);
#endif
            if(path_prefix_len > 0)
            {
                strcpy(packfs_ctx.packfs_archive_prefix, path_sanitized);
                packfs_ctx.packfs_archive_prefix[path_prefix_len] = '\0';
                packfs_archive_filename = packfs_ctx.packfs_archive_prefix;
            }
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: prefix: \"%s\" ( \"%s\" )\n", packfs_archive_filename, path_sanitized);
#endif
        }

        packfs_ctx.disabled = (packfs_archive_filename != NULL && strlen(packfs_archive_filename) > 0) ? 0 : 1;
#ifdef PACKFS_LOG 
        fprintf(stderr, "packfs: disabled: %d, \"%s\", prefix: \"%s\"\n", packfs_ctx.disabled, packfs_archive_filename, packfs_ctx.packfs_archive_prefix);
#endif
        
        struct archive* a = packfs_archive_read_new();
        struct archive_entry *entry;

        do
        {
            if( packfs_archive_filename == NULL || 0 == strlen(packfs_archive_filename) )// || 0 == strncmp(packfs_ctx.packfs_archive_prefix, packfs_archive_filename, strlen(packfs_ctx.packfs_archive_prefix)))
                break;

            packfs_ctx.packfs_archive_fileptr = fopen(packfs_archive_filename, "rb");

            if(packfs_ctx.packfs_archive_fileptr == NULL)
                break;
            
            if(archive_read_open_FILE(a, packfs_ctx.packfs_archive_fileptr) != ARCHIVE_OK)
                break;
            
            //if(archive_read_open1(a) != ARCHIVE_OK)
            //    break;
            
            strcpy(packfs_ctx.packfs_archive_entries_names, "");
            packfs_ctx.packfs_archive_entries_names_lens[packfs_ctx.packfs_archive_entries_num] = 0;
            packfs_ctx.packfs_archive_entries_isdir[packfs_ctx.packfs_archive_entries_num] = 1;
            packfs_ctx.packfs_archive_entries_num++;
            
            for(size_t entrynames_lens_total = 1; ;)
            {
                int r = archive_read_next_header(a, &entry);
                if (r == ARCHIVE_EOF)
                    break;
                if (r != ARCHIVE_OK)
                    break; //fprintf(stderr, "%s\n", archive_error_string(a));
                    
                int filetype = archive_entry_filetype(entry);
                size_t entry_byte_size = (size_t)archive_entry_size(entry);
                const char* entryname = archive_entry_pathname(entry);
                size_t entryname_len = strlen(entryname);
                
                if(entryname_len > 0 && entryname[entryname_len - 1] == packfs_pathsep)
                    entryname_len--;
                strncpy(packfs_ctx.packfs_archive_entries_names + entrynames_lens_total, entryname, entryname_len);
                packfs_ctx.packfs_archive_entries_names_lens[packfs_ctx.packfs_archive_entries_num] = entryname_len;
                
                packfs_ctx.packfs_archive_entries_isdir[packfs_ctx.packfs_archive_entries_num] = filetype == AE_IFDIR;
                packfs_ctx.packfs_archive_sizes[packfs_ctx.packfs_archive_entries_num] = entry_byte_size;
                
                entrynames_lens_total += packfs_ctx.packfs_archive_entries_names_lens[packfs_ctx.packfs_archive_entries_num] + 1;
                packfs_ctx.packfs_archive_entries_num++;
                    
                r = archive_read_data_skip(a);
                if (r == ARCHIVE_EOF)
                    break;
                if (r != ARCHIVE_OK)
                    break; //fprintf(stderr, "%s\n", archive_error_string(a));
            }
#ifdef PACKFS_LOG
            for(size_t i = 0, packfs_archive_entries_names_offset = 0; i < packfs_ctx.packfs_archive_entries_num; packfs_archive_entries_names_offset += (packfs_ctx.packfs_archive_entries_names_lens[i] + 1), i++)
                fprintf(stderr, "packfs: %s: %s\n", packfs_archive_filename, packfs_ctx.packfs_archive_entries_names + packfs_archive_entries_names_offset);
#endif
        }
        while(0);
        archive_read_close(a);
        archive_read_free(a);
        
    }
    
    return &packfs_ctx;
}

struct dirent* packfs_readdir(struct packfs_context* packfs_ctx, struct dirent* stream)
{
    for(size_t i = 0, packfs_archive_entries_names_offset = 0; i < packfs_ctx->packfs_archive_entries_num; packfs_archive_entries_names_offset += (packfs_ctx->packfs_archive_entries_names_lens[i] + 1), i++)
    {
        const char* path = packfs_ctx->packfs_archive_entries_names + packfs_archive_entries_names_offset;
        const char* dir_entry_name = packfs_ctx->packfs_archive_entries_names + (size_t)stream->d_off;
#ifdef PACKFS_LOG
        fprintf(stderr, "packfs: readdir testing \"%s\" <> \"%s\" %d\n", dir_entry_name, path, packfs_indir(dir_entry_name, path));
#endif
        
        if(i > (size_t)stream->d_ino && packfs_indir(dir_entry_name, path))
        {
            stream->d_type = packfs_ctx->packfs_archive_entries_isdir[i] ? DT_DIR : DT_REG;
            strcpy(stream->d_name, packfs_basename(path));
            stream->d_ino = (ino_t)i;
            return stream;
        }
    }
    
    return NULL;
}

struct dirent* packfs_opendir(struct packfs_context* packfs_ctx, const char* path)
{
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: packfs_opendir: before: \"%s\"\n", path);
#endif
    //const char* path_sanitized = packfs_sanitize_path(path);
    char path_sanitized[packfs_entries_name_maxlen]; packfs_sanitize_path(path_sanitized, path);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: packfs_opendir:  after: \"%s\"\n", path_sanitized);
#endif

    const char* path_without_prefix = packfs_lstrip_prefix(path_sanitized, packfs_ctx->packfs_archive_prefix);

    struct dirent* fileptr = NULL;
    
    size_t d_ino, d_off;
    int found = 0;
    
    if(packfs_ctx->packfs_archive_entries_num > 0 && path_without_prefix != NULL)
    {
        for(size_t i = 0, packfs_archive_entries_names_offset = 0; i < packfs_ctx->packfs_archive_entries_num; packfs_archive_entries_names_offset += (packfs_ctx->packfs_archive_entries_names_lens[i] + 1), i++)
        {
            const char* entry_path = packfs_ctx->packfs_archive_entries_names + packfs_archive_entries_names_offset;
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: testing \"%s\" <> \"%s\"\n", path_without_prefix, entry_path);
#endif
            if(packfs_ctx->packfs_archive_entries_isdir[i] && 0 == strcmp(entry_path, path_without_prefix))
            {
#ifdef PACKFS_LOG
                fprintf(stderr, "packfs: testing \"%s\" <> \"%s\" 1\n", path_without_prefix, entry_path);
#endif
                d_ino = i;
                d_off = packfs_archive_entries_names_offset;
                found = 1;
                break;
            }
        }
    }
    
    for(size_t k = 0; found && k < packfs_filefd_max - packfs_filefd_min; k++)
    {
        if(packfs_ctx->packfs_filefd[k] == 0)
        {
            struct dirent* fileptr = &packfs_ctx->packfs_dirent[k];
            *fileptr = (struct dirent){0};
            fileptr->d_ino = (ino_t)d_ino;
            fileptr->d_off = (off_t)d_off;

            packfs_ctx->packfs_fileisdir[k] = 1;
            packfs_ctx->packfs_filefd[k] = packfs_filefd_min + k;
            packfs_ctx->packfs_filesize[k] = 0;
            packfs_ctx->packfs_fileino[k] = d_ino;
            packfs_ctx->packfs_fileptr[k] = (void*)fileptr;
            return fileptr;
        }
    }

    return NULL;
}

FILE* packfs_open(struct packfs_context* packfs_ctx, const char* path)
{
    //const char* path_sanitized = packfs_sanitize_path(path);
    char path_sanitized[packfs_entries_name_maxlen]; packfs_sanitize_path(path_sanitized, path);
    const char* path_without_prefix = packfs_lstrip_prefix(path_sanitized, packfs_ctx->packfs_archive_prefix);

    FILE* fileptr = NULL;
    size_t filesize = 0;
    size_t fileino = 0;
    
    if(packfs_ctx->packfs_archive_entries_num > 0 && path_without_prefix != NULL)
    {
        for(size_t i = 0, packfs_archive_entries_names_offset = 0; i < packfs_ctx->packfs_archive_entries_num; packfs_archive_entries_names_offset += (packfs_ctx->packfs_archive_entries_names_lens[i] + 1), i++)
        {
            const char* entry_path = packfs_ctx->packfs_archive_entries_names + packfs_archive_entries_names_offset;
            if(0 == strcmp(entry_path, path_without_prefix))
            {
                filesize = packfs_ctx->packfs_archive_sizes[i];
                fileptr = fmemopen(NULL, filesize, "rb+");
                fileino = i;
    
                struct archive* a = packfs_archive_read_new();
                struct archive_entry *entry;
                do
                {
                    fseek(packfs_ctx->packfs_archive_fileptr, 0, SEEK_SET);
                    if(archive_read_open_FILE(a, packfs_ctx->packfs_archive_fileptr) != ARCHIVE_OK)
                        break;
                    
                    while (1)
                    {
                        int r = archive_read_next_header(a, &entry);
                        if (r == ARCHIVE_EOF)
                            break;
                        if (r != ARCHIVE_OK)
                            break; //fprintf(stderr, "%s\n", archive_error_string(a));

                        if(0 == strcmp(entry_path, archive_entry_pathname(entry)))
                        {
                            enum { MAX_WRITE = 1024 * 1024};
                            const void *buff;
                            size_t size;
                            off_t offset;

                            while ((r = archive_read_data_block(a, &buff, &size, &offset)) == ARCHIVE_OK)
                            {
                                // assert(offset <= output_offset), do not support sparse files just yet, https://github.com/libarchive/libarchive/issues/2299
                                const char* p = buff;
                                while (size > 0)
                                {
                                    ssize_t bytes_written = fwrite(p, 1, size, fileptr);
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

                fseek(fileptr, 0, SEEK_SET);
                break;
            }
        }
    }
    
    for(size_t k = 0; fileptr != NULL && k < packfs_filefd_max - packfs_filefd_min; k++)
    {
        if(packfs_ctx->packfs_filefd[k] == 0)
        {
            packfs_ctx->packfs_fileisdir[k] = 0;
            packfs_ctx->packfs_filefd[k] = packfs_filefd_min + k;
            packfs_ctx->packfs_fileptr[k] = fileptr;
            packfs_ctx->packfs_filesize[k] = filesize;
            packfs_ctx->packfs_fileino[k] = fileino;
            return fileptr;
        }
    }

    return NULL;
}

int packfs_close(struct packfs_context* packfs_ctx, int fd)
{
    if(fd < packfs_filefd_min || fd >= packfs_filefd_max)
        return -2;

#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: closing: %d\n", fd);
#endif

    for(size_t k = 0; k < packfs_filefd_max - packfs_filefd_min; k++)
    {
        if(packfs_ctx->packfs_filefd[k] == fd)
        {
            if(packfs_ctx->packfs_fileisdir[k])
            {
#ifdef PACKFS_LOG
                fprintf(stderr, "packfs: closed fakely dir: %d\n", fd);
#endif
                return 0;//-1;
            }

            int res = (!packfs_ctx->packfs_fileisdir[k]) ? packfs_ctx->orig_fclose(packfs_ctx->packfs_fileptr[k]) : 0;
            packfs_ctx->packfs_fileisdir[k] = 0;
            packfs_ctx->packfs_filefd[k] = 0;
            packfs_ctx->packfs_filesize[k] = 0;
            packfs_ctx->packfs_fileptr[k] = NULL;
            packfs_ctx->packfs_fileino[k] = 0;
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: closed: %d\n", fd);
#endif
            return res;
        }
    }
    return -1;
}

void* packfs_find(struct packfs_context* packfs_ctx, int fd, void* ptr)
{
    if(ptr != NULL)
    {
        for(size_t k = 0; k < packfs_filefd_max - packfs_filefd_min; k++)
        {
            if(packfs_ctx->packfs_fileptr[k] == ptr)
                return &packfs_ctx->packfs_filefd[k];
        }
        return NULL;
    }
    else
    {
        if(fd < packfs_filefd_min || fd >= packfs_filefd_max)
            return NULL;
        
        for(size_t k = 0; k < packfs_filefd_max - packfs_filefd_min; k++)
        {
            if(packfs_ctx->packfs_filefd[k] == fd)
                return packfs_ctx->packfs_fileptr[k];
        }
    }
    return NULL;
}

ssize_t packfs_read(struct packfs_context* packfs_ctx, int fd, void* buf, size_t count)
{
    FILE* ptr = packfs_find(packfs_ctx, fd, NULL);
    if(!ptr)
        return -1;
    return (ssize_t)fread(buf, 1, count, ptr);
}

int packfs_seek(struct packfs_context* packfs_ctx, int fd, long offset, int whence)
{
    FILE* ptr = packfs_find(packfs_ctx, fd, NULL);
    if(!ptr)
        return -1;
    return fseek(ptr, offset, whence);
}

int packfs_access(struct packfs_context* packfs_ctx, const char* path)
{
    //const char* path_sanitized = packfs_sanitize_path(path);
    char path_sanitized[packfs_entries_name_maxlen]; packfs_sanitize_path(path_sanitized, path);
    const char* path_without_prefix = packfs_lstrip_prefix(path_sanitized, packfs_ctx->packfs_archive_prefix);

    if(path_without_prefix != NULL)
    {
        for(size_t i = 0, packfs_archive_entries_names_offset = 0, prefix_strlen = strlen(packfs_ctx->packfs_archive_prefix); i < packfs_ctx->packfs_archive_entries_num; packfs_archive_entries_names_offset += (packfs_ctx->packfs_archive_entries_names_lens[i] + 1), i++)
        {
            const char* entry_path = packfs_ctx->packfs_archive_entries_names + packfs_archive_entries_names_offset;
            if(0 == strcmp(path_without_prefix, entry_path))
                return 0;
        }
        return -1;
    }
    
    return -2;
}

int packfs_stat(struct packfs_context* packfs_ctx, const char* path, int fd, size_t* isdir, size_t* size, size_t* d_ino)
{
    //const char* path_sanitized = packfs_sanitize_path(path);
    char path_sanitized[packfs_entries_name_maxlen]; packfs_sanitize_path(path_sanitized, path);
    const char* path_without_prefix = packfs_lstrip_prefix(path_sanitized, packfs_ctx->packfs_archive_prefix);
    
    if(path_without_prefix != NULL)
    {
        for(size_t i = 0, packfs_archive_entries_names_offset = 0; i < packfs_ctx->packfs_archive_entries_num; packfs_archive_entries_names_offset += (packfs_ctx->packfs_archive_entries_names_lens[i] + 1), i++)
        {
            const char* entry_path = packfs_ctx->packfs_archive_entries_names + packfs_archive_entries_names_offset;
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: testing \"%s\" <> \"%s\"\n", path_without_prefix, entry_path);
#endif
            if(0 == strcmp(path_without_prefix, entry_path))
            {
                *size = packfs_ctx->packfs_archive_sizes[i];
                *isdir = packfs_ctx->packfs_archive_entries_isdir[i];
                *d_ino = i;
                return 0;
            }
        }
        return -1;
    }
    
    if(fd >= 0 && packfs_filefd_min <= fd && fd < packfs_filefd_max)
    {
        for(size_t k = 0; k < packfs_filefd_max - packfs_filefd_min; k++)
        {
            if(packfs_ctx->packfs_filefd[k] == fd)
            {
                *size = packfs_ctx->packfs_filesize[k];
                *isdir = packfs_ctx->packfs_fileisdir[k];
                *d_ino = packfs_ctx->packfs_fileino[k];
                return 0;
            }
        }
        return -1;
    }

    return -2;
}

///////////

FILE* fopen(const char *path, const char *mode)
{
    struct packfs_context* packfs_ctx = packfs_ensure_context(path);
    if(!packfs_ctx->disabled)
    {
        FILE* res = packfs_open(packfs_ctx, path);
        if(res != NULL)
        {
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: Fopen(\"%s\", \"%s\") == %p\n", path, mode, (void*)res);
#endif
            return res;
        }
    }

    FILE* res = packfs_ctx->orig_fopen(path, mode);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: fopen(\"%s\", \"%s\") == %p\n", path, mode, (void*)res);
#endif
    return res;
}

int fileno(FILE *stream)
{
    struct packfs_context* packfs_ctx = packfs_ensure_context(NULL);
    
    int res = packfs_ctx->orig_fileno(stream);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: fileno(%p) == %d\n", (void*)stream, res);
#endif
    
    if(!packfs_ctx->disabled && res < 0)
    {        
        int* ptr = packfs_find(packfs_ctx, -1, stream);
        res = ptr == NULL ? -1 : (*ptr);
#ifdef PACKFS_LOG
        fprintf(stderr, "packfs: Fileno(%p) == %d\n", (void*)stream, res);
#endif
    }
    
    return res;
}

int fclose(FILE* stream)
{
    struct packfs_context* packfs_ctx = packfs_ensure_context(NULL);
    if(!packfs_ctx->disabled)
    {
        int* ptr = packfs_find(packfs_ctx, -1, stream);
        int fd = ptr == NULL ? -1 : *ptr;
        int res = packfs_close(packfs_ctx, fd);
        if(res >= -1)
        {
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: Fclose(%p) == %d\n", (void*)stream, res);
#endif
            return res;
        }
    }

    int res = packfs_ctx->orig_fclose(stream);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: fclose(%p) == %d\n", (void*)stream, res);
#endif
    return res;
}


int open(const char *path, int flags, ...)
{
    struct packfs_context* packfs_ctx = packfs_ensure_context(path);
    if(!packfs_ctx->disabled)
    {
        //If neither O_CREAT nor O_TMPFILE is specified in flags, then mode is ignored (and can thus be specified as 0, or simply omitted)
        void* stream = ((flags & O_DIRECTORY) != 0) ? (void*)packfs_opendir(packfs_ctx, path) : (void*)packfs_open(packfs_ctx, path);
        if(stream != NULL)
        {
            int* ptr = packfs_find(packfs_ctx, -1, stream);
            int res = ptr == NULL ? -1 : (*ptr);
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: Open(\"%s\", %d) == %d\n", path, flags, res);
#endif
            return res;
        }
    }
    
    int res = packfs_ctx->orig_open(path, flags);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: open(\"%s\", %d) == %d\n", path, flags, res);
#endif
    return res;
}

int openat(int dirfd, const char *path, int flags, ...)
{
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: openat: enter(%d, \"%s\", %d)\n", dirfd, path, flags);
#endif
    
    struct packfs_context* packfs_ctx = packfs_ensure_context(path);
    if(!packfs_ctx->disabled)
    {
        struct dirent* ptr = dirfd != AT_FDCWD ? packfs_find(packfs_ctx, dirfd, NULL) : NULL;
        const char* dir_entry_name = ptr != NULL ? (packfs_ctx->packfs_archive_entries_names + (size_t)ptr->d_off) : "";
        char buf[2 * packfs_entries_name_maxlen] = ""; packfs_join_path(buf, packfs_ctx->packfs_archive_prefix, dir_entry_name, path); path = buf;
        
        void* stream = ((flags & O_DIRECTORY) != 0) ? (void*)packfs_opendir(packfs_ctx, path) : (void*)packfs_open(packfs_ctx, path);
        if(stream != NULL)
        {
            int* ptr = packfs_find(packfs_ctx, -1, stream);
            int res = ptr == NULL ? -1 : (*ptr);
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: Openat(%d, \"%s\", %d) == %d\n", dirfd, path, flags, res);
#endif
            return res;
        }
    }
    
    int res = packfs_ctx->orig_openat(dirfd, path, flags);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: openat(%d, \"%s\", %d) == %d\n", dirfd, path, flags, res);
#endif
    return res;
}

int close(int fd)
{
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: close enter: %d\n", (int)errno);
#endif
    
    struct packfs_context* packfs_ctx = packfs_ensure_context(NULL);
    if(!packfs_ctx->disabled)
    {
        int res = packfs_close(packfs_ctx, fd);
        if(res >= -1)
        {
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: Close(%d) == %d / %d\n", fd, res, (int)errno);
#endif
            return res;
        }
    }
    
    int res = packfs_ctx->orig_close(fd);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: close(%d) == %d\n", fd, res);
#endif
    return res;
}


ssize_t read(int fd, void* buf, size_t count)
{
    struct packfs_context* packfs_ctx = packfs_ensure_context(NULL);
    if(!packfs_ctx->disabled)
    {
        ssize_t res = packfs_read(packfs_ctx, fd, buf, count);
        if(res >= 0)
        {
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: Read(%d, %p, %zu) == %d\n", fd, buf, count, (int)res);
#endif
            return res;
        }
    }

    ssize_t res = packfs_ctx->orig_read(fd, buf, count);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: read(%d, %p, %zu) == %d\n", fd, buf, count, (int)res);
#endif
    return res;
}

off_t lseek(int fd, off_t offset, int whence)
{
    struct packfs_context* packfs_ctx = packfs_ensure_context(NULL);
    if(!packfs_ctx->disabled)
    {
        int res = packfs_seek(packfs_ctx, fd, (long)offset, whence);
        if(res >= 0)
        {
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: Seek(%d, %d, %d) == %d\n", fd, (int)offset, whence, (int)res);
#endif
            return res;
        }
    }

    off_t res = packfs_ctx->orig_lseek(fd, offset, whence);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: seek(%d, %d, %d) == %d\n", fd, (int)offset, whence, (int)res);
#endif
    return res;
}


int access(const char *path, int flags) 
{
    struct packfs_context* packfs_ctx = packfs_ensure_context(NULL);
    if(!packfs_ctx->disabled)
    {
        int res = packfs_access(packfs_ctx, path);
        if(res >= -1)
        {
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: Access(\"%s\", %d) == %d\n", path, flags, res);
#endif
            return res;
        }
    }
    
    int res = packfs_ctx->orig_access(path, flags); 
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: access(\"%s\", %d) == %d\n", path, flags, res);
#endif
    return res;
}

int stat(const char *restrict path, struct stat *restrict statbuf)
{
    struct packfs_context* packfs_ctx = packfs_ensure_context(NULL);
    if(!packfs_ctx->disabled)
    {
        *statbuf = (struct stat){0};
        size_t size, isdir, d_ino;
        int res = packfs_stat(packfs_ctx, path, -1, &isdir, &size, &d_ino);
        if(res == 0)
        {
            statbuf->st_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->st_size = size;
            statbuf->st_ino = (ino_t)d_ino;
        }
        if(res >= -1)
        {
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: Stat(\"%s\", %p) == %d\n", path, (void*)statbuf, res);
#endif
            return res;
        }
    }

    int res = packfs_ctx->orig_stat(path, statbuf);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: stat(\"%s\", %p) == %d\n", path, (void*)statbuf, res);
#endif
    return res;
}

int fstat(int fd, struct stat * statbuf)
{
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: Fstat enter: %d / %d\n", fd, (int)errno);
#endif
    struct packfs_context* packfs_ctx = packfs_ensure_context(NULL);
    if(!packfs_ctx->disabled)
    {
        *statbuf = (struct stat){0};
        size_t size, isdir, d_ino;
        int res = packfs_stat(packfs_ctx, NULL, fd, &isdir, &size, &d_ino);
        if(res == 0)
        {
            statbuf->st_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->st_size = size;
            statbuf->st_ino = (ino_t)d_ino;
        }
        if(res >= -1)
        {
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: Fstat(%d, %p) == %d / %d\n", fd, (void*)statbuf, res, (int)errno);
#endif
            return res;
        }
    }
    
    int res = packfs_ctx->orig_fstat(fd, statbuf);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: fstat(%d, %p) == %d\n", fd, (void*)statbuf, res);
#endif
    return res;
}

int fstatat(int dirfd, const char* path, struct stat * statbuf, int flags)
{
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: Fstatat enter: %d / \"%s\"\n", dirfd, path);
#endif
    struct packfs_context* packfs_ctx = packfs_ensure_context(path);
    if(!packfs_ctx->disabled)
    {
        struct dirent* ptr = dirfd != AT_FDCWD ? packfs_find(packfs_ctx, dirfd, NULL) : NULL;
        const char* dir_entry_name = ptr != NULL ? (packfs_ctx->packfs_archive_entries_names + (size_t)ptr->d_off) : "";
        char buf[2 * packfs_entries_name_maxlen] = ""; packfs_join_path(buf, packfs_ctx->packfs_archive_prefix, dir_entry_name, path); path = buf;

#ifdef PACKFS_LOG
        fprintf(stderr, "packfs: Fstatat: %d / \"%s\"\n", dirfd, path);
#endif

        *statbuf = (struct stat){0};
        size_t size, isdir, d_ino;
        int res = packfs_stat(packfs_ctx, path, -1, &isdir, &size, &d_ino);
        if(res == 0)
        {
            statbuf->st_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->st_size = size;
            statbuf->st_ino = (ino_t)d_ino;
        }

#ifdef PACKFS_LOG
        fprintf(stderr, "packfs: Fstatat(%d, \"%s\", %p, %d) == %d. ISDIR: %zu\n", dirfd, path, (void*)statbuf, flags, res, isdir);
#endif
        if(res >= -1)
        {
            return res;
        }
    }
    
    int res = packfs_ctx->orig_fstatat(dirfd, path, statbuf, flags);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: fstatat(%d, \"%s\", %p, %d) == %d\n", dirfd, path, (void*)statbuf, flags, res);
#endif
    return res;
}

int statx(int dirfd, const char *restrict path, int flags, unsigned int mask, struct statx *restrict statbuf)
{
    struct packfs_context* packfs_ctx = packfs_ensure_context(path);
    if(!packfs_ctx->disabled)
    {
        size_t size, isdir, d_ino;
        int res = packfs_stat(packfs_ctx, path, -1, &isdir, &size, &d_ino);
        if(res == 0)
        {
            *statbuf = (struct statx){0};
            statbuf->stx_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->stx_size = size;
            statbuf->stx_ino = d_ino;
        }
#ifdef PACKFS_LOG
        fprintf(stderr, "packfs: Statx(%d, \"%s\", %d, %u, %p) == %d\n", dirfd, path, flags, mask, (void*)statbuf, res);
#endif
        return res;
    }

    int res = packfs_ctx->orig_statx(dirfd, path, flags, mask, statbuf);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: statx(%d, \"%s\", %d, %u, %p) == %d\n", dirfd, path, flags, mask, (void*)statbuf, res);
#endif
    return res;
}

DIR* opendir(const char *path)
{
    struct packfs_context* packfs_ctx = packfs_ensure_context(path);
    if(!packfs_ctx->disabled)
    {
        struct dirent* stream = packfs_opendir(packfs_ctx, path);
        if(stream != NULL)
        {
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: Opendir(\"%s\") == %p\n", path, (void*)stream);
#endif
            return (DIR*)stream;
        }
    }
    
    DIR* res = packfs_ctx->orig_opendir(path);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: opendir(\"%s\") == %p\n", path, (void*)res);
#endif
    return res;
}


DIR* fdopendir(int dirfd)
{
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: Fdopendir enter: %d\n", dirfd);
#endif
    struct packfs_context* packfs_ctx = packfs_ensure_context(NULL);
    if(!packfs_ctx->disabled)
    {
        DIR* stream = packfs_find(packfs_ctx, dirfd, NULL);
        if(stream != NULL)
        {
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: Fdopendir(%d) == %p\n", dirfd, (void*)stream);
#endif
            return stream;
        }
    }
    
    DIR* res = packfs_ctx->orig_fdopendir(dirfd);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: fdopendir(%d) == %p\n", dirfd, (void*)res);
#endif
    return res;
}

struct dirent* readdir(DIR* stream)
{
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: Readdir enter: %p\n", (void*)stream);
#endif
    struct packfs_context* packfs_ctx = packfs_ensure_context(NULL);
    if(!packfs_ctx->disabled)
    {
        int* ptr = packfs_find(packfs_ctx, -1, stream);
        if(ptr != NULL)
            return packfs_readdir(packfs_ctx, (struct dirent*)stream);
    }
    
    struct dirent* res = packfs_ctx->orig_readdir(stream);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: readdir(%p) == %p\n", (void*)stream, (void*)res);
#endif
    return res;
}

int closedir(DIR* stream)
{
    struct packfs_context* packfs_ctx = packfs_ensure_context(NULL);
    
    if(!packfs_ctx->disabled)
    {        
        int* ptr = packfs_find(packfs_ctx, -1, stream);
        int fd = ptr == NULL ? -1 : *ptr;
        int res = packfs_close(packfs_ctx, fd);
        if(res >= -1)
        {
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: Closedir(%p) == %d\n", (void*)stream, res);
#endif
            return res;
        }
    }

    int res = packfs_ctx->orig_closedir(stream);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: closedir(%p) == %d\n", (void*)stream, res);
#endif
    
    return res;
}

int packfs_dup(struct packfs_context* packfs_ctx, int oldfd, int newfd)
{
    return oldfd;
}

int fcntl(int fd, int action, ...)
{
    // https://github.com/coreutils/gnulib/blob/master/lib/fcntl.c
    // https://git.savannah.gnu.org/gitweb/?p=gnulib.git;a=blob;f=lib/fcntl.c
    // https://man7.org/linux/man-pages/man2/fcntl.2.html
    int intarg = -1;
    void* ptrarg = NULL;
    int argtype = 0;

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
            argtype = 0;
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
            break;
        }
        default:
        {
            ptrarg = va_arg(arg, void*);
            argtype = -1;
            break;
        }
    }
    va_end(arg);

    struct packfs_context* packfs_ctx = packfs_ensure_context(NULL);
    
    if(!packfs_ctx->disabled)
    {
        int res = (action == F_DUPFD || action == F_DUPFD_CLOEXEC) ? packfs_dup(packfs_ctx, fd, intarg) : -1;
        if(res >= -1)
        {
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: Fcntl(%d, %d, ...) == %d\n", fd, action, res);
#endif
            return res;
        }
    }
    
    int res = argtype == 1 ? packfs_ctx->orig_fcntl(fd, action, intarg) : arg_type == -1 ? packfs_ctx->orig_fcntl(fd, action, ptrarg) : packfs_ctx->orig_fcntl(fd, action);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: fcntl(%d, %d, ...) == %d\n", fd, action, res);
#endif
    return res;
}

int fchdir(int fd)
{
    struct packfs_context* packfs_ctx = packfs_ensure_context(NULL);

    int res = packfs_ctx->orig_fchdir(fd);
#ifdef PACKFS_LOG
    fprintf(stderr, "packfs: fchdir(%d) == %d\n", fd, res);
#endif
    return res;
}
