/*
ip_write.c
*/

#include "inet.h"
#include "buf.h"
#include "type.h"

#include "arp.h"
#include "assert.h"
#include "clock.h"
#include "eth.h"
#include "icmp_lib.h"
#include "io.h"
#include "ip.h"
#include "ip_int.h"
#include "ipr.h"

INIT_PANIC();

FORWARD acc_t *get_packet ARGS(( ip_fd_t *ip_fd,
	U16_t id /* should be: u16_t id */ ));
FORWARD int dll_ready ARGS(( ip_port_t *port, ipaddr_t dst ));
FORWARD void dll_write ARGS(( ip_port_t *port, ipaddr_t dst,
	acc_t *pack ));
FORWARD int dll_eth_ready ARGS(( ip_port_t *port, ipaddr_t dst ));
FORWARD void dll_eth_write ARGS(( ip_port_t *port, ipaddr_t dst,
	acc_t *pack ));
FORWARD void dll_eth_arp_func ARGS(( int fd,
	ether_addr_t *ethaddr ));
FORWARD acc_t *ip_split_pack ARGS(( acc_t **ref_last,
	int first_size ));
FORWARD void error_reply ARGS(( ip_fd_t *fd, int error ));
FORWARD int chk_dstaddr ARGS(( ipaddr_t dst ));
FORWARD void restart_netbroadcast ARGS(( void ));
FORWARD int ip_localroute_addr ARGS(( ip_fd_t *ip_fd ));
FORWARD void ip_remroute_addr ARGS(( ip_fd_t *ip_fd, U8_t ttl ));
FORWARD void restart_fd_write ARGS(( ip_fd_t *ip_fd ));
FORWARD void restart_netbroad_fd ARGS(( ip_fd_t *tcp_fd ));
FORWARD void dll_eth_get_work ARGS(( ip_port_t *ip_port ));

#define NF_EMPTY		0
#define NF_INUSE		1
#define NF_SUSPENDED		2

PRIVATE unsigned int netbroad_flags= NF_EMPTY;
PRIVATE acc_t *netbroad_pack;
PRIVATE ipaddr_t netbroad_dst;
PRIVATE ipaddr_t netbroad_netmask;
PRIVATE ip_port_t *netbroad_port;

PUBLIC int ip_write (fd, count)
int fd;
size_t count;
{
	ip_fd_t *ip_fd;
	acc_t *data;
	int result;
	int ttl;

#if DEBUG & 256
 { where(); printf("ip_write.c: ip_write(fd= %d, count= %d\n", fd,
	count); }
#endif
	ip_fd= &ip_fd_table[fd];

	if (!(ip_fd->if_flags & IFF_OPTSET))
	{
		error_reply (ip_fd, EBADMODE);
		return NW_OK;
	}
	if (ip_fd->if_ipopt.nwio_flags & NWIO_RWDATALL)
	{
		if (count < IP_MIN_HDR_SIZE || count > IP_MAX_PACKSIZE)
		{
			error_reply (ip_fd, EPACKSIZE);
			return NW_OK;
		}
	}
	else
	{
assert (ip_fd->if_ipopt.nwio_flags & NWIO_RWDATONLY);
		if (count < 0 || count > IP_MAX_PACKSIZE-IP_MIN_HDR_SIZE)
		{
			error_reply (ip_fd, EPACKSIZE);
			return NW_OK;
		}
	}
	ip_fd->if_wr_count= count;

	assert (!(ip_fd->if_flags & IFF_WRITE_IP));

	ip_fd->if_flags &= ~IFF_WRITE_MASK;
	ip_fd->if_flags |= IFF_WRITE_IP;

	if (!(ip_fd->if_port->ip_flags & IPF_IPADDRSET))
		return NW_SUSPEND;

	if (ip_fd->if_ipopt.nwio_flags & NWIO_REMSPEC)
		ip_fd->if_wr_dstaddr= ip_fd->if_ipopt.nwio_rem;
	else
	{
		data= (*ip_fd->if_get_userdata)(ip_fd->if_srfd,
			(size_t)(offsetof (struct ip_hdr, ih_dst)),
			sizeof(ipaddr_t), FALSE);
		if (!data)
		{
			ip_fd->if_flags &= ~IFF_WRITE_IP;
			return NW_OK;
		}
		data= bf_packIffLess (data, sizeof(ipaddr_t));
		ip_fd->if_wr_dstaddr= *(ipaddr_t *)ptr2acc_data(data);
		bf_afree(data);
		data= 0;
	}
	if (ip_fd->if_ipopt.nwio_flags & NWIO_HDR_O_SPEC)
		ttl= 255;				/* For traceroute */
	else
	{
		data= (*ip_fd->if_get_userdata)(ip_fd->if_srfd,
			(size_t)(offsetof (struct ip_hdr, ih_ttl)),
			sizeof(u8_t), FALSE);
		if (!data)
		{
			ip_fd->if_flags &= ~IFF_WRITE_IP;
			return NW_OK;
		}
		data= bf_packIffLess (data, sizeof(u8_t));
		ttl= *(u8_t *)ptr2acc_data(data);
		bf_afree(data);
		data= 0;
	}
	result= ip_localroute_addr(ip_fd);
	if (!result)
		ip_remroute_addr(ip_fd, ttl);
	if (ip_fd->if_flags & IFF_WRITE_IP)
		return NW_SUSPEND;
	else
		return NW_OK;
}

