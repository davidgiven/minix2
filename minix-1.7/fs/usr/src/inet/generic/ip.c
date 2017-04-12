/*
ip.c
*/

#include "inet.h"
#include "buf.h"
#include "type.h"

#include "arp.h"
#include "assert.h"
#include "clock.h"
#include "eth.h"
#include "icmp.h"
#include "icmp_lib.h"
#include "io.h"
#include "ip.h"
#include "ip_int.h"
#include "ipr.h"
#include "sr.h"

INIT_PANIC();

FORWARD void ip_eth_main ARGS(( ip_port_t *port ));
FORWARD void ip_close ARGS(( int fd ));
FORWARD int ip_cancel ARGS(( int fd, int which_operation ));
FORWARD acc_t *get_eth_data ARGS(( int fd, size_t offset,
	size_t count, int for_ioctl ));
FORWARD int put_eth_data ARGS(( int fd, size_t offset,
	acc_t *data, int for_ioctl ));
FORWARD void rarp_func ARGS(( int fd, ipaddr_t ipaddr ));
FORWARD void do_eth_read ARGS(( ip_port_t *port ));

#if IP_ROUTER
FORWARD void ip_route ARGS(( ip_port_t *port, acc_t *pack ));
#endif /* IP_ROUTER */

PUBLIC ip_port_t ip_port_table[IP_PORT_NR];
PUBLIC ip_fd_t ip_fd_table[IP_FD_NR];
PUBLIC ip_ass_t ip_ass_table[IP_ASS_NR];


PRIVATE int ip_cancel (fd, which_operation)
int fd;
int which_operation;
{
	ip_fd_t *ip_fd;
	acc_t *repl_res;
	int result;

	ip_fd= &ip_fd_table[fd];

	switch (which_operation)
	{
	case SR_CANCEL_IOCTL:
assert (ip_fd->if_flags & IFF_GIPCONF_IP);
		ip_fd->if_flags &= ~IFF_GIPCONF_IP;
		repl_res= (*ip_fd->if_get_userdata)(ip_fd->if_srfd, 
			(size_t)EINTR, (size_t)0, TRUE);
assert (!repl_res);
		break;
	case SR_CANCEL_READ:
assert (ip_fd->if_flags & IFF_READ_IP);
		ip_fd->if_flags &= ~IFF_READ_IP;
		result= (*ip_fd->if_put_userdata)(ip_fd->if_srfd, 
			(size_t)EINTR, (acc_t *)0, FALSE);
assert (!result);
		break;
	case SR_CANCEL_WRITE:
assert (ip_fd->if_flags & IFF_WRITE_MASK);
		ip_fd->if_flags &= ~IFF_WRITE_MASK;
		repl_res= (*ip_fd->if_get_userdata)(ip_fd->if_srfd, 
			(size_t)EINTR, (size_t)0, FALSE);
assert (!repl_res);
		break;
	default:
		ip_panic(( "unknown cancel request" ));
		break;
	}
	return NW_OK;
}


