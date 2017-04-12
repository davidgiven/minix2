/*
ip_read.c
*/

#include "inet.h"
#include "buf.h"
#include "clock.h"
#include "type.h"

#include "assert.h"
#include "icmp_lib.h"
#include "io.h"
#include "ip.h"
#include "ip_int.h"

INIT_PANIC();

FORWARD ip_ass_t *find_ass_ent ARGS(( ip_port_t *port, U16_t id,
	int proto, ipaddr_t src, ipaddr_t dst ));
FORWARD acc_t *merge_frags ARGS(( acc_t *first, acc_t *second ));
FORWARD int net_broad ARGS(( ipaddr_t hoaddr, ipaddr_t netaddr,
	ipaddr_t netmask ));
FORWARD int ip_frag_chk ARGS(( acc_t *pack ));
FORWARD acc_t *reassemble ARGS(( ip_port_t *port, acc_t *pack, 
	ip_hdr_t *ip_hdr ));
FORWARD int ok_for_port ARGS(( ip_port_t *port, ipaddr_t ipaddr,
	int *ref_broad_all ));

PUBLIC int ip_read (fd, count)
int fd;
size_t count;
{
	ip_fd_t *ip_fd;

	ip_fd= &ip_fd_table[fd];
	if (!(ip_fd->if_flags & IFF_OPTSET))
		return (*ip_fd->if_put_userdata)(ip_fd->if_srfd, EBADMODE,
			(acc_t *)0, FALSE);

	ip_fd->if_rd_count= count;

	if (ip_fd->if_rd_buf)
	{
		if (get_time() <= ip_fd->if_exp_tim)
			return ip_packet2user (ip_fd);
	 where();
		bf_afree(ip_fd->if_rd_buf);
	 where();
		ip_fd->if_rd_buf= 0;
	}
	ip_fd->if_flags |= IFF_READ_IP;
#if DEBUG & 256
 { where(); printf("ip_fd_table[%d].if_flags= 0x%x\n",
	ip_fd-ip_fd_table, ip_fd->if_flags); }
#endif
	return NW_SUSPEND;
}