PRIVATE int ip_localroute_addr (ip_fd)
ip_fd_t *ip_fd;
{
	ipaddr_t dstaddr, netmask;
	u8_t *addrInBytes;
	acc_t *pack;
	ip_hdr_t *hdr_ptr;
	ip_port_t *ip_port;
	int result, i;

#if DEBUG & 256
 { where(); printf("ip_write.c: ip_route_addr(...) to ");
	writeIpAddr(ip_fd->if_wr_dstaddr); printf("\n"); }
#endif
	dstaddr= ip_fd->if_wr_dstaddr;
	addrInBytes= (u8_t *)&dstaddr;
	ip_port= ip_fd->if_port;

	if ((addrInBytes[0] & 0xff) == 0x7f)	/* local loopback */
	{
		pack= get_packet(ip_fd, (u16_t)0);
		if (!pack)
		{
			ip_fd->if_flags &= ~IFF_WRITE_IP;
			return TRUE;
		}

		assert (pack->acc_length >= IP_MIN_HDR_SIZE);
		assert (pack->acc_linkC == 1);
		hdr_ptr= (ip_hdr_t *)ptr2acc_data(pack);
		dstaddr= hdr_ptr->ih_dst;	/* src and dst
						   addresses */
		hdr_ptr->ih_dst= hdr_ptr->ih_src;
		hdr_ptr->ih_src= dstaddr;
		ip_port_arrive (ip_port, pack, hdr_ptr);
		ip_fd->if_flags &= ~IFF_WRITE_IP;
		error_reply (ip_fd, ip_fd->if_wr_count);
		return TRUE;
	}
	if (dstaddr == (ipaddr_t)-1)
	{
		ip_fd->if_flags |= IFF_DLL_WR_IP;
		ip_fd->if_wr_port= ip_port;

#if DEBUG
 { where(); printf("calling restart_fd_write\n"); }
#endif
		restart_fd_write(ip_fd);
		return TRUE;
	}
	netmask= ip_get_netmask(dstaddr);

	for (i=0, ip_port= ip_port_table; i<IP_PORT_NR; i++, ip_port++)
	{
		if (!(ip_port->ip_flags & IPF_IPADDRSET))
		{
#if DEBUG 
 { where(); printf("!(ip_port_table[%d].ip_flags & IPF_IPADDRSET)\n",
	ip_port-ip_port_table); }
#endif
			continue;
		}
#if DEBUG & 256
 { where(); printf("ip_port_table[%d].ip_ipaddr=  ", ip_port-ip_port_table);
	writeIpAddr(ip_port->ip_ipaddr); printf("\n"); }
#endif
		if (ip_port->ip_ipaddr == dstaddr)
		{
			pack= get_packet(ip_fd, (u16_t)0);
			if (!pack)
			{
				ip_fd->if_flags &= ~IFF_WRITE_IP;
				return TRUE;
			}

			assert (pack->acc_length >= IP_MIN_HDR_SIZE);
			assert (pack->acc_linkC == 1);
			ip_port_arrive (ip_port, pack, 
				(ip_hdr_t *)ptr2acc_data(pack));
			ip_fd->if_flags &= ~IFF_WRITE_IP;
			error_reply (ip_fd, ip_fd->if_wr_count);
			return TRUE;
		}
		if ((dstaddr & ip_port->ip_netmask) ==
			(ip_port->ip_ipaddr & ip_port->ip_netmask))
		{
			ip_fd->if_wr_port= ip_port;

			if ((dstaddr & ~ip_port->ip_netmask) ==
				~ip_port->ip_netmask)
				ip_fd->if_wr_dstaddr= (ipaddr_t)-1;
			ip_fd->if_flags |= IFF_DLL_WR_IP;

#if DEBUG & 256
 { where(); printf("calling restart_fd_write\n"); }
#endif
			restart_fd_write(ip_fd);
			return TRUE;
		}
		if (((dstaddr & netmask) == (ip_port->ip_ipaddr &
			netmask)) && ((dstaddr & ~netmask) == ~netmask))
		{
			if (!(netbroad_flags & NF_INUSE))
				restart_netbroad_fd(ip_fd);
			else
				ip_fd->if_flags |= IFF_NETBROAD_IP;
			return TRUE;
		}
	}
	return FALSE;
}

PRIVATE int dll_ready(port, dst)
ip_port_t *port;
ipaddr_t dst;
{
	switch (port->ip_dl_type)
	{
	case IPDL_ETH:
		return dll_eth_ready (port, dst);
	default:
		ip_panic(( "strange dll_type" ));
	}
	return NW_OK;
}

