/*
arp.c
*/

#include "inet.h"

#include "arp.h"
#include "assert.h"
#include "buf.h"
#include "clock.h"
#include "eth.h"
#include "io.h"
#include "sr.h"
#include "type.h"

INIT_PANIC();

#define ARP_PORT_NR	1
#define ARP_CACHE1_NR	8
#define ARP_CACHE2_NR	8
#define ARP_CACHE3_NR	16
#define ARP_CACHE_NR	(ARP_CACHE1_NR+ARP_CACHE2_NR+ARP_CACHE3_NR)
#define ARP_TYPE1	1
#define ARP_TYPE2	2
#define ARP_TYPE3	3

#define MAX_RARP_RETRIES	5
#define MAX_ARP_RETRIES		5
#define RARP_TIMEOUT		(1*HZ)
#define ARP_TIMEOUT		(HZ/2+1)	/* .5 seconds */
#define ARP_EXP_TIME		(20L*60L*HZ)	/* 20 minutes */
#define ARP_NOTRCH_EXP_TIME	(2L*60L*HZ)	/* 2 minutes */
#define ARP_INUSE_OFFSET	(60*HZ)	/* an entry in the cache can be deleted
					   if its not used for 1 minute */

typedef struct arp46
{
	ether_addr_t a46_dstaddr;
	ether_addr_t a46_srcaddr;
	ether_type_t a46_ethtype;
	union
	{
		struct
		{
			u16_t a_hdr, a_pro;
			u8_t a_hln, a_pln;
			u16_t a_op;
			ether_addr_t a_sha;
			u8_t a_spa[4];
			ether_addr_t a_tha;
			u8_t a_tpa[4];
		} a46_data;
		char    a46_dummy[ETH_MIN_PACK_SIZE-ETH_HDR_SIZE];
	} a46_data;
} arp46_t, rarp46_t;

#define a46_hdr a46_data.a46_data.a_hdr
#define a46_pro a46_data.a46_data.a_pro
#define a46_hln a46_data.a46_data.a_hln
#define a46_pln a46_data.a46_data.a_pln
#define a46_op a46_data.a46_data.a_op
#define a46_sha a46_data.a46_data.a_sha
#define a46_spa a46_data.a46_data.a_spa
#define a46_tha a46_data.a46_data.a_tha
#define a46_tpa a46_data.a46_data.a_tpa

typedef struct arp_cache
{
	int ac_flags;
	int ac_type;
	ether_addr_t ac_ethaddr;
	ipaddr_t ac_ipaddr;
	int ac_eth_port;
	time_t ac_expire;
	time_t ac_lastuse;
} arp_cache_t;

#define ACF_EMPTY	0
#define ACF_NETREQ	1
#define ACF_NOTRCH	2

typedef struct arp_port
{
	int ap_flags;
	int ap_state;
	int ap_eth_port;
	int ap_eth_fd;
	int ap_rarp_retries;
	ether_addr_t ap_ethaddr;
	ipaddr_t ap_ipaddr;
	timer_t ap_timer;
	ether_addr_t ap_write_ethaddr;
	ipaddr_t ap_write_ipaddr;
	int ap_write_code;
	ipaddr_t ap_req_ipaddr;
	arp_req_func_t ap_req_func;
	int ap_req_ref;
	int ap_req_count;
	rarp_func_t ap_rarp_func;
	int ap_rarp_ref;
} arp_port_t;

#define APF_EMPTY	0
#define APF_RARP_RD_IP	0x1
#define APF_RARP_RD_SP	0x2
#define APF_ARP_RD_IP	0x4
#define APF_ARP_RD_SP	0x8
#define APF_ARP_WR_IP	0x10
#define APF_ARP_WR_SP	0x20
#define APF_INADDR_SET	0x100
#define APF_MORE2WRITE	0x200
#define APF_CLIENTREQ	0x400
#define APF_RARPREQ	0x800
#define APF_CLIENTWRITE	0x1000

#define APS_EMPTY	0
#define APS_STATMASK	0xff
#define		APS_GETADDR	0x1
#define		APS_RARPPROTO	0x2
#define		APS_RARPWRITE	0x4
#define		APS_RARPWAIT	0x8
#define		APS_ARPSTART	0x10
#define		APS_ARPPROTO	0x20
#define		APS_ARPMAIN	0x40
#define		APS_ERROR	0x80
#define APS_SUSPEND	0x400

FORWARD acc_t *arp_getdata ARGS(( int fd, size_t offset,
	size_t count, int for_ioctl ));
FORWARD int arp_putdata ARGS(( int fd, size_t offset,
	acc_t *data, int for_ioctl ));
FORWARD void arp_main ARGS(( arp_port_t *port ));
FORWARD void arp_timeout ARGS(( int fd, timer_t *timer ));
FORWARD void rarp_timeout ARGS(( int fd, timer_t *timer ));
FORWARD void ipaddr_set ARGS(( arp_port_t *port ));
FORWARD void setup_write ARGS(( arp_port_t *port ));
FORWARD void setup_read ARGS(( arp_port_t *port ));
FORWARD void process_arp_req ARGS(( arp_port_t *port, acc_t *data ));
FORWARD void client_reply ARGS(( arp_port_t *port,
	ether_addr_t *ethaddr ));
