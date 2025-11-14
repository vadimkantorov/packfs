#include <stddef.h>
#include <dirent.h>

int packfs_init(const char* path, const char* packfs_config);


int packfs_scan_archive(FILE* f, const char* packfs_archive_filename, const char* prefix);

int packfs_scan_archive_dir(DIR* dirptr, const char* path_normalized, size_t path_normalized_len, const char* prefix);

int packfs_scan_path(const char* input_path, bool isdir);

int packfs_scan_listing(FILE* fileptr, const char* packfs_listing_filename, const char* prefix, const char* prefix_archive);


int packfs_dump_listing(const char* removeprefix, const char* output_path);

int packfs_dump_static_package(const char* prefix, const char* removeprefix, const char* output_path, const char* ld, const char* input_path);


int packfs_dynamic_add_file(const char* prefix, size_t prefix_len, const char* entrypath, size_t entrypath_len, size_t size, size_t offset, size_t archivepaths_offset);

int packfs_dynamic_add_dirname(const char* prefix, size_t prefix_len, const char* entrypath, size_t entrypath_len, bool isdir);

int packfs_dynamic_add_prefix(const char* prefix, size_t prefix_len);