PRIVATE int dll_eth_ready (port, dst)
ip_port_t *port;
ipaddr_t dst;
{
	int result;

	if (port->ip_dl.dl_eth.de_wr_frame || port->ip_dl.dl_eth.
		de_wr_frag)
	{
#if DEBUG & 256
 { where(); printf("dll_eth_ready: frame or frag present\n"); }
#endif
		return NW_SUSPEND;
	}
	if (dst == (ipaddr_t)-1)
	{
#if DEBUG & 256
 { where(); printf("dll_eth_ready: broadcast\n"); }
#endif
		port->ip_dl.dl_eth.de_wr_ipaddr= dst;
		port->ip_dl.dl_eth.de_wr_ethaddr.ea_addr[0]= 0xff;
		port->ip_dl.dl_eth.de_wr_ethaddr.ea_addr[1]= 0xff;
		port->ip_dl.dl_eth.de_wr_ethaddr.ea_addr[2]= 0xff;
		port->ip_dl.dl_eth.de_wr_ethaddr.ea_addr[3]= 0xff;
		port->ip_dl.dl_eth.de_wr_ethaddr.ea_addr[4]= 0xff;
		port->ip_dl.dl_eth.de_wr_ethaddr.ea_addr[5]= 0xff;
		return NW_OK;
	}
#if DEBUG & 256
 { where(); printf("ip_write.c: calling arp_ip_eth_nonbl(...)\n"); }
#endif
	result= arp_ip_eth_nonbl (port->ip_dl.dl_eth.de_port,
		dst, &port->ip_dl.dl_eth.de_wr_ethaddr);
#if DEBUG & 256
 { where(); printf("ip_write.c: arp_ip_eth_nonbl(...)= %d\n", result); }
#endif
	if (result<0)
		port->ip_dl.dl_eth.de_wr_ipaddr= (ipaddr_t)0;

	if (result == EDSTNOTRCH)
		return EDSTNOTRCH;

	if (result >= 0)
	{
		port->ip_dl.dl_eth.de_wr_ipaddr= dst;
		return NW_OK;
	}
assert (result == NW_SUSPEND);

	if (!(port->ip_dl.dl_eth.de_flags & IEF_ARP_IP))
	{
#if DEBUG & 256
 { where(); printf("dll_eth_ready: no ARP_IP\n"); }
#endif
		return NW_OK;
	}
#if DEBUG
 { where(); printf("dll_eth_ready: ARP_IP\n"); }
#endif
	return NW_SUSPEND;
}

PRIVATE void dll_write (port, dst, pack)
ip_port_t *port;
ipaddr_t dst;
acc_t *pack;
{
	switch (port->ip_dl_type)
	{
	case IPDL_ETH:
		dll_eth_write (port, dst, pack);
		break;
	default:
		ip_panic(( "wrong dl_type" ));
		break;
	}
}

PRIVATE void dll_eth_write (ip_port, dst, pack)
ip_port_t *ip_port;
ipaddr_t dst;
acc_t *pack;
{
	int result;

	if (!ip_port->ip_dl.dl_eth.de_wr_frag)
	{
		if (ip_port->ip_dl.dl_eth.de_wr_ipaddr == dst)
		{
			ip_port->ip_dl.dl_eth.de_wr_frag= pack;
			if (!(ip_port->ip_dl.dl_eth.de_flags &
				IEF_WRITE_IP))
			{
				dll_eth_write_frame(ip_port);
			}
			return;
		}
		ip_port->ip_dl.dl_eth.de_wr_ipaddr= (ipaddr_t)0;
#if DEBUG & 256
 { where(); printf("ip_write.c: calling arp_ip_eth_nonbl(...)\n"); }
#endif
		result= arp_ip_eth_nonbl (ip_port->ip_dl.dl_eth.de_port,
			dst, &ip_port->ip_dl.dl_eth.de_wr_ethaddr);
#if DEBUG & 256
 { where(); printf("ip_write.c: arp_ip_eth_nonbl(...)= %d\n", result); }
#endif
		if (result == NW_OK)
		{
			ip_port->ip_dl.dl_eth.de_wr_frag= pack;
			if (!(ip_port->ip_dl.dl_eth.de_flags &
				IEF_WRITE_IP))
				dll_eth_write_frame(ip_port);
			return;
		}
	}
	assert (!(ip_port->ip_dl.dl_eth.de_flags & IEF_ARP_MASK));
	ip_port->ip_dl.dl_eth.de_arp_pack= pack;
	ip_port->ip_dl.dl_eth.de_flags |= IEF_ARP_IP;
#if DEBUG & 256
 { where(); printf("ip_write.c: calling arp_ip_eth(...)\n"); }
#endif
	result= arp_ip_eth (ip_port->ip_dl.dl_eth.de_port,
		ip_port-ip_port_table, dst, dll_eth_arp_func);
#if DEBUG & 256
 { where(); printf("ip_write.c: arp_ip_eth(...)= %d\n", result); }
#endif
	if (result == NW_SUSPEND)
		ip_port->ip_dl.dl_eth.de_flags |= IEF_ARP_SP;
	else if (result == EDSTNOTRCH)
	{
		if (ip_port->ip_dl.dl_eth.de_arp_pack)
		{
			bf_afree(ip_port->ip_dl.dl_eth.de_arp_pack);
			ip_port->ip_dl.dl_eth.de_arp_pack= 0;
		}
		ip_port->ip_dl.dl_eth.de_flags &= ~IEF_ARP_MASK;
	}
	else
	{
		assert (result == NW_OK);
		assert (ip_port->ip_dl.dl_eth.de_flags & IEF_WRITE_IP);
	}
}