FORWARD arp_cache_t *find_cache_ent ARGS(( int eth_port, ipaddr_t ipaddr,
	int level, arp_cache_t **new_ent ));
FORWARD void rarp_read_setup ARGS(( arp_port_t *port ));
FORWARD void print_arp_cache ARGS(( void ));

PRIVATE arp_port_t arp_port_table[ARP_PORT_NR];
PRIVATE arp_port_t *arp_port;
PRIVATE	arp_cache_t arp_cache[ARP_CACHE_NR];

PUBLIC void arp_init()
{
	int i;

	assert (BUF_S >= sizeof(struct nwio_ethstat));
	assert (BUF_S >= sizeof(struct nwio_ethopt));
	assert (BUF_S >= sizeof(rarp46_t));
	assert (BUF_S >= sizeof(arp46_t));
	arp_port_table[0].ap_eth_port= ETH0;

	for (i=0, arp_port= arp_port_table; i<ARP_PORT_NR; i++, arp_port++)
	{
		arp_port->ap_state= APS_EMPTY;
		arp_port->ap_flags= APF_EMPTY;
		arp_main(arp_port);
	}
}

PRIVATE void arp_main(port)
arp_port_t *port;
{
	int result;

#if DEBUG & 256
 { where(); printf("in arp_main with status: %d\n", port->ap_state &
	APS_STATMASK); }
#endif
	switch (port->ap_state & APS_STATMASK)
	{
	case APS_EMPTY:
		port->ap_rarp_retries= 0;
		port->ap_eth_fd= eth_open(port->ap_eth_port,
			port-arp_port_table, arp_getdata, arp_putdata);

		if (port->ap_eth_fd<0)
		{
	printf("arp.c: unable to open ethernet\n");
			return;
		}

		port->ap_state= (port->ap_state &
			 ~(APS_STATMASK|APS_SUSPEND)) | APS_GETADDR;

		result= eth_ioctl (port->ap_eth_fd, NWIOGETHSTAT);

		if (result==NW_SUSPEND)
			port->ap_state |= APS_SUSPEND;

		if (result<0)
		{
#if DEBUG
 if (result != NW_SUSPEND)
 { where(); printf("arp.c: eth_ioctl(..,NWIOGETHSTAT)=%d\n", result); }
#endif
			return;
		}
		if ((port->ap_state & APS_STATMASK) != APS_GETADDR)
			return;
		/* drop through */
	case APS_GETADDR:
#if DEBUG & 256
 { where(); printf("in arp_main with status: %d\n", port->ap_state & APS_STATMASK); }
#endif
		port->ap_state= (port->ap_state &
			~(APS_STATMASK|APS_SUSPEND)) | APS_RARPPROTO;

		result= eth_ioctl (port->ap_eth_fd, NWIOSETHOPT);

		if (result==NW_SUSPEND)
			port->ap_state |= APS_SUSPEND;

		if (result<0)
		{
#if DEBUG
 if (result != NW_SUSPEND)
 { printf("arp.c: eth_ioctl(..,NWIOSETHOPT)=%d\n", result); }
#endif
			return;
		}
		if ((port->ap_state & APS_STATMASK) != APS_RARPPROTO)
			return;
		/* drop through */
	case APS_RARPWAIT:
#if DEBUG & 256
 { where(); printf("in arp_main with status: %d\n",
	port->ap_state & APS_STATMASK); }
#endif
		if (port->ap_flags & APF_INADDR_SET)
		{
			port->ap_state= (port->ap_state &
				~(APS_STATMASK|APS_SUSPEND)) | APS_ARPSTART;
			arp_main(port);
			return;
		}
		/* drop through */
	case APS_RARPPROTO:
#if DEBUG & 256
 { where(); printf("in arp_main with status: %d\n",
	port->ap_state & APS_STATMASK); }
#endif
		rarp_read_setup(port);
		port->ap_state= (port->ap_state &
			~(APS_STATMASK|APS_SUSPEND)) | APS_RARPWRITE;
#if DEBUG & 256
 { where(); printf("doing eth_write\n"); }
#endif
		result= eth_write (port->ap_eth_fd, sizeof(rarp46_t));
		if (result == NW_SUSPEND)
			port->ap_state |= APS_SUSPEND;
		if (result<0)
		{
#if DEBUG & 256
 if (result != NW_SUSPEND)
 { where();  printf("arp.c: eth_write(..,%d)=%d\n", sizeof(rarp46_t), result); }
#endif
			return;
		}
		if ((port->ap_state & APS_STATMASK) != APS_RARPWRITE)
			return;
		/* drop through */
	case APS_RARPWRITE:
#if DEBUG & 256
 { where(); printf("in arp_main with status: %d\n",
	port->ap_state & APS_STATMASK); }
#endif
		port->ap_state= (port->ap_state &
			~(APS_STATMASK|APS_SUSPEND)) | APS_RARPWAIT;
		if (port->ap_rarp_retries>=MAX_RARP_RETRIES)
			return;
#if DEBUG & 256
 { where(); printf("port->ap_rarp_retries= %d\n",
	port->ap_rarp_retries); }
#endif
		port->ap_rarp_retries++;
		clck_timer(&port->ap_timer, get_time() + RARP_TIMEOUT,
			rarp_timeout, port-arp_port_table);
		return;
	case APS_ARPSTART:
#if DEBUG & 256
 { where(); printf("in arp_main with status: %d\n",
	port->ap_state & APS_STATMASK); }
#endif
		if (port->ap_flags & APF_RARP_RD_IP)
		{
			eth_cancel(port->ap_eth_fd, SR_CANCEL_READ);
			port->ap_flags &= ~(APF_RARP_RD_IP|APF_RARP_RD_SP);
		}
		port->ap_state= (port->ap_state &
			~(APS_STATMASK|APS_SUSPEND)) | APS_ARPPROTO;

		{
			arp_cache_t *cache;
			int i;

			cache= arp_cache;
			for (i=0; i<ARP_CACHE_NR; i++, cache++)
			{
				cache->ac_flags= ACF_EMPTY;
				cache->ac_expire= 0;
				cache->ac_lastuse= 0;
			}

			cache= arp_cache;
			for (i=0; i<ARP_CACHE1_NR; i++, cache++)
				cache->ac_type= 1;
			for (i=0; i<ARP_CACHE2_NR; i++, cache++)
				cache->ac_type= 2;
			for (i=0; i<ARP_CACHE3_NR; i++, cache++)
				cache->ac_type= 3;
		}
		result= eth_ioctl (port->ap_eth_fd, NWIOSETHOPT);

		if (result==NW_SUSPEND)
			port->ap_state |= APS_SUSPEND;

		if (result<0)
		{
#if DEBUG
 { where(); printf("arp.c: /* arp */ eth_ioctl(..,NWIOSETHOPT)=%d\n",
	result); }
#endif
			return;
		}
		if ((port->ap_state & APS_STATMASK) != APS_ARPPROTO)
			return;
		/* drop through */
	case APS_ARPPROTO:
#if DEBUG & 256
 { where(); printf("in arp_main with status: %d\n",
	port->ap_state & APS_STATMASK); }
#endif
		port->ap_state= (port->ap_state &
			~(APS_STATMASK|APS_SUSPEND)) | APS_ARPMAIN;
		if (port->ap_flags & APF_MORE2WRITE)
			setup_write(port);
		setup_read(port);
		return;
	default:
		ip_panic((
		 "arp_main(&arp_port_table[%d]) called but ap_state=0x%x\n",
			port-arp_port_table, port->ap_state ));
	}
}