PRIVATE acc_t *reassemble (port, pack, pack_hdr)
ip_port_t *port;
acc_t *pack;
ip_hdr_t *pack_hdr;
{
	ip_ass_t *ass_ent;
	size_t pack_hdr_len, pack_data_len, pack_offset, tmp_offset;
	u16_t pack_flags_fragoff;
	acc_t *prev_head, *new_head, *new_tail, *tmp_acc;
	acc_t swap_acc;
	ip_hdr_t *tmp_hdr;
	time_t first_time;

#if DEBUG & 256
 { where(); printf("in reassemble()\n"); }
#endif
	ass_ent= find_ass_ent (port, pack_hdr->ih_id,
		pack_hdr->ih_proto, pack_hdr->ih_src, pack_hdr->ih_dst);
#if DEBUG & 256
 { where(); ip_print_frags(ass_ent->ia_frags); printf("\n"); }
#endif

	pack_flags_fragoff= ntohs(pack_hdr->ih_flags_fragoff);
	pack_hdr_len= (pack_hdr->ih_vers_ihl & IH_IHL_MASK) * 4;
	pack_data_len= ntohs(pack_hdr->ih_length)-pack_hdr_len;
	pack_offset= (pack_flags_fragoff & IH_FRAGOFF_MASK)*8;
	pack->acc_ext_link= NULL;
#if DEBUG & 256
 { where(); ip_print_frags(pack); printf("\n"); }
#endif

	new_head= 0;

	for (prev_head= ass_ent->ia_frags, ass_ent->ia_frags= NULL; prev_head;
		prev_head= prev_head->acc_ext_link)
	{
		tmp_hdr= (ip_hdr_t *)ptr2acc_data(prev_head);
		tmp_offset= (ntohs(tmp_hdr->ih_flags_fragoff) &
			IH_FRAGOFF_MASK)*8;
#if DEBUG & 256
 { where(); printf("tmp_offset= %d, pack_offset= %d\n", tmp_offset, 
	pack_offset); }
#endif


		if (tmp_offset >= pack_offset)
			break;

		if (new_head)
			new_tail->acc_ext_link= prev_head;
		else
			new_head= prev_head;
		new_tail= prev_head;
	}
	if (prev_head)
	{
where();
		pack= merge_frags(pack, prev_head);
	}
	if (new_head)
	{
		pack= merge_frags(new_tail, pack);
		if (pack != new_tail)
		{
			swap_acc= *pack;
			*pack= *new_tail;
			*new_tail= swap_acc;
		}
	}
	else
	{
		new_head= pack;
		new_tail= pack;
	}
	ass_ent->ia_frags= new_head;
#if DEBUG & 256
 { where(); ip_print_frags(ass_ent->ia_frags); printf("\n"); }
#endif

	pack= ass_ent->ia_frags;
	pack_hdr= (ip_hdr_t *)ptr2acc_data(pack);
	pack_flags_fragoff= ntohs(pack_hdr->ih_flags_fragoff);
#if DEBUG & 256
 { where(); printf(
		"merge_pack: flags_fragoff= %u, vers_ihl= 0x%x, length= %u\n",
	pack_flags_fragoff, pack_hdr->ih_vers_ihl,
	ntohs(pack_hdr->ih_length)); }
#endif

	if (!(pack_flags_fragoff & (IH_FRAGOFF_MASK|IH_MORE_FRAGS)))
		/* it's now a complete packet */
	{
#if DEBUG & 256
 { where(); printf("got a complete packet now\n"); }
#endif
		first_time= ass_ent->ia_first_time;

		ass_ent->ia_frags= 0;
		ass_ent->ia_first_time= 0;

		while (pack->acc_ext_link)
		{
 { where(); printf("strange\n"); }
			tmp_acc= pack->acc_ext_link;
			pack->acc_ext_link= tmp_acc->acc_ext_link;
			bf_afree(tmp_acc);
		}
		if ((ass_ent->ia_min_ttl) * HZ + first_time <
			get_time())
			icmp_frag_ass_tim(pack);
		else
			return pack;
	}
	return NULL;
}

PRIVATE acc_t *merge_frags (first, second)
acc_t *first, *second;
{
	ip_hdr_t *first_hdr, *second_hdr;
	size_t first_hdr_size, second_hdr_size, first_datasize, second_datasize,
		first_offset, second_offset;
	acc_t *cut_second, *tmp_acc;

#if DEBUG & 256
 { where(); ip_print_frags(first); printf(" , "); ip_print_frags(second); }
#endif
	if (!second)
	{
		first->acc_ext_link= NULL;
		return first;
	}

assert (first->acc_length >= IP_MIN_HDR_SIZE);
assert (second->acc_length >= IP_MIN_HDR_SIZE);

	first_hdr= (ip_hdr_t *)ptr2acc_data(first);
	first_offset= (ntohs(first_hdr->ih_flags_fragoff) &
		IH_FRAGOFF_MASK) * 8;
	first_hdr_size= (first_hdr->ih_vers_ihl & IH_IHL_MASK) * 4;
	first_datasize= ntohs(first_hdr->ih_length) - first_hdr_size;

	for (;;)
	{
		second_hdr= (ip_hdr_t *)ptr2acc_data(second);
		second_offset= (ntohs(second_hdr->ih_flags_fragoff) &
			IH_FRAGOFF_MASK) * 8;
		second_hdr_size= (second_hdr->ih_vers_ihl & IH_IHL_MASK) * 4;
		second_datasize= ntohs(second_hdr->ih_length) - second_hdr_size;

#if DEBUG
 if (second_offset <= first_offset)
 { where(); printf ("first_offset= %u, second_offset= %u\n",
	first_offset, second_offset);
 printf ("first_hdr_size= %u, second_hdr_size= %u\n",
	first_hdr_size, second_hdr_size);
 printf ("first_datasize= %u, second_datasize= %u\n",
	first_datasize, second_datasize); }
#endif
assert (first_hdr_size + first_datasize == bf_bufsize(first));
assert (second_hdr_size + second_datasize == bf_bufsize(second));
assert (second_offset > first_offset);

		if (second_offset > first_offset+first_datasize)
		{
			first->acc_ext_link= second;
			return first;
		}

		if (second_offset + second_datasize <= first_offset +
			first_datasize)
		{
			first->acc_ext_link= second->acc_ext_link;
			bf_afree(second);
			break;
		}

		if (!(second_hdr->ih_flags_fragoff & HTONS(IH_MORE_FRAGS)))
			first_hdr->ih_flags_fragoff &= ~HTONS(IH_MORE_FRAGS);

		second_datasize= second_offset+second_datasize-(first_offset+
			first_datasize);
		cut_second= bf_cut(second, second_hdr_size + first_offset+
			first_datasize-second_offset, second_datasize);
		tmp_acc= second->acc_ext_link;
		bf_afree(second);
		second= tmp_acc;

		first_datasize += second_datasize;
		first_hdr->ih_length= htons(first_hdr_size + first_datasize);

		first= bf_append (first, cut_second);
		if (!second)
		{
			first->acc_ext_link= NULL;
			break;
		}
assert (first->acc_length >= IP_MIN_HDR_SIZE);
		first_hdr= (ip_hdr_t *)ptr2acc_data(first);
	}
assert (first_hdr_size + first_datasize == bf_bufsize(first));
	return first;
}