PUBLIC void dll_eth_write_frame (ip_port)
ip_port_t *ip_port;
{
	acc_t *frag, *frame, *hdr, *tail;
	eth_hdr_t *eth_hdr;
	size_t pack_size;
	int result;

#if DEBUG & 256
 { where(); printf("ip_write.c: dll_eth_write_frame(...)\n"); }
#endif

assert (!(ip_port->ip_dl.dl_eth.de_flags & IEF_WRITE_IP));
	ip_port->ip_dl.dl_eth.de_flags |= IEF_WRITE_IP;

	do
	{
		if (!ip_port->ip_dl.dl_eth.de_wr_frag)
		{
			dll_eth_get_work (ip_port);
			if (!ip_port->ip_dl.dl_eth.de_wr_frag)
			{
				ip_port->ip_dl.dl_eth.de_flags &=
					~IEF_WRITE_IP;
				return;
			}
		}
assert (!ip_port->ip_dl.dl_eth.de_wr_frame);
assert (ip_port->ip_dl.dl_eth.de_wr_frag);

		frag= ip_port->ip_dl.dl_eth.de_wr_frag;
		ip_port->ip_dl.dl_eth.de_wr_frag= 0;
		frame= ip_split_pack(&frag, ETH_MAX_PACK_SIZE-
			ETH_HDR_SIZE);
		if (!frame)
		{
			assert (!frag);
			continue;
		}
		ip_port->ip_dl.dl_eth.de_wr_frag= frag;
		hdr= bf_memreq(ETH_HDR_SIZE);
		eth_hdr= (eth_hdr_t *)ptr2acc_data(hdr);
		eth_hdr->eh_dst= ip_port->ip_dl.dl_eth.de_wr_ethaddr;
		hdr->acc_next= frame;
		frame= hdr;
		hdr= 0;
		pack_size= bf_bufsize(frame);
		if (pack_size<ETH_MIN_PACK_SIZE)
		{
#if DEBUG & 256
 { where(); printf("pack_size= %d\n", pack_size); }
#endif
			tail= bf_memreq(ETH_MIN_PACK_SIZE-pack_size);
			frame= bf_append(frame, tail);
		}
#if DEBUG & 256
 { where(); printf("packet size= %d\n", bf_bufsize(ip_port->ip_dl.
	dl_eth.de_wr_frame)); }
#endif
		ip_port->ip_dl.dl_eth.de_wr_frame= frame;
		ip_port->ip_dl.dl_eth.de_flags &= ~IEF_WRITE_SP;
#if DEBUG & 256
 { where(); printf("ip_write.c: calling eth_write(...)\n"); }
#endif
		result= eth_write (ip_port->ip_dl.dl_eth.de_fd,
			bf_bufsize(ip_port->ip_dl.dl_eth.de_wr_frame));
#if DEBUG & 256
 { where(); printf("ip_write.c: eth_write(...)= %d\n", result); }
#endif
		if (result == NW_SUSPEND)
		{
			ip_port->ip_dl.dl_eth.de_flags |= IEF_WRITE_SP;
			return;
		}
	} while (!ip_port->ip_dl.dl_eth.de_wr_frame);
	ip_port->ip_dl.dl_eth.de_flags &= ~IEF_WRITE_IP;
}

PRIVATE void dll_eth_arp_func (port, ethaddr)
int port;
ether_addr_t *ethaddr;
{
	ip_port_t *ip_port;

#if DEBUG & 256
 { where(); printf("ip_write.c: dll_eth_arp_func(port= %d, ...)\n",
	port); }
#endif
	ip_port= &ip_port_table[port];

	if (ethaddr && ip_port->ip_dl.dl_eth.de_arp_pack)
	{
		ip_port->ip_dl.dl_eth.de_arp_ethaddr= *ethaddr;
		ip_port->ip_dl.dl_eth.de_flags |= IEF_ARP_COMPL;
	}
	else
	{
		if (ip_port->ip_dl.dl_eth.de_arp_pack)
		{
			bf_afree(ip_port->ip_dl.dl_eth.de_arp_pack);
			ip_port->ip_dl.dl_eth.de_arp_pack= 0;
		}
		ip_port->ip_dl.dl_eth.de_flags &= ~IEF_ARP_MASK;
	}
	if (!(ip_port->ip_dl.dl_eth.de_flags & IEF_WRITE_IP))
		dll_eth_write_frame(ip_port);
}

PRIVATE void dll_eth_get_work(ip_port)
ip_port_t *ip_port;
{
	int i;
	ip_fd_t *ip_fd;

	if (ip_port->ip_dl.dl_eth.de_wr_frag)
		return;

	if ((netbroad_flags & NF_INUSE) && netbroad_port == ip_port)
	{
		restart_netbroadcast();
		if (ip_port->ip_dl.dl_eth.de_wr_frag)
			return;
	}
	if (ip_port->ip_dl.dl_eth.de_flags & IEF_ARP_COMPL)
	{
#if DEBUG & 256
 { where(); printf("processing arp_pack\n"); }
#endif
		assert (ip_port->ip_dl.dl_eth.de_arp_pack);
		ip_port->ip_dl.dl_eth.de_wr_ipaddr= (ipaddr_t)0;
		ip_port->ip_dl.dl_eth.de_wr_ethaddr= ip_port->ip_dl.
			dl_eth.de_arp_ethaddr;
		ip_port->ip_dl.dl_eth.de_wr_frag= ip_port->ip_dl.dl_eth.
			de_arp_pack;
		ip_port->ip_dl.dl_eth.de_flags &= ~IEF_ARP_MASK;
		return;
	}
	for (i=0, ip_fd= ip_fd_table; i<IP_FD_NR; i++, ip_fd++)
	{
		if (!(ip_fd->if_flags & IFF_INUSE))
			continue;
		if (!(ip_fd->if_flags & IFF_DLL_WR_IP))
			continue;
		if (ip_fd->if_wr_port != ip_port)
			continue;
#if DEBUG & 256
 { where(); printf("calling restart_fd_write\n"); }
#endif
		restart_fd_write(ip_fd);
		if (ip_port->ip_dl.dl_eth.de_wr_frag)
			return;
	}
}


