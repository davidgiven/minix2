/*
server/ip/gen/icmp.h
*/

#ifndef __SERVER__IP__GEN__ICMP_H__
#define __SERVER__IP__GEN__ICMP_H__

#define ICMP_MIN_HDR_LEN	4

#define ICMP_TYPE_ECHO_REPL	0
#define ICMP_TYPE_DST_UNRCH	3
#	define ICMP_NET_UNRCH		0
#	define ICMP_HOST_UNRCH		1
#define ICMP_TYPE_REDIRECT	5
#	define ICMP_REDIRECT_NET	0
#	define ICMP_REDIRECT_HOST	1
#define ICMP_TYPE_ECHO_REQ	8
#define ICMP_TYPE_ROUTER_ADVER	9
#define ICMP_TYPE_ROUTE_SOL	10
#define ICMP_TYPE_TIME_EXCEEDED	11
#	define ICMP_TTL_EXC		0

#endif /* __SERVER__IP__GEN__ICMP_H__ */