PRIVATE ip_ass_t *find_ass_ent (port, id, proto, src, dst)
ip_port_t *port;
u16_t id;
ipproto_t proto;
ipaddr_t src;
ipaddr_t dst;
{
	ip_ass_t *new_ass_ent, *tmp_ass_ent;
	int i;
	acc_t *tmp_acc, *curr_acc;

#if DEBUG & 256
 { where(); printf("find_ass_ent (.., id= %u, proto= %u, src= ",
	id, ntohs(proto)); writeIpAddr(src); printf(" dst= ");
	writeIpAddr(dst); printf(")\n"); }
#endif
	new_ass_ent= 0;

	for (i=0, tmp_ass_ent= ip_ass_table; i<IP_ASS_NR; i++,
		tmp_ass_ent++)
	{
		if (!tmp_ass_ent->ia_frags && tmp_ass_ent->
			ia_first_time)
		{
#if DEBUG
 { where(); printf("ip.c: strange ip_ass entry (can be a race condition)\n"); }
#endif
			continue;
		}

		if ((tmp_ass_ent->ia_srcaddr == src) &&
			(tmp_ass_ent->ia_dstaddr == dst) &&
			(tmp_ass_ent->ia_proto == proto) &&
			(tmp_ass_ent->ia_id == id) &&
			(tmp_ass_ent->ia_port == port))
		{
#if DEBUG & 256
 { where(); printf("found an ass_ent\n"); }
#endif
			return tmp_ass_ent;
		}
		if (!new_ass_ent || tmp_ass_ent->ia_first_time <
			new_ass_ent->ia_first_time)
			new_ass_ent= tmp_ass_ent;
	}
#if DEBUG & 256
 { where(); printf("made an ass_ent\n"); }
#endif
	new_ass_ent->ia_min_ttl= IP_MAX_TTL;
	new_ass_ent->ia_port= port;
	new_ass_ent->ia_first_time= get_time();
	new_ass_ent->ia_srcaddr= src;
	new_ass_ent->ia_dstaddr= dst;
	new_ass_ent->ia_proto= proto;
	new_ass_ent->ia_id= id;

	if (new_ass_ent->ia_frags)
	{
		curr_acc= new_ass_ent->ia_frags->acc_ext_link;
		while (curr_acc)
		{
			tmp_acc= curr_acc->acc_ext_link;
			bf_afree(curr_acc);
			curr_acc= tmp_acc;
		}
		curr_acc= new_ass_ent->ia_frags;
		new_ass_ent->ia_frags= 0;
		icmp_frag_ass_tim(curr_acc);
	}
	return new_ass_ent;
}