PRIVATE void restart_netbroad_fd(ip_fd)
ip_fd_t *ip_fd;
{
	assert (!(netbroad_flags & NF_INUSE));
	assert (ip_fd->if_flags & IFF_NETBROAD_IP);
	ip_fd->if_flags &= ~IFF_NETBROAD_IP;
	netbroad_flags |= NF_INUSE;
	netbroad_dst= ip_fd->if_wr_dstaddr;
	netbroad_netmask= ip_get_netmask(netbroad_dst);
	netbroad_pack= get_packet(ip_fd, (int)get_time());
	if (!netbroad_pack)
	{
		netbroad_flags &= ~NF_INUSE;
		return;
	}
	netbroad_port= ip_port_table;
	restart_netbroadcast();

	error_reply(ip_fd, ip_fd->if_wr_count);
}

PRIVATE void restart_fd_write(ip_fd)
ip_fd_t *ip_fd;
{
	ip_port_t *ip_port;
	ipaddr_t dstaddr;
	acc_t *pack;
	int result;

	assert (ip_fd->if_flags & IFF_DLL_WR_IP);

	ip_port= ip_fd->if_wr_port;
	dstaddr= ip_fd->if_wr_dstaddr;
	result= dll_ready(ip_port, dstaddr);
	if (result == NW_SUSPEND)
	{
		return;
	}
	if (result == EDSTNOTRCH)
	{
#if DEBUG
 { where(); printf("dll_ready returned EDSTNOTRCH, gateway= ");
	writeIpAddr(ip_fd->if_wr_dstaddr); printf(", the packet was %s\n",
	(ip_fd->if_flags & IFF_ROUTED) ? "routed" : "not routed"); }
#endif
		if (!(ip_fd->if_flags & IFF_ROUTED))
		{
			error_reply (ip_fd, result);
			return;
		}
		else
		{
			ipr_gateway_down (ip_fd->if_wr_dstaddr,
				IPR_GW_DOWN_TIMEOUT);
			error_reply(ip_fd, NW_OK);
			return;
		}
	}
assert (result == NW_OK);

	ip_fd->if_flags &= ~IFF_DLL_WR_IP;

	ip_port->ip_frame_id++;
	pack= get_packet(ip_fd, ip_port->ip_frame_id);
	if (!pack)
	{
		return;
	}
	dll_write(ip_port, dstaddr, pack);
	error_reply(ip_fd, ip_fd->if_wr_count);
}

PRIVATE void ip_remroute_addr(ip_fd, ttl)
ip_fd_t *ip_fd;
u8_t ttl;
{
	ipaddr_t dstaddr, nexthop;
	ip_port_t *ip_port;
	int result, port;

	dstaddr= ip_fd->if_wr_dstaddr;
	result= iproute_frag (dstaddr, ttl, &nexthop, &port);
#if DEBUG & 256
 { where(); printf("ip_remroute_addr("); writeIpAddr(dstaddr); 
	printf(", %d)= %d\n", ttl, result); }
#endif
	if (result>0)
	{
		ip_port= &ip_port_table[port];
		ip_fd->if_flags |= IFF_DLL_WR_IP|IFF_ROUTED;
		ip_fd->if_wr_dstaddr= nexthop;
		ip_fd->if_wr_port= ip_port;
#if DEBUG & 256
 { where(); printf("calling restart_fd_write\n"); }
#endif
		restart_fd_write(ip_fd);
		return;
	}
	if (result<0)
	{
		error_reply (ip_fd, result);
		return;
	}
#if IP_ROUTER
	ip_panic(( "not implemented" ));
#else
	ip_panic(( "shouldn't be here" ));
#endif
}