PRIVATE acc_t *arp_getdata (fd, offset, count, for_ioctl)
int fd;
size_t offset;
size_t count;
int for_ioctl;
{
	arp_port_t *port;
	rarp46_t *rarp;
	arp46_t *arp;
	acc_t *data;
	int result;

#if DEBUG & 256
 { where(); printf("arp_getdata (fd= %d, offset= %d, count= %d)\n", fd,
	offset, count); }
#endif
	port= &arp_port_table[fd];

	switch (port->ap_state & APS_STATMASK)
	{
	case APS_RARPPROTO:
		if (!count)
		{
			result= (int)offset;
			if (result<0)
			{
				port->ap_state= (port->ap_state &
					 ~(APS_STATMASK|APS_SUSPEND))|
					 APS_ERROR;
				break;
			}
			if (port->ap_state & APS_SUSPEND)
				arp_main(port);
			return NW_OK;
		}
		assert ((!offset) && (count == sizeof(struct nwio_ethopt)));
		{
			struct nwio_ethopt *ethopt;
			acc_t *acc;

			acc= bf_memreq(sizeof(*ethopt));
			ethopt= (struct nwio_ethopt *)ptr2acc_data(acc);
			ethopt->nweo_flags= NWEO_COPY|NWEO_TYPESPEC;
			ethopt->nweo_type= htons(ETH_RARP_PROTO);
			return acc;
		}
	case APS_RARPWRITE:
		if (!count)
		{
			result= (int)offset;
			if (result<0)
			{
#if DEBUG
 if (result != NW_SUSPEND)
 { where(); printf("arp.c: write error on port %d: error %d\n", fd, result); }
#endif
				port->ap_state= (port->ap_state &
					 ~(APS_STATMASK|APS_SUSPEND))|
					 APS_ERROR;
				break;
			}
			if (port->ap_state & APS_SUSPEND)
				arp_main(port);
			return NW_OK;
		}
		assert (offset+count <= sizeof(rarp46_t));
		data= bf_memreq(sizeof(rarp46_t));
		rarp= (rarp46_t *)ptr2acc_data(data);
		data->acc_offset += offset;
		data->acc_length= count;
		rarp->a46_dstaddr.ea_addr[0]= 0xff;
		rarp->a46_dstaddr.ea_addr[1]= 0xff;
		rarp->a46_dstaddr.ea_addr[2]= 0xff;
		rarp->a46_dstaddr.ea_addr[3]= 0xff;
		rarp->a46_dstaddr.ea_addr[4]= 0xff;
		rarp->a46_dstaddr.ea_addr[5]= 0xff;
		rarp->a46_hdr= htons(ARP_ETHERNET);
		rarp->a46_pro= htons(ETH_IP_PROTO);
		rarp->a46_hln= 6;
		rarp->a46_pln= 4;
		rarp->a46_op= htons(RARP_REQUEST);
		rarp->a46_sha= port->ap_ethaddr;
		rarp->a46_tha= port->ap_ethaddr;
		return data;
	case APS_ARPPROTO:
		if (!count)
		{
			result= (int)offset;
			if (result<0)
			{
				port->ap_state= (port->ap_state &
					 ~(APS_STATMASK|APS_SUSPEND))|
					 APS_ERROR;
				break;
			}
			if (port->ap_state & APS_SUSPEND)
				arp_main(port);
			return NW_OK;
		}
		assert ((!offset) && (count == sizeof(struct nwio_ethopt)));
		{
			struct nwio_ethopt *ethopt;
			acc_t *acc;

			acc= bf_memreq(sizeof(*ethopt));
			ethopt= (struct nwio_ethopt *)ptr2acc_data(acc);
			ethopt->nweo_flags= NWEO_COPY|NWEO_EN_BROAD|
				NWEO_TYPESPEC;
			ethopt->nweo_type= htons(ETH_ARP_PROTO);
			return acc;
		}
	case APS_ARPMAIN:
		assert (port->ap_flags & APF_ARP_WR_IP);
		if (!count)
		{
			result= (int)offset;
			if (result<0)
			{
#if DEBUG
 if (result != NW_SUSPEND)
 { where(); printf("arp.c: write error on port %d: error %d\n", fd, result); }
#endif
				port->ap_state= (port->ap_state &
					 ~(APS_STATMASK|APS_SUSPEND))|
					 APS_ERROR;
				break;
			}
			port->ap_flags &= ~APF_ARP_WR_IP;
			if (port->ap_flags & APF_ARP_WR_SP)
				setup_write(port);
			return NW_OK;
		}
		assert (offset+count <= sizeof(arp46_t));
		data= bf_memreq(sizeof(arp46_t));
		arp= (arp46_t *)ptr2acc_data(data);
		data->acc_offset += offset;
		data->acc_length= count;
		if (port->ap_write_code == ARP_REPLY)
			arp->a46_dstaddr= port->ap_write_ethaddr;
		else
		{
			arp->a46_dstaddr.ea_addr[0]= 0xff;
			arp->a46_dstaddr.ea_addr[1]= 0xff;
			arp->a46_dstaddr.ea_addr[2]= 0xff;
			arp->a46_dstaddr.ea_addr[3]= 0xff;
			arp->a46_dstaddr.ea_addr[4]= 0xff;
			arp->a46_dstaddr.ea_addr[5]= 0xff;
		}
		arp->a46_hdr= htons(ARP_ETHERNET);
		arp->a46_pro= htons(ETH_IP_PROTO);
		arp->a46_hln= 6;
		arp->a46_pln= 4;
		arp->a46_op= htons(port->ap_write_code);
		arp->a46_sha= port->ap_ethaddr;
		memcpy (arp->a46_spa, &port->ap_ipaddr, sizeof(ipaddr_t));
		arp->a46_tha= port->ap_write_ethaddr;
		memcpy (arp->a46_tpa, &port->ap_write_ipaddr, sizeof(ipaddr_t));
		return data;
	default:
		printf("arp_getdata(%d, 0x%d, 0x%d) called but ap_state=0x%x\n",
			fd, offset, count, port->ap_state);
		break;
	}
	return 0;
}

