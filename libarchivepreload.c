#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <dlfcn.h>

#include <archive.h>
#include <archive_entry.h>
// https://github.com/libarchive/libarchive/issues/2295
// define required for #include <archive_read_private.h>
#define __LIBARCHIVE_BUILD
#include <archive_read_private.h>
void* last_file_buff; size_t last_file_block_size; size_t last_file_offset; archive_read_callback* old_file_read; archive_seek_callback* old_file_seek;
static ssize_t new_file_read(struct archive *a, void *client_data, const void **buff)
{
    // struct read_file_data copied from https://github.com/libarchive/libarchive/blob/master/libarchive/archive_read_open_filename.c
    struct read_file_data {int fd; size_t block_size; void* buffer;} *mine = client_data;
    last_file_buff = mine->buffer;
    last_file_block_size = mine->block_size;
    last_file_offset = old_file_seek(a, client_data, 0, SEEK_CUR);
    return old_file_read(a, client_data, buff);
}

enum {
    packfs_filefd_min = 1000000000, 
    packfs_filefd_max = 1000001000, 
    packfs_filepath_max_len = 128, 
    packfs_archive_filenames_num = 1024,  
    packfs_archive_use_mmap = 0
};
struct packfs_context
{
    int initialized, disabled;
    
    int (*orig_open)(const char *path, int flags);
    int (*orig_close)(int fd);
    ssize_t (*orig_read)(int fd, void* buf, size_t count);
    int (*orig_access)(const char *path, int flags);
    off_t (*orig_lseek)(int fd, off_t offset, int whence);
    int (*orig_stat)(const char *restrict path, struct stat *restrict statbuf);
    int (*orig_fstat)(int fd, struct stat * statbuf);
    int (*orig_statx)(int dirfd, const char *restrict path, int flags, unsigned int mask, struct statx *restrict statbuf);
    FILE* (*orig_fopen)(const char *path, const char *mode);
    int (*orig_fileno)(FILE* stream);
    
    int packfs_filefd[packfs_filefd_max - packfs_filefd_min];
    FILE* packfs_fileptr[packfs_filefd_max - packfs_filefd_min];
    size_t packfs_filesize[packfs_filefd_max - packfs_filefd_min];
    
    size_t packfs_archive_files_num;
    char packfs_archive_prefix[packfs_filepath_max_len];
    void* packfs_archive_fileptr;
    size_t packfs_archive_mmapsize;
    size_t packfs_archive_filenames_lens[packfs_archive_filenames_num], packfs_archive_offsets[packfs_archive_filenames_num], packfs_archive_sizes[packfs_archive_filenames_num];
    char packfs_archive_filenames[packfs_archive_filenames_num * packfs_filepath_max_len];
};

const char packfs_pathsep = '/';

const char* packfs_sanitize_path(const char* path)
{
    return (path != NULL && strlen(path) > 2 && path[0] == '.' && path[1] == packfs_pathsep) ? (path + 2) : path;
}

int packfs_strncmp(const char* prefix, const char* path, size_t count)
{
    return (prefix != NULL && prefix[0] != '\0' && path != NULL && path[0] != '\0') ? strncmp(prefix, path, count) : 1;
}