PRIVATE acc_t *ip_split_pack (ref_last, first_size)
acc_t **ref_last;
int first_size;
{
	int pack_siz;
	ip_hdr_t *first_hdr, *second_hdr;
	int first_hdr_len, second_hdr_len;
	int first_data_len, second_data_len;
	int new_first_data_len;
	int first_opt_size, second_opt_size;
	acc_t *first_pack, *second_pack, *tmp_pack, *tmp_pack1;
	u8_t *first_optptr, *second_optptr;
	int i, optlen;

	first_pack= *ref_last;
	*ref_last= 0;
	second_pack= 0;

	first_pack= bf_packIffLess(first_pack, IP_MIN_HDR_SIZE);
	assert (first_pack->acc_length >= IP_MIN_HDR_SIZE);

	first_hdr= (ip_hdr_t *)ptr2acc_data(first_pack);
#if DEBUG & 256
 { where(); writeIpAddr(first_hdr->ih_dst); printf("\n"); }
#endif
	first_hdr_len= (first_hdr->ih_vers_ihl & IH_IHL_MASK) * 4;
#if DEBUG & 256
 { where(); printf("fist_hdr_len= %d\n", first_hdr_len); }
#endif
	pack_siz= bf_bufsize(first_pack);
	if (pack_siz > first_size)
	{
#if DEBUG & 256
 { where(); printf("splitting pack\n"); }
#endif
		if (first_hdr->ih_flags_fragoff & HTONS(IH_DONT_FRAG))
		{
assert (!(first_hdr->ih_flags_fragoff) & HTONS(IH_FRAGOFF_MASK));
			icmp_dont_frag(first_pack);
			return 0;
		}
		first_data_len= ntohs(first_hdr->ih_length) - first_hdr_len;
		new_first_data_len= (first_size- first_hdr_len) & ~7;
			/* data goes in 8 byte chuncks */
		second_data_len= first_data_len-new_first_data_len;
		second_pack= bf_cut(first_pack, first_hdr_len+
			new_first_data_len, second_data_len);
		tmp_pack= first_pack;
		first_data_len= new_first_data_len;
		first_pack= bf_cut (tmp_pack, 0, first_hdr_len+first_data_len);
		bf_afree(tmp_pack);
		tmp_pack= bf_memreq(first_hdr_len);
		tmp_pack->acc_next= second_pack;
		second_pack= tmp_pack;
		second_hdr= (ip_hdr_t *)ptr2acc_data(second_pack);
		*second_hdr= *first_hdr;
		second_hdr->ih_flags_fragoff= htons(
			ntohs(first_hdr->ih_flags_fragoff)+(first_data_len/8));

		first_opt_size= first_hdr_len-IP_MIN_HDR_SIZE;
		second_opt_size= 0;
		if (first_opt_size)
		{
			first_pack= bf_packIffLess (first_pack,
				first_hdr_len);
			first_hdr= (ip_hdr_t *)ptr2acc_data(first_pack);
			assert (first_pack->acc_length>=first_hdr_len);
			first_optptr= (u8_t *)ptr2acc_data(first_pack)+
				IP_MIN_HDR_SIZE;
			second_optptr= (u8_t *)ptr2acc_data(
				second_pack)+IP_MIN_HDR_SIZE;
			i= 0;
			while (i<first_opt_size)
			{
				switch (*first_optptr & IP_OPT_NUMBER)
				{
				case 0:
				case 1:
					optlen= 1;
					break;
				default:
					optlen= first_optptr[1];
					break;
				}
				assert (i + optlen <= first_opt_size);
				i += optlen;
				if (*first_optptr & IP_OPT_COPIED)
				{
					second_opt_size += optlen;
					while (optlen--)
						*second_optptr++=
							*first_optptr++;
				}
				else
					first_optptr += optlen;
			}
			while (second_opt_size & 3)
			{
				*second_optptr++= 0;
				second_opt_size++;
			}
		}
		second_hdr_len= IP_MIN_HDR_SIZE + second_opt_size;
#if DEBUG & 256
 { where(); printf("second_opt_size= %d, second_hdr_len= %d\n",
	second_opt_size, second_hdr_len); }
#endif
		second_hdr->ih_vers_ihl= second_hdr->ih_vers_ihl & 0xf0
			+ (second_hdr_len/4);
		second_hdr->ih_length= htons(second_data_len+
			second_hdr_len);
		second_pack->acc_length= second_hdr_len;
		if (first_pack->acc_buffer->buf_linkC>1)
		{
			tmp_pack= bf_cut(first_pack, 0,
				IP_MIN_HDR_SIZE);
			tmp_pack1= bf_cut(first_pack, IP_MIN_HDR_SIZE,
				bf_bufsize(first_pack)-
				IP_MIN_HDR_SIZE);
			bf_afree(first_pack);
#if DEBUG
 { where(); printf("calling bf_pack\n"); }
#endif
			first_pack= bf_pack(tmp_pack);
			first_pack->acc_next= tmp_pack1;
			first_hdr= (ip_hdr_t *)ptr2acc_data(
				first_pack);
		}
		assert (first_pack->acc_buffer->buf_linkC == 1);
		first_hdr->ih_flags_fragoff |= HTONS(IH_MORE_FRAGS);
		first_hdr->ih_length= htons(first_data_len+
			first_hdr_len);
assert (!(second_hdr->ih_flags_fragoff & HTONS(IH_DONT_FRAG)));
	}
	if (first_pack->acc_buffer->buf_linkC>1)
	{
		tmp_pack= bf_cut(first_pack, 0,	IP_MIN_HDR_SIZE);
		tmp_pack1= bf_cut(first_pack, IP_MIN_HDR_SIZE,
			bf_bufsize(first_pack)-IP_MIN_HDR_SIZE);
		bf_afree(first_pack);
#if DEBUG
 { where(); printf("calling bf_pack\n"); }
#endif
		first_pack= bf_pack(tmp_pack);
		first_pack->acc_next= tmp_pack1;
		first_hdr= (ip_hdr_t *)ptr2acc_data(first_pack);
	}
	assert (first_hdr->ih_ttl);
#if DEBUG & 256
 { where(); printf("ip_write.c: ip_split_pack: first_hdr_len= %d\n",
	first_hdr_len); }
#endif
	first_hdr->ih_hdr_chk= 0;
	first_hdr->ih_hdr_chk= ~oneC_sum (0, (u16_t *)first_hdr,
		first_hdr_len);
	*ref_last= second_pack;
	return first_pack;
}

