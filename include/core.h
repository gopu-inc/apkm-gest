#ifndef CORE_H
#define CORE_H

int extract_package(const char *filepath, const char *dest_path);
int run_install_script(const char *staging_path, const char *pkg_name);

#endif