PUBLIC void ip_eth_arrived(ip_port, pack)
ip_port_t *ip_port;
acc_t *pack;
{
	ip_hdr_t *ip_hdr;
	int for_this_port, broadcast_allowed, broadcast_pack;
	int ip_frag_len, ip_hdr_len;
	acc_t *ip_acc, *eth_acc;
	ether_addr_t eth_dst, eth_src;
	eth_hdr_t *eth_hdr;
	size_t pack_size;

#if DEBUG & 256
 { where(); printf("ip_eth_arrived(&ip_port_table[%d], packet_length= %d)\n",
	ip_port-ip_port_table, bf_bufsize(pack)); }
#endif
	pack= bf_packIffLess(pack, ETH_HDR_SIZE);
assert (pack->acc_length >= ETH_HDR_SIZE);

	eth_hdr= (eth_hdr_t *)ptr2acc_data(pack);
	eth_dst= eth_hdr->eh_dst;
	eth_src= eth_hdr->eh_src;
	if (eth_dst.ea_addr[0] & 0x01)
		broadcast_pack= TRUE;
	else
		broadcast_pack= FALSE;

	pack_size= bf_bufsize(pack);
	eth_acc= bf_cut(pack, 0, ETH_HDR_SIZE);
	ip_acc= bf_cut(pack, ETH_HDR_SIZE, pack_size-ETH_HDR_SIZE);
	pack_size -= ETH_HDR_SIZE;
#if DEBUG & 256
 { where(); printf("packet_length= %d\n", bf_bufsize(ip_acc)); }
#endif
	bf_afree(pack);

	if (pack_size < IP_MIN_HDR_SIZE)
	{
#if DEBUG
 { where(); printf("wrong acc_length\n"); }
#endif
		bf_afree(ip_acc);
		bf_afree(eth_acc);
		return;
	}
	ip_acc= bf_packIffLess(ip_acc, IP_MIN_HDR_SIZE);
assert (ip_acc->acc_length >= IP_MIN_HDR_SIZE);

	ip_hdr= (ip_hdr_t *)ptr2acc_data(ip_acc);
	ip_hdr_len= (ip_hdr->ih_vers_ihl & IH_IHL_MASK) << 2;
	if (ip_hdr_len>IP_MIN_HDR_SIZE)
	{
		ip_acc= bf_packIffLess(ip_acc, ip_hdr_len);
		ip_hdr= (ip_hdr_t *)ptr2acc_data(ip_acc);
	}
#if DEBUG & 256
 { where(); printf("ih_vers_ihl= 0x%x\n", ip_hdr->ih_vers_ihl); }
#endif
	ip_frag_len= ntohs(ip_hdr->ih_length);
	if (ip_frag_len<pack_size)
	{
		pack= ip_acc;
		ip_acc= bf_cut(pack, 0, ip_frag_len);
		bf_afree(pack);
	}

#if DEBUG & 256
 { where(); printf("ip_frag_len= %d, packet length= %d\n", ip_frag_len, bf_bufsize(ip_acc)); }
#endif
	if (!ip_frag_chk(ip_acc))
	{
#if DEBUG
 { where(); where(); printf("fragment not allright\n"); }
#endif
		bf_afree(ip_acc);
		bf_afree(eth_acc);
		return;
	}
	for_this_port= ok_for_port(ip_port, ip_hdr->ih_dst,
		&broadcast_allowed);

	if (!broadcast_allowed && broadcast_pack)
	{
		printf("got eth-broadcast pack for ip-nonbroadcast addr, src=");
		writeIpAddr(ip_hdr->ih_src);
		printf(" dst=");
		writeIpAddr(ip_hdr->ih_dst);
		printf("\n");
		bf_afree(ip_acc);
		bf_afree(eth_acc);
		return;
	}
#if !IP_ROUTER
	if (!for_this_port)
	{
#if DEBUG
 { where(); printf("ip.c: got strange packet, src="); 
   writeIpAddr(ip_hdr->ih_src); printf(" dst="); writeIpAddr(ip_hdr->ih_dst);
   printf(" src_eth= "); writeEtherAddr(&eth_src); printf(" dst_eth= ");
   writeEtherAddr(&eth_dst); printf("\n"); }
#endif
		bf_afree(ip_acc);
		bf_afree(eth_acc);
		return;
	}
#else
	if (!for_this_port)
	{
		bf_afree(eth_acc);
		ip_route(ip_port, ip_acc);
		return;
	}
#endif /* !IP_ROUTER */
	bf_afree(eth_acc);
	if (ntohs(ip_hdr->ih_flags_fragoff) & (IH_FRAGOFF_MASK|IH_MORE_FRAGS))
	{
#if DEBUG & 256
 { where(); printf("reassembling\n"); }
#endif
		ip_acc= reassemble (ip_port, ip_acc, ip_hdr);
		if (!ip_acc)
			return;
assert (ip_acc->acc_length >= IP_MIN_HDR_SIZE);
	ip_hdr= (ip_hdr_t *)ptr2acc_data(ip_acc);
assert (!(ntohs(ip_hdr->ih_flags_fragoff) & (IH_FRAGOFF_MASK|IH_MORE_FRAGS)));
	}
	ip_port_arrive (ip_port, ip_acc, ip_hdr);
}