PRIVATE void restart_netbroadcast()
{
	int was_suspended, result, i;
	ip_port_t *ip_port, *hi_port;
	ip_fd_t *ip_fd;

	assert (netbroad_flags & NF_INUSE);
	was_suspended= !!(netbroad_flags & NF_SUSPENDED);
	hi_port= &ip_port_table[IP_PORT_NR];

	for (; netbroad_port < hi_port; netbroad_port++)
	{
		if (!(netbroad_port->ip_flags & IPF_IPADDRSET))
			continue;
		if (!((netbroad_dst ^ netbroad_port->ip_ipaddr) &
			netbroad_netmask))
			continue;
		result= dll_ready (netbroad_port, (ipaddr_t)-1);
		if (result == NW_SUSPEND)
		{
			netbroad_flags |= NF_SUSPENDED;
			return;
		}
assert (result >= 0);

		netbroad_pack->acc_linkC++;
#if DEBUG
 { where(); printf("calling dll_write\n"); }
#endif
		dll_write (netbroad_port, (ipaddr_t)(-1),
			netbroad_pack);
	}
	netbroad_flags &= ~NF_INUSE;
	bf_afree(netbroad_pack);
	netbroad_pack= 0;
	if (!was_suspended)
		return;

	for (i=0, ip_fd= ip_fd_table; i<IP_FD_NR; i++, ip_fd++)
	{
		if (!(ip_fd->if_flags & IFF_INUSE) ||
			!(ip_fd->if_flags & IFF_NETBROAD_IP))
			continue;
		restart_netbroad_fd(ip_fd);
		if (netbroad_flags & NF_INUSE)
			return;
	}
}

PRIVATE void error_reply (ip_fd, error)
ip_fd_t *ip_fd;
int error;
{
	ip_fd->if_flags &= ~IFF_WRITE_MASK;
	if ((*ip_fd->if_get_userdata)(ip_fd->if_srfd, (size_t)error,
		(size_t)0, FALSE))
		ip_panic(( "can't error_reply" ));
}

