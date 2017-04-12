/*
tcp.h
*/

#ifndef TCP_H
#define TCP_H

#define ISS_INC_FREQ	250000L
#define TCP_MAX_DATAGRAM	8192
#define TCP_MAX_WND_SIZE	(4*1024)
#define SUN_0WND_BUG		1
	/* the sun 4.x.y implementation of tcp/ip does not send zero
	   windows but instead does not acknowledge new data */
#define SUN_TRANS_BUG		0
	/* the sun 4.x.y implementation of tcp/ip does not send fast
	   as posible until a zero window is reached but use the 
	   round trip delay as a measurement. It is then not possible
	   to delay ACKs */

#define TCP_DEF_URG_WND		1024
#define TCP_DEF_TOS		0
#define TCP_DEF_TTL		1	/* seconds */
#define TCP_DEF_TIME_OUT	HZ	/* 1 second, in clock ticks */
#define TCP_DEF_MAX_NO_RETRANS	10000
#define TCP_DEF_RTT		15	/* initial retransmission time in
					   ticks */
#define TCP_DEF_MSS		1400
#if SUN_TRANS_BUG
#define TCP_ACK_DELAY		1	/* no delay */
#else
#define TCP_ACK_DELAY		(HZ/2)	/* .5 second is clock ticks */
#endif

#define TCP_DEF_OPT		(NWTC_COPY | NWTC_LP_UNSET | NWTC_UNSET_RA | \
					NWTC_UNSET_RP)

#define TCP0			0

struct acc;

void tcp_init ARGS(( void ));
int tcp_open ARGS(( int port, int srfd,
	struct acc *(*get_userdata) (int fd, size_t offset, size_t count, 
		int for_ioctl),
	int (*put_userdata) (int fd, size_t offset, struct acc *data, 
		int for_ioctl) ));
int tcp_read ARGS(( int fd, size_t count));
int tcp_write ARGS(( int fd, size_t count));
int tcp_ioctl ARGS(( int fd, int req));
int tcp_cancel ARGS(( int fd, int which_operation ));
void tcp_close ARGS(( int fd));

#endif /* TCP_H */
