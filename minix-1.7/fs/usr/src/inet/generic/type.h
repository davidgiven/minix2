/*
type.h
*/

#ifndef INET_TYPE_H
#define INET_TYPE_H

typedef struct acc *(*get_userdata_t) ARGS(( int fd, size_t offset,
	size_t count, int for_ioctl ));
typedef int (*put_userdata_t) ARGS(( int fd, size_t offset,
	struct acc *data, int for_ioctl ));
typedef void (*arp_req_func_t) ARGS(( int fd, ether_addr_t
	*ethaddr ));
typedef void (*rarp_func_t) ARGS(( int fd, ipaddr_t ipaddr ));

#endif /* INET_TYPE_H */
