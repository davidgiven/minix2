/*
ip.h
*/

#ifndef INET_IP_H
#define INET_IP_H

#define IP0	0

/* Prototypes */

struct acc;

void ip_init ARGS(( void ));
int  ip_open ARGS(( int port, int srfd,
	struct acc *(*get_userdata) (int fd, size_t offset, size_t count,
		int for_ioctl),
	int (*put_userdata) (int fd, size_t offset, struct acc *data,
		int for_ioctl) ));
int ip_ioctl ARGS(( int fd, int req ));
int ip_read ARGS(( int fd, size_t count ));
int ip_write ARGS(( int fd, size_t count ));

#endif /* INET_IP_H */
