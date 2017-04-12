/*
inet/inet.h

Created:	Dec 30, 1991 by Philip Homburg
*/

#ifndef INET__NW_TASK_H
#define INET__NW_TASK_H

#define _MINIX	1
#define _SYSTEM	1	/* get OK and negative error codes */

#include <sys/types.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <ansi.h>
#include <minix/config.h>
#include <minix/type.h>

#include <minix/com.h>
#include <minix/const.h>
#include <minix/syslib.h>
#include <net/hton.h>
#include <net/gen/ether.h>
#include <net/gen/eth_hdr.h>
#include <net/gen/eth_io.h>
#include <net/gen/in.h>
#include <net/gen/ip_hdr.h>
#include <net/gen/ip_io.h>
#include <net/gen/icmp.h>
#include <net/gen/icmp_hdr.h>
#include <net/gen/oneCsum.h>
#include <net/gen/route.h>
#include <net/gen/tcp.h>
#include <net/gen/tcp_hdr.h>
#include <net/gen/tcp_io.h>
#include <net/gen/udp.h>
#include <net/gen/udp_hdr.h>
#include <net/gen/udp_io.h>
#include <sys/ioctl.h>

/* Ioctl's may contain size and type encoding.  It pays to extract the type. */
#ifdef _IOCTYPE_MASK
#define IOCTYPE_MASK	_IOCTYPE_MASK
#define IOCPARM_MASK	_IOCPARM_MASK
#else
#define IOCTYPE_MASK	0xFFFF
#endif

#include "const.h"

#define PUBLIC
#define EXTERN	extern
#define PRIVATE	static
#define FORWARD	static

#define INIT_PANIC()	static char *ip_panic_warning_file= __FILE__

#define ip_panic(print_list)  \
	( \
		printf("panic at %s, %d: ", ip_panic_warning_file, __LINE__), \
		printf print_list, \
		printf("\n"), \
		abort(), \
		0 \
	)

#define ip_warning(print_list)  \
	( \
		printf("warning at %s, %d: ", ip_panic_warning_file, \
								__LINE__), \
		printf print_list, \
		printf("\n"), \
		0 \
	)

#if _ANSI
#define ARGS(x) x
#else /* _ANSI */
#define ARGS(x) ()
#endif /* _ANSI */

#endif /* INET__NW_TASK_H */
