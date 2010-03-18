/* 
 * File:   capture.h
 * Author: carmelo
 *
 * Created on December 12, 2009, 7:58 PM
 */

#ifndef _VIDEO_H
#define	_VIDEO_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <asm/types.h>          /* for videodev2.h */

#include <linux/videodev2.h>

#define CLEAR(x) memset (&(x), 0, sizeof (x))

typedef enum {
    IO_METHOD_READ,
    IO_METHOD_MMAP,
    IO_METHOD_USERPTR,
} io_method;

struct buffer{
    void * start;
    size_t length;
};

char* video_dev_name;
io_method io;
int fd;;
struct buffer * buffers;
unsigned int n_buffers;
struct v4l2_format fmt;
__u32 video_format;

void errno_exit (const char * s);
static int xioctl (int fd, int request, void *arg);
void process_frame (unsigned char *p, int bytes, const struct v4l2_format* fmt);
int read_frame(void);
int poll_video_device(void);
void mainloop (void);
void stop_video_capturing(void);
void start_video_capturing(void);
void uninit_video_device (void);
static void init_read(unsigned int buffer_size);
static void init_mmap(void);
static void init_userp (unsigned int buffer_size);
void init_video_device (void);
void close_video_device (void);
void open_video_device (void);

#ifdef	__cplusplus
}
#endif

#endif	/* _VIDEO_H */