PRIVATE int ok_for_port (ip_port, ipaddr, ref_broad_all)
ip_port_t *ip_port;
ipaddr_t ipaddr;
int *ref_broad_all;
{
	ipaddr_t netmask;

#if DEBUG & 256
 { where(); printf("ok_for_port( .., "); writeIpAddr(ipaddr);
   printf(", ..)\nip_port->ip_ipaddr= "); 
   writeIpAddr(ip_port->ip_ipaddr); printf("\n"); }
#endif
	if (ipaddr == ip_port->ip_ipaddr)
		*ref_broad_all= FALSE;
	else if (ipaddr == (ipaddr_t)-1)
		*ref_broad_all= TRUE;
	else if (net_broad (ipaddr, ip_port->ip_ipaddr &
		ip_port->ip_netmask, ip_port->ip_netmask))
		*ref_broad_all= TRUE;
	else
	{
		netmask= ip_get_netmask(ipaddr);
		if (!net_broad (ipaddr, ip_port->ip_ipaddr & netmask,
			netmask))
			return FALSE;
		*ref_broad_all= TRUE;
	}
	return TRUE;
}

PRIVATE int ip_frag_chk(pack)
acc_t *pack;
{
	ip_hdr_t *ip_hdr;
	int hdr_len;

	if (pack->acc_length < sizeof(ip_hdr_t))
	{
#if DEBUG
 { where(); printf("wrong length\n"); }
#endif
		return FALSE;
	}

	ip_hdr= (ip_hdr_t *)ptr2acc_data(pack);

	hdr_len= (ip_hdr->ih_vers_ihl & IH_IHL_MASK) * 4;
	if (pack->acc_length < hdr_len)
	{
#if DEBUG
 { where(); printf("wrong length\n"); }
#endif
		return FALSE;
	}

	if (((ip_hdr->ih_vers_ihl >> 4) & IH_VERSION_MASK) !=
		IP_VERSION)
	{
#if DEBUG
 { where(); printf("wrong version (ih_vers_ihl=0x%x)\n",ip_hdr->ih_vers_ihl); }
#endif
		return FALSE;
	}
	if (ntohs(ip_hdr->ih_length) != bf_bufsize(pack))
	{
#if DEBUG
 { where(); printf("wrong size\n"); }
#endif
		return FALSE;
	}
	if ((u16_t)~oneC_sum(0, (u16_t *)ip_hdr, hdr_len))
	{
#if DEBUG
 { where(); printf("packet with wrong checksum (= %x)\n", 
   (u16_t)~oneC_sum(0, (u16_t *)ip_hdr, hdr_len)); }
#endif
		return FALSE;
	}
	if (hdr_len>IP_MIN_HDR_SIZE && ip_chk_hdropt((u8_t *)
		(ptr2acc_data(pack) + IP_MIN_HDR_SIZE),
		hdr_len-IP_MIN_HDR_SIZE))
	{
#if DEBUG
 { where(); printf("packet with wrong options\n"); }
#endif
		return FALSE;
	}
	return TRUE;
}