PRIVATE int arp_putdata (fd, offset, data, for_ioctl)
int fd;
size_t offset;
acc_t *data;
int for_ioctl;
{
	arp_port_t *port;
	int result;
	struct nwio_ethstat *ethstat;
	rarp46_t *rarp;

#if DEBUG & 256
 { where(); printf("arp_putdata (fd= %d, offset= %d, data= 0x%x)\n",
	fd, offset, data); }
#endif
	port= &arp_port_table[fd];

	if (port->ap_flags & APF_ARP_RD_IP)
	{
		if (!data)
		{
			result= (int)offset;
			if (result<0)
			{
#if DEBUG
 if (result != NW_SUSPEND)
 { where(); printf("arp.c: read error on port %d: error %d\n", fd, result); }
#endif
				return NW_OK;
			}
			if (port->ap_flags & APF_ARP_RD_SP)
			{
				port->ap_flags &= ~(APF_ARP_RD_IP|
					APF_ARP_RD_SP);
				setup_read(port);
			}
			else
				port->ap_flags &= ~(APF_ARP_RD_IP|
					APF_ARP_RD_SP);
			return NW_OK;
		}
		assert (!offset);
		/* Warning: the above assertion is illegal; puts and gets of
		   data can be brokenup in any piece the server likes. However
		   we assume that the server is eth.c and it transfers only
		   whole packets. */
		data= bf_packIffLess(data, sizeof(arp46_t));
		if (data->acc_length >= sizeof(arp46_t))
			process_arp_req(port,data);
		bf_afree(data);
		return NW_OK;
	}
	if (port->ap_flags & APF_RARP_RD_IP)
	{
		if (!data)
		{
			result= (int)offset;
			if (result<0)
			{
				if (result != EINTR)
					 ip_warning((
				"arp.c: read error on port %d: error %d\n", 
								fd, result ));
				return NW_OK;
			}
			port->ap_flags &= ~APF_RARP_RD_IP;
			if (port->ap_flags & APF_INADDR_SET)
				ipaddr_set(port);
			else if (port->ap_flags & APF_RARP_RD_SP)
				rarp_read_setup(port);
			return NW_OK;
		}
		assert (!offset);
		/* Warning: the above assertion is illegal; puts and gets of
		   data can be brokenup in any piece the server likes. However
		   we assume that the server is eth.c and it transfers only
		   whole packets. */
		data= bf_packIffLess(data, sizeof(rarp46_t));
		rarp= (rarp46_t *)ptr2acc_data(data);
		if ((data->acc_length >= sizeof(rarp46_t)) &&
			(rarp->a46_hdr == htons(ARP_ETHERNET)) &&
			(rarp->a46_pro == htons(ETH_IP_PROTO)) &&
			(rarp->a46_hln == 6) &&
			(rarp->a46_pln == 4) &&
			(rarp->a46_op == htons(RARP_REPLY)) &&
			(rarp->a46_tha.ea_addr[5] ==
				port->ap_ethaddr.ea_addr[5]) &&
			(rarp->a46_tha.ea_addr[4] ==
				port->ap_ethaddr.ea_addr[4]) &&
			(rarp->a46_tha.ea_addr[3] ==
				port->ap_ethaddr.ea_addr[3]) &&
			(rarp->a46_tha.ea_addr[2] ==
				port->ap_ethaddr.ea_addr[2]) &&
			(rarp->a46_tha.ea_addr[1] ==
				port->ap_ethaddr.ea_addr[1]) &&
			(rarp->a46_tha.ea_addr[0] ==
				port->ap_ethaddr.ea_addr[0]) &&
			!(port->ap_flags & APF_INADDR_SET))
		{
			memcpy (&port->ap_ipaddr, rarp->a46_tpa,
				sizeof(ipaddr_t));
			port->ap_flags |= APF_INADDR_SET;
#if DEBUG & 256
 { unsigned char *a; where();  a=(unsigned char *)&port->ap_ipaddr; 
	printf("arp.c: got ip address: %d.%d.%d.%d\n", 
	a[0], a[1], a[2], a[3]); }
#endif
		}
		bf_afree(data);
		return NW_OK;
	}
	switch (port->ap_state & APS_STATMASK)
	{
	case APS_GETADDR:
		if (!data)
		{
			result= (int)offset;
			if (result<0)
			{
				port->ap_state= (port->ap_state &
					 ~(APS_STATMASK|APS_SUSPEND))|
					 APS_ERROR;
				break;
			}
			if (port->ap_state & APS_SUSPEND)
				arp_main(port);
			return NW_OK;
		}
		compare (bf_bufsize(data), ==, sizeof(*ethstat));
		data= bf_packIffLess(data, sizeof(*ethstat));
		compare (data->acc_length, ==, sizeof(*ethstat));
		ethstat= (struct nwio_ethstat *)ptr2acc_data(data);
		port->ap_ethaddr= ethstat->nwes_addr;
		bf_afree(data);
		return NW_OK;
	default:
		printf("arp_putdata(%d, 0x%d, 0x%lx) called but ap_state=0x%x\n",
			fd, offset, (unsigned long)data, port->ap_state);
		break;
	}
	return EGENERIC;
}

