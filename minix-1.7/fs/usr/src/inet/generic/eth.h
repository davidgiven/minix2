/*
eth.h
*/

#ifndef ETH_H
#define ETH_H

#define NWEO_DEFAULT    (NWEO_EN_LOC | NWEO_DI_BROAD | NWEO_DI_MULTI | \
	NWEO_DI_PROMISC | NWEO_REMANY | NWEO_RWDATALL)

#define ETH0	0	/* port number of ethernet port 0 */

#define eth_addrcmp(a,b) (memcmp((_VOIDSTAR)&a, (_VOIDSTAR)&b, \
	sizeof(a)))

/* Forward declatations */

struct acc;

/* prototypes */

void eth_init ARGS(( void ));
int eth_open ARGS(( int port, int srfd,
	struct acc *(*get_userdata)(int fd, size_t offset,size_t count, 
		int for_ioctl),
	int (*put_userdata)(int fd, size_t offset, struct acc *data, 
		int for_ioctl) ));
int eth_ioctl ARGS(( int fd, int req));
int eth_read ARGS(( int port, size_t count ));
int eth_write ARGS(( int port, size_t count ));
int eth_cancel ARGS(( int fd, int which_operation ));
void eth_close ARGS(( int fd ));

#endif /* ETH_H */