PRIVATE int net_broad (hostaddr, netaddr, netmask)
ipaddr_t hostaddr, netaddr, netmask;
{
	if ((hostaddr & netmask) != (netaddr & netmask))
		return FALSE;
	if ((hostaddr & ~netmask) == ~netmask)
		return TRUE;
#if IP_SUN_BROADCAST
	if ((hostaddr & ~netmask) == 0)
		return TRUE;
#endif
	return FALSE;
}

PUBLIC int ip_packet2user (ip_fd)
ip_fd_t *ip_fd;
{
	acc_t *pack, *tmp_pack;
	ip_hdr_t *hdr;
	int result, hdr_len;
	size_t size, transf_size;

	pack= ip_fd->if_rd_buf;
	ip_fd->if_rd_buf= 0;

	size= bf_bufsize (pack);

	if (ip_fd->if_ipopt.nwio_flags & NWIO_RWDATONLY)
	{

		pack= bf_packIffLess (pack, IP_MIN_HDR_SIZE);
		assert (pack->acc_length >= IP_MIN_HDR_SIZE);

		hdr= (ip_hdr_t *)ptr2acc_data(pack);
		hdr_len= (hdr->ih_vers_ihl & IH_IHL_MASK) * 4;

		assert (size>= hdr_len);
		size -= hdr_len;
		tmp_pack= bf_cut(pack, hdr_len, size);
		bf_afree(pack);
		pack= tmp_pack;
	}

	if (size>ip_fd->if_rd_count)
	{
		tmp_pack= bf_cut (pack, 0, ip_fd->if_rd_count);
		bf_afree(pack);
		pack= tmp_pack;
		transf_size= ip_fd->if_rd_count;
	}
	else
		transf_size= size;

	result= (*ip_fd->if_put_userdata)(ip_fd->if_srfd,
		(size_t)0, pack, FALSE);
	if (result >= 0)
		if (size > transf_size)
			result= EPACKSIZE;
		else
			result= transf_size;

#if DEBUG & 256
 { where(); printf("packet2user cleared IFF_READ_IP\n"); }
#endif
	ip_fd->if_flags &= ~IFF_READ_IP;
	result= (*ip_fd->if_put_userdata)(ip_fd->if_srfd, result,
			(acc_t *)0, FALSE);
	assert (result >= 0);

	return result;
}

PUBLIC int ip_ok_for_fd (ip_fd, pack)
ip_fd_t *ip_fd;
acc_t *pack;
{
	ip_port_t *ip_port;
	ip_hdr_t *hdr;
	ipaddr_t dst;
	unsigned long pack_kind, nwio_flags;


	assert (pack->acc_length >= IP_MIN_HDR_SIZE);

	ip_port= ip_fd->if_port;

	hdr= (ip_hdr_t *)ptr2acc_data(pack);
	dst= hdr->ih_dst;
	if (dst == ip_port->ip_ipaddr)
		pack_kind= NWIO_EN_LOC;
	else
		pack_kind= NWIO_DI_LOC;

	nwio_flags= ip_fd->if_ipopt.nwio_flags;
	if (!(pack_kind & nwio_flags))
		return FALSE;

	if ((nwio_flags & NWIO_PROTOSPEC) &&
		(hdr->ih_proto != ip_fd->if_ipopt.nwio_proto))
		return FALSE;

	if ((nwio_flags & NWIO_REMSPEC) &&
		(hdr->ih_src != ip_fd->if_ipopt.nwio_rem))
		return FALSE;

	return TRUE;
}

