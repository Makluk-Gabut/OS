#ifndef FS_H
#define FS_H

#include <stdint.h>

void fs_format(void);
int fs_mount(void);
int fs_write(const char* name, const uint8_t* data, uint32_t size);
int fs_read(const char* name, uint8_t* out_buf, uint32_t max_size, uint32_t* out_size);
int fs_delete(const char* name);
void fs_list(void);

#endif