PRIVATE void rarp_timeout (fd, timer)
int fd;
timer_t *timer;
{
	arp_port_t *port;

#if DEBUG & 256
 { where(); printf("in rarp_timeout()\n"); }
#endif
	port= &arp_port_table[fd];

	assert (timer == &port->ap_timer);

	arp_main(port);
}

PRIVATE void ipaddr_set (port)
arp_port_t *port;
{
	if (port->ap_flags & APF_RARPREQ)
	{
		port->ap_flags &= ~APF_RARPREQ;
		(*port->ap_rarp_func)(port->ap_rarp_ref, port->ap_ipaddr);
	}
	if (port->ap_state & APS_RARPWAIT)
	{
		clck_untimer(&port->ap_timer);
		port->ap_state= (port->ap_state &
			 ~(APS_STATMASK|APS_SUSPEND)) | APS_ARPSTART;
		arp_main(port);
	}
}

PRIVATE void setup_read(port)
arp_port_t *port;
{
	int result;

	while (!(port->ap_flags & APF_ARP_RD_IP))
	{
		port->ap_flags |= APF_ARP_RD_IP;
		result= eth_read (port->ap_eth_fd, ETH_MAX_PACK_SIZE);
		if (result == NW_SUSPEND)
			port->ap_flags |= APF_ARP_RD_SP;
		if (result<0)
		{
#if DEBUG
 if (result != NW_SUSPEND) 
 { where(); printf("arp.c: eth_read(..,%d)=%d\n", ETH_MAX_PACK_SIZE, result); }
#endif
			return;
		}
	}
}