PUBLIC void ip_init()
{
	int i, result;
	ip_ass_t *ip_ass;
	ip_fd_t *ip_fd;
	ip_port_t *ip_port;

	assert (BUF_S >= sizeof(struct nwio_ethopt));
	assert (BUF_S >= IP_MAX_HDR_SIZE + ETH_HDR_SIZE);
	assert (BUF_S >= sizeof(nwio_ipopt_t));
	assert (BUF_S >= sizeof(nwio_route_t));

	ip_port_table[0].ip_dl.dl_eth.de_port= ETH0;
	ip_port_table[0].ip_dl_type= IPDL_ETH;
	ip_port_table[0].ip_minor= IP_DEV0;

	for (i=0, ip_ass= ip_ass_table; i<IP_ASS_NR; i++, ip_ass++)
	{
		ip_ass->ia_frags= 0;
		ip_ass->ia_first_time= 0;
		ip_ass->ia_port= 0;
	}

	for (i=0, ip_fd= ip_fd_table; i<IP_FD_NR; i++, ip_fd++)
	{
		ip_fd->if_flags= IFF_EMPTY;
	}

	for (i=0, ip_port= ip_port_table; i<IP_PORT_NR; i++, ip_port++)
	{
		ip_port->ip_flags= IPF_EMPTY;
		switch(ip_port->ip_dl_type)
		{
		case IPDL_ETH:
			ip_port->ip_dl.dl_eth.de_state= IES_EMPTY;
			ip_port->ip_dl.dl_eth.de_flags= IEF_EMPTY;
			break;
		default:
			ip_panic(( "unknown ip_dl_type" ));
			break;
		}
	}

	icmp_init();
	ipr_init();

	for (i=0, ip_port= ip_port_table; i<IP_PORT_NR; i++, ip_port++)
	{
		ip_port->ip_frame_id= (u16_t)get_time();

		result= sr_add_minor(ip_port->ip_minor,
			ip_port-ip_port_table, ip_open, ip_close,
			ip_read, ip_write, ip_ioctl, ip_cancel);
		assert (result>=0);

		switch(ip_port->ip_dl_type)
		{
		case IPDL_ETH:
			ip_eth_main(ip_port);
			break;
		default:
			ip_panic(( "unknown ip_dl_type" ));
		}
	}
}

PRIVATE void ip_eth_main(ip_port)
ip_port_t *ip_port;
{
	int result, i;
	ip_fd_t *ip_fd;

	switch (ip_port->ip_dl.dl_eth.de_state)
	{
	case IES_EMPTY:
		ip_port->ip_dl.dl_eth.de_wr_ipaddr= (ipaddr_t)0;
		ip_port->ip_dl.dl_eth.de_state= IES_SETPROTO;
		ip_port->ip_dl.dl_eth.de_fd= eth_open(ip_port->
			ip_dl.dl_eth.de_port, ip_port-ip_port_table,
			get_eth_data, put_eth_data);
		if (ip_port->ip_dl.dl_eth.de_fd < 0)
		{
			printf("ip.c: unable to open eth port\n");
			return;
		}

		result= eth_ioctl(ip_port->ip_dl.dl_eth.de_fd,
			NWIOSETHOPT);
		if (result == NW_SUSPEND)
			ip_port->ip_dl.dl_eth.de_flags |= IEF_SUSPEND;
		if (result<0)
		{
#if DEBUG
 { where(); printf("eth_ioctl(..,%lx)=%d\n", NWIOSETHOPT, result); }
#endif
			return;
		}
		if (ip_port->ip_dl.dl_eth.de_state != IES_SETPROTO)
			return;
		/* drops through */
	case IES_SETPROTO:
		ip_port->ip_dl.dl_eth.de_state= IES_GETIPADDR;

		result= rarp_req (ip_port->ip_dl.dl_eth.de_port,
			ip_port-ip_port_table, rarp_func);

		if (result == NW_SUSPEND)
			ip_port->ip_dl.dl_eth.de_flags |= IEF_SUSPEND;
		if (result<0)
		{
#if DEBUG & 256
 { where(); printf("rarp_req(...)=%d\n", result); }
#endif
			return;
		}
		if (ip_port->ip_dl.dl_eth.de_state != IES_GETIPADDR)
			return;
		/* drops through */
	case IES_GETIPADDR:
		ip_port->ip_dl.dl_eth.de_state= IES_MAIN;
		for (i=0, ip_fd= ip_fd_table; i<IP_FD_NR; i++, ip_fd++)
		{
			if (!(ip_fd->if_flags & IFF_INUSE))
			{
#if DEBUG & 256
 { where(); printf("%d not inuse\n", i); }
#endif
				continue;
			}
			if (ip_fd->if_port != ip_port)
			{
#if DEBUG
 { where(); printf("%d wrong port\n", i); }
#endif
				continue;
			}
#if DEBUG & 256
 { where(); printf("(ip_fd_t *)0x%x->if_flags= 0x%x\n", ip_fd,
 ip_fd->if_flags); }
#endif
			if (ip_fd->if_flags & IFF_WRITE_IP)
			{
#if DEBUG
 { where(); printf("%d write ip\n", i); }
#endif
				ip_fd->if_flags &= ~IFF_WRITE_IP;
				ip_write (i, ip_fd->if_wr_count);
			}
			if (ip_fd->if_flags & IFF_GIPCONF_IP)
			{
#if DEBUG  & 256
 { where(); printf("restarting ip_ioctl (.., NWIOGIPCONF)\n"); }
#endif
				ip_ioctl (i, NWIOGIPCONF);
			}
		}
#if DEBUG & 256
 { where(); printf("ip_port->ip_ipaddr= "); writeIpAddr(ip_port->ip_ipaddr); 
	printf("\n"); }
#endif
		icmp_getnetmask(ip_port-ip_port_table);
		do_eth_read(ip_port);
		return;
	default:
		ip_panic(( "unknown state" ));
	}
}