size_t packfs_archive_prefix_extract(const char* path)
{
    const char* packfs_archive_suffixes[] = {".tar", ".zip", ".iso"};
    
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


struct packfs_context* packfs_ensure_context(const char* path)
{
    static struct packfs_context packfs_ctx = {0};

    if(packfs_ctx.initialized != 1)
    {
        packfs_ctx.orig_open   = dlsym(RTLD_NEXT, "open");
        packfs_ctx.orig_close  = dlsym(RTLD_NEXT, "close");
        packfs_ctx.orig_read   = dlsym(RTLD_NEXT, "read");
        packfs_ctx.orig_access = dlsym(RTLD_NEXT, "access");
        packfs_ctx.orig_lseek  = dlsym(RTLD_NEXT, "lseek");
        packfs_ctx.orig_stat   = dlsym(RTLD_NEXT, "stat");
        packfs_ctx.orig_fstat  = dlsym(RTLD_NEXT, "fstat");
        packfs_ctx.orig_statx  = dlsym(RTLD_NEXT, "statx");
        packfs_ctx.orig_fopen  = dlsym(RTLD_NEXT, "fopen");
        packfs_ctx.orig_fileno = dlsym(RTLD_NEXT, "fileno");
        
#define PACKFS_STRING_VALUE_(x) #x
#define PACKFS_STRING_VALUE(x) PACKFS_STRING_VALUE_(x)
        strcpy(packfs_ctx.packfs_archive_prefix, 
#ifdef PACKFS_ARCHIVE_PREFIX
            PACKFS_STRING_VALUE(PACKFS_ARCHIVE_PREFIX)
#else
        ""
#endif
        );
#undef PACKFS_STRING_VALUE
#undef PACKFS_STRING_VALUE_

        packfs_ctx.packfs_archive_files_num = 0;
        packfs_ctx.packfs_archive_mmapsize = 0;
        packfs_ctx.packfs_archive_fileptr = NULL;
        
        packfs_ctx.initialized = 1;
        packfs_ctx.disabled = 1;
    }

    if(packfs_ctx.initialized == 1 && packfs_ctx.disabled == 1)
    {
#ifdef PACKFS_LOG 
            fprintf(stderr, "packfs: enabling\n");
#endif
        const char* packfs_archive_filename = getenv("LIBARCHIVEPRELOAD");
        if(packfs_archive_filename == NULL)
        {
            path = packfs_sanitize_path(path);
            size_t path_prefix_len = packfs_archive_prefix_extract(path);
            if(path_prefix_len > 0)
            {
                strcpy(packfs_ctx.packfs_archive_prefix, path);
                packfs_ctx.packfs_archive_prefix[path_prefix_len] = '\0';
                packfs_archive_filename = packfs_ctx.packfs_archive_prefix;
            }
#ifdef PACKFS_LOG 
            fprintf(stderr, "packfs: %s ( %s )\n", packfs_archive_filename, path);
#endif
        }
        
        packfs_ctx.disabled = (packfs_archive_filename != NULL && strlen(packfs_archive_filename) > 0) ? 0 : 1;
#ifdef PACKFS_LOG 
        fprintf(stderr, "packfs: disabled: %d, %s, prefix: %s\n", packfs_ctx.disabled, packfs_archive_filename, packfs_ctx.packfs_archive_prefix);
#endif
        
        struct archive *a = archive_read_new();
        archive_read_support_format_tar(a);
        archive_read_support_format_iso9660(a);
        archive_read_support_format_zip(a);
        struct archive_entry *entry;
        
        do
        {
            if( packfs_archive_filename == NULL || 0 == strlen(packfs_archive_filename) )// || 0 == strncmp(packfs_ctx.packfs_archive_prefix, packfs_archive_filename, strlen(packfs_ctx.packfs_archive_prefix)))
                break;
            
            int fd = open(packfs_archive_filename, O_RDONLY);
            struct stat statbufobj = {0}; 
            if(fd >= 0 && fstat(fd, &statbufobj) >= 0)
            {
                if(packfs_archive_use_mmap)
                {
                    packfs_ctx.packfs_archive_mmapsize = statbufobj.st_size;
                    packfs_ctx.packfs_archive_fileptr = mmap(NULL, packfs_ctx.packfs_archive_mmapsize, PROT_READ, MAP_PRIVATE, fd, 0);
                }
                else
                {
                    packfs_ctx.packfs_archive_mmapsize = 0;
                    packfs_ctx.packfs_archive_fileptr = fopen(packfs_archive_filename, "rb");
                }
            }
            close(fd);

            if(packfs_ctx.packfs_archive_fileptr == NULL)
                break;

            if(archive_read_open_filename(a, packfs_archive_filename, 10240) != ARCHIVE_OK)
                break;
            
            // struct archive_read in https://github.com/libarchive/libarchive/blob/master/libarchive/archive_read_private.h
            struct archive_read *_a = ((struct archive_read *)a);
            old_file_read = _a->client.reader;
            old_file_seek = _a->client.seeker;
            a->state = ARCHIVE_STATE_NEW;
            archive_read_set_read_callback(a, new_file_read);

            if(archive_read_open1(a) != ARCHIVE_OK)
                break;
            
            size_t filenames_lens_total = 0;
            while(1)
            {
                int r = archive_read_next_header(a, &entry);
                if (r == ARCHIVE_EOF)
                    break;
                if (r != ARCHIVE_OK)
                    break; //fprintf(stderr, "%s\n", archive_error_string(a));

                const void* firstblock_buff;
                size_t firstblock_len;
                int64_t firstblock_offset;
                r = archive_read_data_block(a, &firstblock_buff, &firstblock_len, &firstblock_offset);
                int filetype = archive_entry_filetype(entry);
                if(filetype == AE_IFREG && archive_entry_size_is_set(entry) != 0 && last_file_buff != NULL && last_file_buff <= firstblock_buff && firstblock_buff < last_file_buff + last_file_block_size)
                {
                    size_t entry_byte_size = (size_t)archive_entry_size(entry);
                    size_t entry_byte_offset = last_file_offset + (size_t)(firstblock_buff - last_file_buff);
                    const char* entryname = archive_entry_pathname(entry);
                    strcpy(packfs_ctx.packfs_archive_filenames + filenames_lens_total, entryname);
                    packfs_ctx.packfs_archive_filenames_lens[packfs_ctx.packfs_archive_files_num] = strlen(entryname);
                    packfs_ctx.packfs_archive_offsets[packfs_ctx.packfs_archive_files_num] = entry_byte_offset;
                    packfs_ctx.packfs_archive_sizes[packfs_ctx.packfs_archive_files_num] = entry_byte_size;
                    filenames_lens_total += packfs_ctx.packfs_archive_filenames_lens[packfs_ctx.packfs_archive_files_num] + 1;
                    packfs_ctx.packfs_archive_files_num++;
                }
                    
                r = archive_read_data_skip(a);
                if (r == ARCHIVE_EOF)
                    break;
                if (r != ARCHIVE_OK)
                    break; //fprintf(stderr, "%s\n", archive_error_string(a));
            }
#ifdef PACKFS_LOG
            for(size_t i = 0,filenames_start = 0; i < packfs_ctx.packfs_archive_files_num; filenames_start += (packfs_ctx.packfs_archive_filenames_lens[i] + 1), i++)
                fprintf(stderr, "packfs: %s: %s\n", packfs_archive_filename, packfs_ctx.packfs_archive_filenames + filenames_start);
#endif

        }
        while(0);
        archive_read_close(a);
        archive_read_free(a);
    }
    
    return &packfs_ctx;
}

int packfs_open(struct packfs_context* packfs_ctx, const char* path, FILE** out)
{
    path = packfs_sanitize_path(path);

    FILE* fileptr = NULL;
    size_t filesize = 0;
    
    if(packfs_ctx->packfs_archive_files_num > 0 && strncmp(packfs_ctx->packfs_archive_prefix, path, strlen(packfs_ctx->packfs_archive_prefix)) == 0)
    {
        const char* path_without_prefix = path + strlen(packfs_ctx->packfs_archive_prefix);
        if(path_without_prefix[0] == '/')
            path_without_prefix++;

        size_t filenames_start = 0;
        for(size_t i = 0; i < packfs_ctx->packfs_archive_files_num; i++)
        {
            if(0 == strcmp(packfs_ctx->packfs_archive_filenames + filenames_start, path_without_prefix))
            {
                filesize = packfs_ctx->packfs_archive_sizes[i];
                size_t offset = packfs_ctx->packfs_archive_offsets[i];
                if(packfs_ctx->packfs_archive_mmapsize != 0)
                {
                    fileptr = fmemopen((char*)packfs_ctx->packfs_archive_fileptr + offset, filesize, "rb");
                }
                else
                {
                    fileptr = fmemopen(NULL, filesize, "rb+");
                    fseek((FILE*)packfs_ctx->packfs_archive_fileptr, offset, SEEK_SET);
                    char buf[8192];
                    for(size_t size = filesize, len = 0; size > 0; size -= len)
                    {
                        len = fread(buf, 1, sizeof(buf) <= size ? sizeof(buf) : size, (FILE*)packfs_ctx->packfs_archive_fileptr);
                        fwrite(buf, 1, len, fileptr);
                    }
                    fseek(fileptr, 0, SEEK_SET);
                }
                break;
            }
            filenames_start += packfs_ctx->packfs_archive_filenames_lens[i] + 1;
        }
    }
    
    if(out != NULL)
        *out = fileptr;

    for(size_t k = 0; fileptr != NULL && k < packfs_filefd_max - packfs_filefd_min; k++)
    {
        if(packfs_ctx->packfs_filefd[k] == 0)
        {
            packfs_ctx->packfs_filefd[k] = packfs_filefd_min + k;
            packfs_ctx->packfs_fileptr[k] = fileptr;
            packfs_ctx->packfs_filesize[k] = filesize;
            return packfs_ctx->packfs_filefd[k];
        }
    }

    return -1;
}

int packfs_close(struct packfs_context* packfs_ctx, int fd)
{
    if(fd < packfs_filefd_min || fd >= packfs_filefd_max)
        return -2;

    for(size_t k = 0; k < packfs_filefd_max - packfs_filefd_min; k++)
    {
        if(packfs_ctx->packfs_filefd[k] == fd)
        {
            packfs_ctx->packfs_filefd[k] = 0;
            packfs_ctx->packfs_filesize[k] = 0;
            int res = fclose(packfs_ctx->packfs_fileptr[k]);
            packfs_ctx->packfs_fileptr[k] = NULL;
            return res;
        }
    }
    return -2;
}

void* packfs_find(struct packfs_context* packfs_ctx, int fd, FILE* ptr)
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
    path = packfs_sanitize_path(path);

    if(0 == packfs_strncmp(packfs_ctx->packfs_archive_prefix, path, strlen(packfs_ctx->packfs_archive_prefix)))
    {
        for(size_t i = 0, filenames_start = 0, prefix_strlen = strlen(packfs_ctx->packfs_archive_prefix); i < packfs_ctx->packfs_archive_files_num; filenames_start += (packfs_ctx->packfs_archive_filenames_lens[i] + 1), i++)
        {
            if(0 == strcmp(path + prefix_strlen, packfs_ctx->packfs_archive_filenames + filenames_start))
                return 0;
        }
        return -1;
    }
    
    return -2;
}

