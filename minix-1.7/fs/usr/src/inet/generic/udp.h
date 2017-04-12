/*
udp.h
*/

#ifndef UDP_H
#define UDP_H

#define UDP_DEF_OPT		NWUO_NOFLAGS
#define UDP_MAX_DATAGRAM	40000 /* 8192 */
#define UDP_READ_EXP_TIME	(10L * HZ)
#define UDP_TOS			0
#define UDP_IP_FLAGS		0
#define UDP_TTL			30

#define UDP0	0

struct acc;

void udp_init ARGS(( void ));
int udp_open ARGS(( int port, int srfd,
	struct acc *(*get_userdata) (int fd, size_t offset, size_t count, 
		int for_ioctl),
	int (*put_userdata) (int fd, size_t offset, struct acc *data,
		int for_ioctl) ));
int udp_ioctl ARGS(( int fd, int req ));
int udp_read ARGS(( int fd, size_t count ));
int udp_write ARGS(( int fd, size_t count ));
void udp_close ARGS(( int fd ));
int udp_cancel ARGS(( int fd, int which_operation ));

#endif /* UDP_H */

