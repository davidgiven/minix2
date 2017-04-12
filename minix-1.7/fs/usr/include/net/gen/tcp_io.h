/*
server/ip/gen/tcp_io.h
*/

#ifndef __SERVER__IP__GEN__TCP_IO_H__
#define __SERVER__IP__GEN__TCP_IO_H__

typedef struct nwio_tcpconf
{
	u32_t nwtc_flags;
	ipaddr_t nwtc_locaddr;
	ipaddr_t nwtc_remaddr;
	tcpport_t nwtc_locport;
	tcpport_t nwtc_remport;
} nwio_tcpconf_t;

#define NWTC_NOFLAGS	0x0000L
#define NWTC_ACC_MASK	0x0003L
#	define NWTC_EXCL	0x00000001L
#	define NWTC_SHARED	0x00000002L
#	define NWTC_COPY	0x00000003L
#define NWTC_LOCPORT_MASK	0x0030L
#	define NWTC_LP_UNSET	0x00000010L
#	define NWTC_LP_SET	0x00000020L
#	define NWTC_LP_SEL	0x00000030L
#define NWTC_REMADDR_MASK	0x0100L
#	define NWTC_SET_RA	0x00000100L
#	define NWTC_UNSET_RA	0x01000000L
#define NWTC_REMPORT_MASK	0x0200L
#	define NWTC_SET_RP	0x00000200L
#	define NWTC_UNSET_RP	0x02000000L

typedef struct nwio_tcpcl
{
	long nwtcl_flags;
	long nwtcl_ttl;
} nwio_tcpcl_t;

typedef struct nwio_tcpatt
{
	long nwta_flags;
} nwio_tcpatt_t;

typedef struct nwio_tcpopt
{
	long nwto_flags;
} nwio_tcpopt_t;

#endif /* __SERVER__IP__GEN__TCP_IO_H__ */
