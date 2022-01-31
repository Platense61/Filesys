#ifndef CRASH_H
#define CRASH_H

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#define CRASHES_IN_100 1
#define FALSE 0
#define TRUE  1

void init_crasher();
ssize_t crash_write(int vdisk, void * buf, size_t num_bytes);
ssize_t crash_read(int vdisk, void *buf, size_t num_bytes);
void * crash_return(void * args);

#endif
