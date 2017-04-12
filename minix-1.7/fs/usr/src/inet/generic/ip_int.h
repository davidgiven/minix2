/*
ip.h
*/

#ifndef INET_IP_INT_H
#define INET_IP_INT_H

/* this include file depends on:
#include <ansi.h>
#include <sys/types.h>
#include <minix/type.h>
#include <inet/ether.h>
#include <inet/in.h>
#include <inet/ip_io.h>
#include "buf.h"
#include "type.h"
*/

#define IP_FD_NR	32
#define IP_PORT_NR	1
#define IP_ASS_NR	3

#define IP_SUN_BROADCAST	1	/* hostnumber 0 is also network
					   broadcast */
#define IP_ROUTER		0	/* this implementation isn't a
					   gateway */

typedef struct ip_port
{
	int ip_flags, ip_dl_type;
	union
	{
		struct
		{
			int de_state;
			int de_flags;
			int de_port;
			int de_fd;
			acc_t *de_wr_frag;
			acc_t *de_wr_frame;
			ether_addr_t de_wr_ethaddr;
			ipaddr_t de_wr_ipaddr;
			acc_t *de_arp_pack;
			ether_addr_t de_arp_ethaddr;
		} dl_eth;
	} ip_dl;
	int ip_minor;
	ipaddr_t ip_ipaddr, ip_netmask;
	u16_t ip_frame_id;
} ip_port_t;

#define IES_EMPTY	0x0
#define	IES_SETPROTO	0x1
#define	IES_GETIPADDR	0x2
#define	IES_MAIN	0x3
#define	IES_ERROR	0x4

#define IEF_EMPTY	0x1
#define IEF_WRITE_IP	0x2
#define IEF_WRITE_SP	0x4
#define IEF_SUSPEND	0x8
#define IEF_READ_IP	0x10
#define IEF_READ_SP	0x20
#define IEF_ARP_MASK	0x1c0
#	define IEF_ARP_IP	0x40
#	define IEF_ARP_SP	0x80
#	define IEF_ARP_COMPL	0x100

#define IPF_EMPTY	0x0
#define IPF_IPADDRSET	0x1
#define IPF_NETMASKSET	0x2

#define IPDL_ETH	0

typedef struct ip_ass
{
	acc_t *ia_frags;
	int ia_min_ttl;
	ip_port_t *ia_port;
	time_t ia_first_time;
	ipaddr_t ia_srcaddr, ia_dstaddr;
	int ia_proto, ia_id;
} ip_ass_t;

typedef struct ip_fd
{
	int if_flags;
	struct nwio_ipopt if_ipopt;
	ip_port_t *if_port;
	int if_srfd;
	acc_t *if_rd_buf;
	get_userdata_t if_get_userdata;
	put_userdata_t if_put_userdata;
	time_t if_exp_tim;
	size_t if_rd_count;
	ipaddr_t if_wr_dstaddr;
	size_t if_wr_count;
	ip_port_t *if_wr_port;
} ip_fd_t;

#define IFF_EMPTY	0x0
#define IFF_INUSE	0x1
#define IFF_OPTSET	0x2
#define IFF_BUSY	0x7f4
#	define IFF_READ_IP	0x4
#	define IFF_WRITE_MASK	0x3f0
#		define IFF_WRITE_IP	0x10
#		define IFF_DLL_WR_IP	0x20
#		define IFF_ROUTED	0x40
#		define IFF_NETBROAD_IP	0x200
#	define IFF_GIPCONF_IP	0x400


/* ip_lib.c */
ipaddr_t ip_get_netmask ARGS(( ipaddr_t hostaddr ));
int ip_chk_hdropt ARGS(( u8_t *opt, int optlen ));
void ip_print_frags ARGS(( acc_t *acc ));

/* ip_read.c */
void ip_port_arrive ARGS(( ip_port_t *port, acc_t *pack, ip_hdr_t *ip_hdr ));
void ip_eth_arrived ARGS(( ip_port_t *port, acc_t *pack ));
int ip_ok_for_fd ARGS(( ip_fd_t *ip_fd, acc_t *pack ));
int ip_packet2user ARGS(( ip_fd_t *ip_fd ));

/* ip_write.c */
void dll_eth_write_frame ARGS(( ip_port_t *port ));

extern ip_fd_t ip_fd_table[IP_FD_NR];
extern ip_port_t ip_port_table[IP_PORT_NR];
extern ip_ass_t ip_ass_table[IP_ASS_NR];


#define NWIO_DEFAULT    (NWIO_EN_LOC | NWIO_EN_BROAD | NWIO_REMANY | \
	NWIO_RWDATALL | NWIO_HDR_O_SPEC)

#endif /* INET_IP_INT_H */