PRIVATE void setup_write(port)
arp_port_t *port;
{
	int i, result;

	while (port->ap_flags & APF_MORE2WRITE)
	{
		if (port->ap_flags & APF_CLIENTWRITE)
		{
			port->ap_flags &= ~APF_CLIENTWRITE;
			port->ap_write_ipaddr= port->ap_req_ipaddr;
			port->ap_write_code= ARP_REQUEST;
			clck_timer(&port->ap_timer, get_time() + ARP_TIMEOUT,
				arp_timeout, port-arp_port_table);
		}
		else
		{
			arp_cache_t *cache;

			cache= arp_cache;
			for (i=0; i<ARP_CACHE_NR; i++, cache++)
				if ((cache->ac_flags & ACF_NETREQ) &&
					cache->ac_eth_port ==
					port->ap_eth_port)
				{
					cache->ac_flags &= ~ACF_NETREQ;
					port->ap_write_ethaddr= cache->
						ac_ethaddr;
					port->ap_write_ipaddr= cache->
						ac_ipaddr;
					port->ap_write_code= ARP_REPLY;
					break;
				}
			if (i>=ARP_CACHE_NR)
			{
				port->ap_flags &= ~APF_MORE2WRITE;
				break;
			}
		}
		port->ap_flags= (port->ap_flags & ~APF_ARP_WR_SP) |
			APF_ARP_WR_IP;
#if DEBUG & 256
 { where(); printf("doing eth_write\n"); }
#endif
		result= eth_write(port->ap_eth_fd, sizeof(arp46_t));
		if (result == NW_SUSPEND)
			port->ap_flags |= APF_ARP_WR_SP;
		if (result<0)
		{
#if DEBUG
 if (result != NW_SUSPEND)
 { where(); printf("arp.c: eth_write(..,%d)=%d\n", sizeof(rarp46_t), result); }
#endif
			return;
		}
	}
}

PRIVATE void process_arp_req (port, data)
arp_port_t *port;
acc_t *data;
{
	arp46_t *arp;
	arp_cache_t *prim, *sec;
	int level;
	time_t curr_tim;
	ipaddr_t spa, tpa;

#if DEBUG & 256
 { where(); printf("process_arp_req(...)\n"); }
#endif
#if DEBUG & 256
 { where(); print_arp_cache(); }
#endif
	arp= (arp46_t *)ptr2acc_data(data);
	memcpy(&spa, arp->a46_spa, sizeof(ipaddr_t));
	memcpy(&tpa, arp->a46_tpa, sizeof(ipaddr_t));

#if DEBUG & 256
 {
  if (arp->a46_hdr == htons(ARP_ETHERNET)) 
  { where(); printf("arp.c: a46_hdr OK\n"); }
  if (arp->a46_hln == 6) 
  { where(); printf("arp.c: a46_hln OK\n"); }
  if (arp->a46_pro == htons(ETH_IP_PROTO)) 
  { where(); printf("arp.c: a46_pro OK\n"); }
  if (arp->a46_pln == 4) 
  { where(); printf("arp.c: a46_pln OK\n"); }
 }
#endif
	if (arp->a46_hdr != htons(ARP_ETHERNET) ||
		arp->a46_hln != 6 ||
		arp->a46_pro != htons(ETH_IP_PROTO) ||
		arp->a46_pln != 4)
		return;
#if DEBUG & 256
 { where(); printf("arp.c: a46_tpa= 0x%lx, ap_ipaddr= 0x%lx\n",
	arp->a46_tpa, port->ap_ipaddr); }
#endif
	if ((port->ap_flags & APF_CLIENTREQ) && (spa == port->ap_req_ipaddr))
		level= ARP_TYPE3;
	else if (arp->a46_op == htons(ARP_REQUEST) && (tpa ==
		port->ap_ipaddr))
		level= ARP_TYPE2;
	else
		level= ARP_TYPE1;

#if DEBUG & 256
 { where(); printf("arp.c: level= %d\n", level); }
#endif
	prim= find_cache_ent(port->ap_eth_port, spa, level, &sec);
	if (!prim)
	{
		prim= sec;
		prim->ac_flags= ACF_EMPTY;
		prim->ac_ipaddr= spa;
		prim->ac_eth_port= port->ap_eth_port;
	}
	else if (prim->ac_type < level)
	{
		sec->ac_type= prim->ac_type;
		prim->ac_type= level;
	}
	prim->ac_ethaddr= arp->a46_sha;
	curr_tim= get_time();
	prim->ac_expire= curr_tim+ ARP_EXP_TIME;
	if (curr_tim > prim->ac_lastuse)
		prim->ac_lastuse= curr_tim;
	prim->ac_flags &= ~ACF_NOTRCH;
	if (level== ARP_TYPE2)
	{
		prim->ac_flags |= ACF_NETREQ;
		port->ap_flags |= APF_MORE2WRITE;
		if (!(port->ap_flags & APF_ARP_WR_IP))
			setup_write(port);
	} else if (level== ARP_TYPE3)
	{
		prim->ac_lastuse= curr_tim + ARP_INUSE_OFFSET;
		client_reply(port, &arp->a46_sha);
	}
#if DEBUG & 256
 { where(); print_arp_cache(); }
#endif
}