PRIVATE acc_t *get_packet (ip_fd, id)
ip_fd_t *ip_fd;
u16_t id;
{
	acc_t *pack, *tmp_pack, *tmp_pack1;
	ip_hdr_t *hdr, *tmp_hdr;
	int pack_len, hdr_len, hdr_opt_len, error;

	pack_len= ip_fd->if_wr_count;
	pack= (*ip_fd->if_get_userdata)(ip_fd->if_srfd, (size_t)0,
		pack_len, FALSE);
	if (!pack)
		return pack;
assert(pack_len == bf_bufsize(pack));
	if (ip_fd->if_ipopt.nwio_flags & NWIO_RWDATONLY)
	{
		tmp_pack= bf_memreq (IP_MIN_HDR_SIZE);
		tmp_pack->acc_next= pack;
		pack= tmp_pack;
		pack_len += IP_MIN_HDR_SIZE;
	}
	if (pack_len<IP_MIN_HDR_SIZE)
	{
		bf_afree(pack);
		error_reply(ip_fd, EPACKSIZE);
		return 0;
	}
	pack= bf_packIffLess(pack, IP_MIN_HDR_SIZE);
assert (pack->acc_length >= IP_MIN_HDR_SIZE);
	hdr= (ip_hdr_t *)ptr2acc_data(pack);
	if (pack->acc_linkC != 1 || pack->acc_buffer->buf_linkC != 1)
	{
		tmp_pack= bf_memreq(IP_MIN_HDR_SIZE);
		tmp_hdr= (ip_hdr_t *)ptr2acc_data(tmp_pack);
		*tmp_hdr= *hdr;
		tmp_pack->acc_next= bf_cut(pack, IP_MIN_HDR_SIZE,
			pack_len-IP_MIN_HDR_SIZE);
		bf_afree(pack);
		hdr= tmp_hdr;
#if DEBUG & 256
 { where(); printf("ih_vers_ihl= 0x%x\n", hdr->ih_vers_ihl); }
#endif
		pack= tmp_pack;
assert (pack->acc_length >= IP_MIN_HDR_SIZE);
	}
assert (pack->acc_linkC == 1 && pack->acc_buffer->buf_linkC == 1);

	if (ip_fd->if_ipopt.nwio_flags & NWIO_HDR_O_SPEC)
	{
#if DEBUG & 256
 { where(); printf("ih_vers_ihl= 0x%x\n", hdr->ih_vers_ihl); }
#endif
		hdr_opt_len= ip_fd->if_ipopt.nwio_hdropt.iho_opt_siz;
		if (hdr_opt_len)
		{
			tmp_pack= bf_cut(pack, 0, IP_MIN_HDR_SIZE);
			tmp_pack1= bf_cut (pack, IP_MIN_HDR_SIZE,
				pack_len-IP_MIN_HDR_SIZE);
			bf_afree(pack);
			pack= bf_packIffLess(tmp_pack, IP_MIN_HDR_SIZE);
			hdr= (ip_hdr_t *)ptr2acc_data(pack);
 { where(); printf("ih_vers_ihl= 0x%x\n", hdr->ih_vers_ihl); }
			tmp_pack= bf_memreq (hdr_opt_len);
			memcpy (ptr2acc_data(tmp_pack), ip_fd->if_ipopt.
				nwio_hdropt.iho_data, hdr_opt_len);
 { where(); printf("ih_vers_ihl= 0x%x\n", hdr->ih_vers_ihl); }
			pack->acc_next= tmp_pack;
			tmp_pack->acc_next= tmp_pack1;
			hdr_len= IP_MIN_HDR_SIZE+hdr_opt_len;
		}
		else
			hdr_len= IP_MIN_HDR_SIZE;
		hdr->ih_vers_ihl= hdr_len/4;
#if DEBUG & 256
 { where(); printf("ih_vers_ihl= 0x%x\n", hdr->ih_vers_ihl); }
#endif
		hdr->ih_tos= ip_fd->if_ipopt.nwio_tos;
		hdr->ih_flags_fragoff= 0;
		if (ip_fd->if_ipopt.nwio_df)
			hdr->ih_flags_fragoff |= HTONS(IH_DONT_FRAG);
		hdr->ih_ttl= ip_fd->if_ipopt.nwio_ttl;
#if DEBUG & 256
 { where(); printf("ih_vers_ihl= 0x%x\n", hdr->ih_vers_ihl); }
#endif
	}
	else
	{
assert (ip_fd->if_ipopt.nwio_flags & NWIO_HDR_O_ANY);
		hdr_len= (hdr->ih_vers_ihl & IH_IHL_MASK)*4;
#if DEBUG & 256
 { where(); printf("ih_vers_ihl= 0x%x\n", hdr->ih_vers_ihl); }
#endif
		error= NW_OK;
		if (hdr_len<IP_MIN_HDR_SIZE)
			error= EINVAL;
		else if (hdr_len>pack_len)
			error= EPACKSIZE;
		else if (!hdr->ih_ttl)
			error= EINVAL;
		if (error<0)
		{
			bf_afree(pack);
			error_reply (ip_fd, error);
			return 0;
		}
		pack= bf_packIffLess(pack, hdr_len);
		hdr= (ip_hdr_t *)ptr2acc_data(pack);
#if DEBUG & 256
 { where(); printf("ih_vers_ihl= 0x%x\n", hdr->ih_vers_ihl); }
#endif
		if (hdr_len != IP_MIN_HDR_SIZE)
		{
			error= ip_chk_hdropt((u8_t *)(ptr2acc_data(pack) +
				IP_MIN_HDR_SIZE),
				hdr_len-IP_MIN_HDR_SIZE);
			if (error<0)
			{
				bf_afree(pack);
				error_reply (ip_fd, error);
				return 0;
			}
		}
#if DEBUG & 256
 { where(); printf("ih_vers_ihl= 0x%x\n", hdr->ih_vers_ihl); }
#endif
	}
#if DEBUG & 256
 if (hdr->ih_flags_fragoff & HTONS(IH_DONT_FRAG))
 { where(); printf("proto= %d\n", hdr->ih_proto); }
#endif
assert (!(hdr->ih_flags_fragoff & HTONS(IH_DONT_FRAG)));
#if DEBUG & 256
 { where(); printf("ih_vers_ihl= 0x%x\n", hdr->ih_vers_ihl); }
#endif
	hdr->ih_vers_ihl= (hdr->ih_vers_ihl & IH_IHL_MASK) |
		(IP_VERSION << 4);
#if DEBUG & 256
 { where(); printf("ih_vers_ihl= 0x%x\n", hdr->ih_vers_ihl); }
#endif
	hdr->ih_length= htons(pack_len);
	hdr->ih_flags_fragoff &= ~HTONS(IH_FRAGOFF_MASK |
		IH_FLAGS_UNUSED | IH_MORE_FRAGS);
	if (ip_fd->if_ipopt.nwio_flags & NWIO_PROTOSPEC)
		hdr->ih_proto= ip_fd->if_ipopt.nwio_proto;
	hdr->ih_id= htons(id);
	hdr->ih_src= ip_fd->if_port->ip_ipaddr;
	if (ip_fd->if_ipopt.nwio_flags & NWIO_REMSPEC)
		hdr->ih_dst= ip_fd->if_ipopt.nwio_rem;
	else
	{
assert (ip_fd->if_ipopt.nwio_flags & NWIO_REMANY);
		error= chk_dstaddr(hdr->ih_dst);
		if (error<0)
		{
			bf_afree(pack);
			error_reply(ip_fd, error);
			return 0;
		}
	}
	return pack;
}

PRIVATE chk_dstaddr (dst)
ipaddr_t dst;
{
	ipaddr_t hostrep_dst, netmask;

	hostrep_dst= ntohl(dst);
	if (hostrep_dst == (ipaddr_t)-1)
		return NW_OK;
	if ((hostrep_dst & 0xe0000000l) == 0xe0000000l)
		return EBADDEST;
	netmask= ip_get_netmask(dst);
	if (!(dst & ~netmask))
		return EBADDEST;
	return NW_OK;
}