int packfs_stat(struct packfs_context* packfs_ctx, const char* path, int fd, struct stat *restrict statbuf)
{
    path = packfs_sanitize_path(path);
    size_t prefix_strlen = strlen(packfs_ctx->packfs_archive_prefix);
    if(packfs_ctx->packfs_archive_prefix[prefix_strlen] != packfs_pathsep)
        prefix_strlen++;
    
    if(0 == packfs_strncmp(packfs_ctx->packfs_archive_prefix, path, strlen(packfs_ctx->packfs_archive_prefix)))
    {
        for(size_t i = 0, filenames_start = 0; i < packfs_ctx->packfs_archive_files_num; filenames_start += (packfs_ctx->packfs_archive_filenames_lens[i] + 1), i++)
        {
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: relpath query: %s <?> relpath database: %s\n", path + prefix_strlen, packfs_ctx->packfs_archive_filenames + filenames_start);
#endif
            if(0 == strcmp(path + prefix_strlen, packfs_ctx->packfs_archive_filenames + filenames_start))
            {
                *statbuf = (struct stat){0};
                {
                    statbuf->st_size = (off_t)(packfs_ctx->packfs_archive_sizes[i]);
                    statbuf->st_mode = S_IFREG;
                }
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
                *statbuf = (struct stat){0};
                statbuf->st_size = packfs_ctx->packfs_filesize[k];
                statbuf->st_mode = S_IFREG;
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
        FILE* res = NULL;
        if(packfs_open(packfs_ctx, path, &res) >= 0)
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

int open(const char *path, int flags, ...)
{
    struct packfs_context* packfs_ctx = packfs_ensure_context(path);
    if(!packfs_ctx->disabled)
    {
#ifdef PACKFS_LOG
        fprintf(stderr, "packfs: Open(\"%s\", %d)\n", path, flags);
#endif
        int res = packfs_open(packfs_ctx, path, NULL);
        if(res >= 0)
        { 
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

int close(int fd)
{
    struct packfs_context* packfs_ctx = packfs_ensure_context(NULL);
    if(!packfs_ctx->disabled)
    {
        int res = packfs_close(packfs_ctx, fd);
        if(res >= -1)
        {
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: Close(%d) == %d\n", fd, res);
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
        int res = packfs_stat(packfs_ctx, path, -1, statbuf);
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
    struct packfs_context* packfs_ctx = packfs_ensure_context(NULL);
    if(!packfs_ctx->disabled)
    {
        int res = packfs_stat(packfs_ctx, NULL, fd, statbuf);
        if(res >= -1)
        {
#ifdef PACKFS_LOG
            fprintf(stderr, "packfs: Fstat(%d, %p) == %d\n", fd, (void*)statbuf, res);
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

int statx(int dirfd, const char *restrict path, int flags, unsigned int mask, struct statx *restrict statbuf)
{
    struct packfs_context* packfs_ctx = packfs_ensure_context(path);
    if(!packfs_ctx->disabled)
    {
        struct stat statbufobj = {0}; 
        int res = packfs_stat(packfs_ctx, path, -1, &statbufobj);
        if(res == 0)
         {
            *statbuf = (struct statx){0};
            statbuf->stx_size = statbufobj.st_size;
            statbuf->stx_mode = statbufobj.st_mode;
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