PRIVATE acc_t *get_eth_data (fd, offset, count, for_ioctl)
int fd;
size_t offset;
size_t count;
int for_ioctl;
{
	ip_port_t *port;
	acc_t *data;
	int result;

#if DEBUG & 256
 { where(); printf("get_eth_data(fd= %d, offset= %d, count= %u) called\n",
		fd, offset, count); }
#endif
	port= &ip_port_table[fd];

	switch (port->ip_dl.dl_eth.de_state)
	{
	case IES_SETPROTO:
		if (!count)
		{
			result= (int)offset;
			if (result<0)
			{
				port->ip_dl.dl_eth.de_state= IES_ERROR;
				break;
			}
			if (port->ip_dl.dl_eth.de_flags & IEF_SUSPEND)
				ip_eth_main(port);
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
			ethopt->nweo_type= htons(ETH_IP_PROTO);
			return acc;
		}

	case IES_MAIN:
		assert (port->ip_dl.dl_eth.de_flags & IEF_WRITE_IP);
		if (!count)
		{

			result= (int)offset;
#if DEBUG & 256
 { where(); printf("get_eth_data: result= %d\n", result); }
#endif
			if (result<0)
				printf("ip.c: error on write: %d\n",
					result);
			bf_afree (port->ip_dl.dl_eth.de_wr_frame);
			port->ip_dl.dl_eth.de_wr_frame= 0;
#if DEBUG & 256
 { where(); printf("eth_write completed\n"); }
#endif
			if (port->ip_dl.dl_eth.de_flags & IEF_WRITE_SP)
			{
#if DEBUG & 256
 { where(); printf("calling dl_eth_write_frame\n"); }
#endif
				port->ip_dl.dl_eth.de_flags &=
					~(IEF_WRITE_SP|IEF_WRITE_IP);
				dll_eth_write_frame(port);
			}
#if DEBUG & 256
 else { where(); printf("not calling dl_eth_write_frame\n"); }
#endif
			return NW_OK;
		}
#if DEBUG & 256
 { where(); printf("supplying data for eth\n"); }
#endif
		data= bf_cut (port->ip_dl.dl_eth.de_wr_frame, offset,
			count);
		assert (data);
		return data;

	default:
		printf("get_eth_data(%d, 0x%d, 0x%d) called but ip_state=0x%x\n",
			fd, offset, count, port->ip_dl.dl_eth.de_state);
		break;
	}
	return 0;
}

PRIVATE void rarp_func (port, ipaddr)
int port;
ipaddr_t ipaddr;
{
	ip_port_t *ip_port;

#if DEBUG & 256
 { where(); printf("rarp_func\n"); }
#endif
	ip_port= &ip_port_table[port];

	if (!(ip_port->ip_flags & IPF_IPADDRSET))
	{
		ip_port->ip_ipaddr= ipaddr;
		ip_port->ip_flags |= IPF_IPADDRSET;
	}
	switch (ip_port->ip_dl_type)
	{
	case IPDL_ETH:
		if (ip_port->ip_dl.dl_eth.de_flags & IEF_SUSPEND)
			ip_eth_main(ip_port);
		break;
	default:
		ip_panic(( "unknown dl_type" ));
	}
}

