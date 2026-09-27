#ifndef SPROUTOS_BROWSER_OSDEP_H
#define SPROUTOS_BROWSER_OSDEP_H



#include "kernel.h"



#define vfs_open  browser_vfs_open
#define vfs_read  browser_vfs_read
#define vfs_close browser_vfs_close

#include "browser.h"


int  browser_vfs_open(const char* path, int flags);
int  browser_vfs_read(int fd, void* buf, int len);
void browser_vfs_close(int fd);

int  net_http_get(const char* url, uint8_t** out_buf, int* out_len);

#endif
