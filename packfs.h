#include <stddef.h>

int packfs_init(const char* path, const char* packfs_config);


int packfs_scan_archive(const char* packfs_archive_filename, const char* prefix);
int packfs_scan_archive_dir(const char* path_normalized, const char* prefix);
int packfs_scan_path(const char* input_path);
int packfs_scan_listing(const char* packfs_listing_filename, const char* prefix, const char* prefix_archive);


int packfs_dynamic_add_prefix(const char* prefix, const size_t prefix_len);
int packfs_dynamic_add_file(const char* prefix, const size_t prefix_len, const char* entrypath, const size_t entrypath_len, const size_t size, const size_t offset, const size_t archivepaths_offset);
int packfs_dynamic_add_dirname(const char* prefix, const size_t prefix_len, const char* entrypath, const size_t entrypath_len, bool isdir);


int packfs_dump_listing(const char* output_path, const char* removeprefix);
int packfs_dump_static_package(const char* output_path, const char* removeprefix, const char* input_path, const char* prefix, const char* ld);