PRIVATE void do_eth_read(port)
ip_port_t *port;
{
	int result;

	while (!(port->ip_dl.dl_eth.de_flags & IEF_READ_IP))
	{
		port->ip_dl.dl_eth.de_flags &= ~IEF_READ_SP;
		port->ip_dl.dl_eth.de_flags |= IEF_READ_IP;
		result= eth_read (port->ip_dl.dl_eth.de_fd,
			ETH_MAX_PACK_SIZE);
		if (result == NW_SUSPEND)
			port->ip_dl.dl_eth.de_flags |= IEF_READ_SP;
		if (result<0)
		{
#if DEBUG & 256
 { where(); printf("eth_read(%d, ...)= %d\n", port->ip_dl.dl_eth.de_fd,
	result); }
#endif
			return;
		}
		port->ip_dl.dl_eth.de_flags &= ~IEF_READ_IP;
	}
}

PRIVATE int put_eth_data (port, offset, data, for_ioctl)
int port;
size_t offset;
acc_t *data;
int for_ioctl;
{
	ip_port_t *ip_port;
	acc_t *pack;
	int result;

#if DEBUG & 256
 { where(); printf("put_eth_data() called\n"); }
#endif
	ip_port= &ip_port_table[port];

	if (ip_port->ip_dl.dl_eth.de_flags & IEF_READ_IP)
	{
		if (!data)
		{
			result= (int)offset;
			if (result<0)
			{
#if DEBUG
 { where(); printf("ip.c: put_eth_data(..,%d,..)\n", result); }
#endif
				return NW_OK;
			}
			ip_port->ip_dl.dl_eth.de_flags &= ~IEF_READ_IP;
			if (ip_port->ip_dl.dl_eth.de_flags &
				IEF_READ_SP)
			{
				do_eth_read(ip_port);
			}
			return NW_OK;
		}
		assert (!offset);
		/* Warning: the above assertion is illegal; puts and
		   gets of data can be brokenup in any piece the server
		   likes. However we assume that the server is eth.c
		   and it transfers only whole packets. */
		ip_eth_arrived(ip_port, data);
		return NW_OK;
	}
	printf("ip_port->ip_dl.dl_eth.de_state= 0x%x",
		ip_port->ip_dl.dl_eth.de_state);
	ip_panic (( "strange status" ));
}

PUBLIC int ip_open (port, srfd, get_userdata, put_userdata)
int port;
int srfd;
get_userdata_t get_userdata;
put_userdata_t put_userdata;
{
	int i;
	ip_fd_t *ip_fd;

	for (i=0; i<IP_FD_NR && (ip_fd_table[i].if_flags & IFF_INUSE);
		i++);

	if (i>=IP_FD_NR)
	{
#if DEBUG
 { where(); printf("out of fds\n"); }
#endif
		return EOUTOFBUFS;
	}

	ip_fd= &ip_fd_table[i];

	ip_fd->if_flags= IFF_INUSE;

	ip_fd->if_ipopt.nwio_flags= NWIO_DEFAULT;
	ip_fd->if_ipopt.nwio_tos= 0;
	ip_fd->if_ipopt.nwio_df= FALSE;
	ip_fd->if_ipopt.nwio_ttl= 255;
	ip_fd->if_ipopt.nwio_hdropt.iho_opt_siz= 0;

	ip_fd->if_port= &ip_port_table[port];
	ip_fd->if_srfd= srfd;
	ip_fd->if_rd_buf= 0;
	ip_fd->if_get_userdata= get_userdata;
	ip_fd->if_put_userdata= put_userdata;
	return i;
}

PRIVATE void ip_close (fd)
int fd;
{
	ip_fd_t *ip_fd;

	ip_fd= &ip_fd_table[fd];

	assert ((ip_fd->if_flags & IFF_INUSE) &&
		!(ip_fd->if_flags & IFF_BUSY));

	ip_fd->if_flags= IFF_EMPTY;
	if (ip_fd->if_rd_buf)
	{
		bf_afree(ip_fd->if_rd_buf);
		ip_fd->if_rd_buf= 0;
	}
}

