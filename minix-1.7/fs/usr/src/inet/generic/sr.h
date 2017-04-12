/*
sr.h
*/

#ifndef SR_H
#define SR_H

#define MAX_IOCTL_S	512

#define ETH_DEV		ETH_DEV0
#define IP_DEV		IP_DEV0

#define ETH_DEV0	1
#define IP_DEV0		2
#define TCP_DEV0	3
#define UDP_DEV0	4

#define SR_CANCEL_IOCTL	1
#define SR_CANCEL_READ	2
#define SR_CANCEL_WRITE	3

/* Forward struct declarations */

struct acc;

/* prototypes */

typedef int  (*sr_open_t) ARGS(( int port, int srfd,
	struct acc *(*get_userdata)(int fd, size_t offset,size_t count,
			int for_ioctl), 
	int (*put_userdata)(int fd, size_t offset, struct acc *data,
			int for_ioctl) ));
typedef void (*sr_close_t) ARGS(( int fd ));
typedef int (*sr_read_t) ARGS(( int fd, size_t count ));
typedef int (*sr_write_t) ARGS(( int fd, size_t count ));
typedef int  (*sr_ioctl_t) ARGS(( int fd, int req ));
typedef int  (*sr_cancel_t) ARGS(( int fd, int which_operation ));

void sr_init ARGS(( void  ));
int sr_add_minor ARGS(( int minor, int port, sr_open_t openf,
	sr_close_t closef, sr_read_t sr_read, sr_write_t sr_write,
	sr_ioctl_t ioctlf, sr_cancel_t cancelf ));

#endif /* SR_H */