PRIVATE void client_reply (port, ethaddr)
arp_port_t *port;
ether_addr_t *ethaddr;
{
	port->ap_flags &= ~(APF_CLIENTREQ|APF_CLIENTWRITE);
	clck_untimer(&port->ap_timer);
	(*port->ap_req_func)(port->ap_req_ref, ethaddr);
}

PRIVATE arp_cache_t *find_cache_ent (eth_port, ipaddr, level, new_ent)
int eth_port;
ipaddr_t ipaddr;
int level;
arp_cache_t **new_ent;
{
	arp_cache_t *cache, *prim, *sec;
	int i;

	cache= arp_cache;
	prim= 0;
	sec= 0;
	for (i=0; i<ARP_CACHE_NR; i++, cache++)
	{
		if (cache->ac_eth_port == eth_port &&
			cache->ac_ipaddr == ipaddr)
			prim= cache;
		if (cache->ac_type == level && (!sec || cache->ac_lastuse <
			sec->ac_lastuse))
			sec= cache;
	}
	assert(sec);
	*new_ent= sec;
	return prim;
}

PRIVATE void rarp_read_setup (port)
arp_port_t *port;
{
	int result;

	while (!(port->ap_flags & (APF_RARP_RD_IP|APF_INADDR_SET)))
	{
		port->ap_flags= (port->ap_flags & ~ APF_RARP_RD_SP) |
			APF_RARP_RD_IP;
		result= eth_read (port->ap_eth_fd, ETH_MAX_PACK_SIZE);
		if (result == NW_SUSPEND)
			port->ap_flags |= APF_RARP_RD_SP;
		if (result<0)
		{
#if DEBUG
 if (result != NW_SUSPEND)
 { where(); printf("arp.c: eth_read(..,%d)=%d\n", ETH_MAX_PACK_SIZE, result); }
#endif
			return;
		}
		if ((port->ap_state & APS_STATMASK) != APS_RARPPROTO)
			return;
	}
}

PUBLIC int rarp_req(eth_port, ref, func)
int eth_port;
int ref;
rarp_func_t func;
{
	arp_port_t *port;
	int i;

	port= arp_port_table;
	for (i=0; i<ARP_PORT_NR; i++, port++)
		if (port->ap_eth_port == eth_port)
			break;
	if (i>=ARP_PORT_NR)
		return EGENERIC;
	if (port->ap_flags & APF_INADDR_SET)
	{
		(*func)(ref, port->ap_ipaddr);
		return NW_OK;
	}
	port->ap_flags |= APF_RARPREQ;
	port->ap_rarp_ref= ref;
	port->ap_rarp_func= func;
	return NW_SUSPEND;
}

PUBLIC void set_ipaddr (eth_port, ipaddr)
int eth_port;
ipaddr_t ipaddr;
{
	arp_port_t *port;
	int i;

	port= arp_port_table;
	for (i=0; i<ARP_PORT_NR; i++, port++)
		if (port->ap_eth_port == eth_port)
			break;
	assert (i < ARP_PORT_NR);
	port->ap_ipaddr= ipaddr;
	port->ap_flags |= APF_INADDR_SET;
	ipaddr_set(port);
}