PUBLIC void ip_port_arrive (ip_port, pack, ip_hdr)
ip_port_t *ip_port;
acc_t *pack;
ip_hdr_t *ip_hdr;
{
	ip_fd_t *ip_fd, *share_fd;
	ip_hdr_t *hdr;
	int port_nr;
	unsigned long ip_pack_stat;
	int i;
	ipproto_t proto;
	time_t exp_tim;

assert (pack->acc_linkC>0);

#if DEBUG & 256
 { where(); printf ("in ip_port_arrive()\n"); }
#endif
assert (pack->acc_length >= IP_MIN_HDR_SIZE);

	exp_tim= get_time() + (ip_hdr->ih_ttl+1) * HZ;

	if (ip_hdr->ih_dst == ip_port->ip_ipaddr)
		ip_pack_stat= NWIO_EN_LOC;
	else
		ip_pack_stat= NWIO_EN_BROAD;

	proto= ip_hdr->ih_proto;

#if DEBUG & 256
 { where(); printf("proto= %d\n", proto); }
#endif

	share_fd= 0;
	for (i=0, ip_fd=ip_fd_table; i<IP_FD_NR; i++, ip_fd++)
	{
		if (!(ip_fd->if_flags & IFF_INUSE))
		{
			continue;
		}
#if DEBUG & 256
 { where(); printf("ip_fd_table[%d].if_flags= 0x%x\n",
	ip_fd-ip_fd_table, ip_fd->if_flags); }
#endif
		if (!(ip_fd->if_flags & IFF_OPTSET))
		{
#if DEBUG & 256
 { where(); printf("%d options not set\n", i); }
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
		if (!(ip_fd->if_ipopt.nwio_flags & ip_pack_stat))
		{
#if DEBUG & 256
 { where(); printf("%d wrong ip_pack_stat\n", i); }
#endif
			continue;
		}
		if ((ip_fd->if_ipopt.nwio_flags & NWIO_PROTOSPEC) &&
			proto != ip_fd->if_ipopt.nwio_proto)
		{
#if DEBUG & 256
 { where(); printf("%d wrong proto\n", i); }
#endif
			continue;
		}
		if ((ip_fd->if_ipopt.nwio_flags & NWIO_REMSPEC) &&
			ip_hdr->ih_src != ip_fd->if_ipopt.nwio_rem)
		{
#if DEBUG
 { where(); printf("%d wrong src addr (REMSPEC)\n", i); }
#endif
			continue;
		}
		if (ip_fd->if_rd_buf)
		{
			if ((ip_fd->if_ipopt.nwio_flags &
				NWIO_ACC_MASK) == NWIO_SHARED)
			{
#if DEBUG
 { where(); printf("%d shared packet\n", i); }
#endif
				share_fd= ip_fd;
				continue;
			}
#if DEBUG
 { where(); printf("throwing away packet\n"); }
#endif
			bf_afree(ip_fd->if_rd_buf);
		}
		ip_fd->if_rd_buf= pack;
		pack->acc_linkC++;
		ip_fd->if_exp_tim= exp_tim;

		if ((ip_fd->if_ipopt.nwio_flags & NWIO_ACC_MASK) == 
			NWIO_SHARED || (ip_fd->if_ipopt.nwio_flags &
			NWIO_ACC_MASK) ==  NWIO_EXCL)
		{
#if DEBUG
 { where(); printf("exclusive packet\n"); }
#endif
			bf_afree(pack);
			pack= 0;
			break;
		}

		if (ip_fd->if_flags & IFF_READ_IP)
		{
#if DEBUG & 256
 { where(); printf("%d calling packet2user\n", i); }
#endif
			ip_packet2user(ip_fd);
		}
		else
		{
#if DEBUG
 { where(); printf("%d not READ_IP\n", i); }
#endif
		}
	}
	if (share_fd && pack)
	{
#if DEBUG
 { where(); printf("exclusive packet\n"); }
#endif
		bf_afree(share_fd->if_rd_buf);
		share_fd->if_rd_buf= pack;
		share_fd->if_exp_tim= exp_tim;
	}
	else
	{
#if DEBUG & 256
 { where(); printf("throwing away packet\n"); }
#endif
		if (pack)
			bf_afree(pack);
	}
}