PUBLIC int arp_ip_eth (eth_port, ref, ipaddr, func)
int eth_port;
int ref;
ipaddr_t ipaddr;
arp_req_func_t func;
{
	arp_port_t *port;
	int i;
	arp_cache_t *prim, *sec;

#if DEBUG & 256
 { where(); printf("sending arp_req for: "); writeIpAddr(ipaddr);
	printf("\n"); }
#endif
	port= arp_port_table;
	for (i=0; i<ARP_PORT_NR; i++, port++)
		if (port->ap_eth_port == eth_port)
			break;
	if (i>=ARP_PORT_NR)
		return EGENERIC;
	if ((port->ap_state & APS_STATMASK) != APS_ARPMAIN)
	{
		port->ap_flags |= APF_CLIENTREQ|APF_MORE2WRITE |
			APF_CLIENTWRITE;
		port->ap_req_func= func;
		port->ap_req_ref= ref;
		port->ap_req_ipaddr= ipaddr;
		port->ap_req_count= 0;
		return NW_SUSPEND;
	}

	prim= find_cache_ent (eth_port, ipaddr, ARP_TYPE3, &sec);
	if (prim)
	{
		if (prim->ac_type < ARP_TYPE3)
		{
			sec->ac_type= prim->ac_type;
			prim->ac_type= ARP_TYPE3;
		}
		if (prim->ac_expire < get_time())
			prim= 0;
	}
	if (!prim)
	{
		port->ap_flags |= APF_CLIENTREQ|APF_MORE2WRITE|APF_CLIENTWRITE;
		port->ap_req_func= func;
		port->ap_req_ref= ref;
		port->ap_req_ipaddr= ipaddr;
		port->ap_req_count= 0;
		if (!(port->ap_flags & APF_ARP_WR_IP))
			setup_write(port);
		return NW_SUSPEND;
	}
	prim->ac_lastuse= get_time();
	if (prim->ac_flags & ACF_NOTRCH)
		return EDSTNOTRCH;
	else
	{
		client_reply (port, &prim->ac_ethaddr);
		return NW_OK;
	}
}

PUBLIC int arp_ip_eth_nonbl (eth_port, ipaddr, ethaddr)
int eth_port;
ipaddr_t ipaddr;
ether_addr_t *ethaddr;
{
	arp_port_t *port;
	int i;
	arp_cache_t *prim, *sec;

#if DEBUG & 256
 { where(); printf("got a arp_ip_eth_nonbl(%d, ", eth_port);
	writeIpAddr(ipaddr); printf(", ...)\n");  }
#endif
	port= arp_port_table;
	for (i=0; i<ARP_PORT_NR; i++, port++)
		if (port->ap_eth_port == eth_port)
			break;
	if (i>=ARP_PORT_NR)
		return EGENERIC;
	if ((port->ap_state & APS_STATMASK) != APS_ARPMAIN)
	{
#if DEBUG
 { where(); printf("replying NW_SUSPEND\n"); }
#endif
		return NW_SUSPEND;
	}

	prim= find_cache_ent (eth_port, ipaddr, ARP_TYPE3, &sec);
	if (prim)
	{
		if (prim->ac_type < ARP_TYPE3)
		{
			sec->ac_type= prim->ac_type;
			prim->ac_type= ARP_TYPE3;
		}
		if (prim->ac_expire < get_time())
			prim= 0;
	}
	if (!prim)
	{
#if DEBUG & 256
 { where(); printf("replying NW_SUSPEND\n"); }
#endif
		return NW_SUSPEND;
	}

	if (prim->ac_flags & ACF_NOTRCH)
	{
#if DEBUG
 { where(); printf("replying EDSTNOTRCH\n"); }
#endif
		return EDSTNOTRCH;
	}
	else
	{
		prim->ac_lastuse= get_time();
		if (ethaddr)
			*ethaddr= prim->ac_ethaddr;
#if DEBUG & 256
 { where(); printf("replying NW_OK (\n"); writeEtherAddr(&prim->ac_ethaddr);
	printf(")\n"); }
#endif
		return NW_OK;
	}
}

PRIVATE void arp_timeout (fd, timer)
int fd;
timer_t *timer;
{
	arp_port_t *port;
	arp_cache_t *prim, *sec;
	int level;
	time_t curr_tim;

	port= &arp_port_table[fd];

	assert (timer == &port->ap_timer);

	if (++port->ap_req_count < MAX_ARP_RETRIES)
	{
		port->ap_flags |= APF_CLIENTWRITE|APF_MORE2WRITE;
		if (!(port->ap_flags & APF_ARP_WR_IP))
			setup_write(port);
	}
	else
	{
		level= ARP_TYPE3;
		prim= find_cache_ent(port->ap_eth_port, port->ap_req_ipaddr,
			level, &sec);
		if (!prim)
		{
			prim= sec;
			prim->ac_flags= ACF_EMPTY;
			prim->ac_ipaddr= port->ap_req_ipaddr;
		}
		else if (prim->ac_type < level)
		{
			sec->ac_type= prim->ac_type;
			prim->ac_type= level;
		}
		curr_tim= get_time();
		prim->ac_expire= curr_tim+ ARP_NOTRCH_EXP_TIME;
		prim->ac_lastuse= curr_tim + ARP_INUSE_OFFSET;
		prim->ac_flags |= ACF_NOTRCH;

		(*port->ap_req_func)(port->ap_req_ref,
			(ether_addr_t *)0);
	}
}

PRIVATE void print_arp_cache()
{
	int i;
	arp_cache_t *ci;

	for (i=0, ci= arp_cache; i< ARP_CACHE_NR; i++, ci++)
	{
		if (!ci->ac_expire)
			continue;
		printf("%d %d ", ci->ac_flags, ci->ac_type);
		writeEtherAddr(&ci->ac_ethaddr);
		printf(" ");
		writeIpAddr(ci->ac_ipaddr);
		printf(" %d %ld %ld\n", ci->ac_eth_port, ci->ac_expire,
			ci->ac_lastuse);
	}
}
