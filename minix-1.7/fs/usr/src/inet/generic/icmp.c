/*
icmp.c
*/

#include "inet.h"
#include "buf.h"
#include "type.h"

#include "assert.h"
#include "clock.h"
#include "icmp.h"
#include "icmp_lib.h"
#include "io.h"
#include "ip.h"
#include "ip_int.h"
#include "ipr.h"

INIT_PANIC();

typedef struct icmp_port
{
	int icp_flags;
	int icp_state;
	int icp_ipport;
	int icp_ipfd;
	acc_t *icp_head_queue;
	acc_t *icp_tail_queue;
	acc_t *icp_write_pack;
} icmp_port_t;

#define ICPF_EMPTY	0x0
#define ICPF_SUSPEND	0x1
#define ICPF_READ_IP	0x2
#define ICPF_READ_SP	0x4
#define ICPF_WRITE_IP	0x8
#define ICPF_WRITE_SP	0x10

#define ICPS_BEGIN	0
#define ICPS_IPOPT	1
#define ICPS_MAIN	2
#define ICPS_ERROR	3

#define ICMP_PORT_NR	IP_PORT_NR

PRIVATE  icmp_port_t icmp_port_table[ICMP_PORT_NR];

FORWARD void icmp_main ARGS(( icmp_port_t *icmp_port ));
FORWARD acc_t *icmp_getdata ARGS(( int port, size_t offset,
	size_t count, int for_ioctl ));
FORWARD int icmp_putdata ARGS(( int port, size_t offset,
	acc_t *data, int for_ioctl ));
FORWARD void icmp_read ARGS(( icmp_port_t *icmp_port ));
FORWARD void process_data ARGS(( icmp_port_t *icmp_port,
	acc_t *data ));
FORWARD u16_t icmp_pack_oneCsum ARGS(( acc_t *ip_pack ));
FORWARD void icmp_echo_request ARGS(( icmp_port_t *icmp_port,
	acc_t *ip_pack, int ip_hdr_len, ip_hdr_t *ip_hdr,
	acc_t *icmp_pack, int icmp_len, icmp_hdr_t *icmp_hdr ));
FORWARD void icmp_dst_unreach ARGS(( icmp_port_t *icmp_port,
	acc_t *ip_pack, int ip_hdr_len, ip_hdr_t *ip_hdr,
	acc_t *icmp_pack, int icmp_len, icmp_hdr_t *icmp_hdr ));
FORWARD void icmp_time_exceeded ARGS(( icmp_port_t *icmp_port,
	acc_t *ip_pack, int ip_hdr_len, ip_hdr_t *ip_hdr,
	acc_t *icmp_pack, int icmp_len, icmp_hdr_t *icmp_hdr ));
FORWARD void icmp_router_advertisement ARGS(( icmp_port_t *icmp_port,
	acc_t *icmp_pack, int icmp_len, icmp_hdr_t *icmp_hdr ));
FORWARD void icmp_redirect ARGS(( icmp_port_t *icmp_port,
	ip_hdr_t *ip_hdr, acc_t *icmp_pack, int icmp_len,
	icmp_hdr_t *icmp_hdr ));
FORWARD acc_t *make_repl_ip ARGS(( ip_hdr_t *ip_hdr,
	int ip_len ));
FORWARD void enqueue_pack ARGS(( icmp_port_t *icmp_port,
	acc_t *reply_ip_hdr ));
FORWARD void icmp_write ARGS(( icmp_port_t *icmp_port ));
FORWARD void icmp_buffree ARGS(( int priority, size_t reqsize ));

PUBLIC void icmp_init()
{
	int i;
	icmp_port_t *icmp_port;

	assert (BUF_S >= sizeof (nwio_ipopt_t));

	for (i= 0, icmp_port= icmp_port_table; i<ICMP_PORT_NR; i++,
		icmp_port++)
	{
		icmp_port->icp_flags= ICPF_EMPTY;
		icmp_port->icp_state= ICPS_BEGIN;
		icmp_port->icp_ipport= i;
	}

	bf_logon(icmp_buffree);

	for (i= 0, icmp_port= icmp_port_table; i<ICMP_PORT_NR; i++,
		icmp_port++)
	{
		icmp_main (icmp_port);
	}
}

PRIVATE void icmp_main(icmp_port)
icmp_port_t *icmp_port;
{
	int result;
	switch (icmp_port->icp_state)
	{
	case ICPS_BEGIN:
		icmp_port->icp_head_queue= 0;
		icmp_port->icp_ipfd= ip_open (icmp_port->icp_ipport,
			icmp_port-icmp_port_table, icmp_getdata,
			icmp_putdata);
		if (icmp_port->icp_ipfd<0)
		{
			where();
			printf("unable to open ip_port %d\n",
				icmp_port->icp_ipport);
			break;
		}
		icmp_port->icp_state= ICPS_IPOPT;
		icmp_port->icp_flags &= ~ICPF_SUSPEND;
		result= ip_ioctl (icmp_port->icp_ipfd, NWIOSIPOPT);
		if (result == NW_SUSPEND)
		{
			icmp_port->icp_flags |= ICPF_SUSPEND;
			break;
		}
		else if (result<0)
		{
			where();
			printf("ip_ioctl (.., NWIOSIPOPT)= %d\n",
				result);
			break;
		}
		/* falls through */
	case ICPS_IPOPT:
		icmp_port->icp_state= ICPS_MAIN;
		icmp_port->icp_flags &= ~ICPF_SUSPEND;
		icmp_read(icmp_port);
		break;
	default:
		where();
		printf("unknown state %d\n", icmp_port->icp_state);
		break;
	}
}

PRIVATE acc_t *icmp_getdata(port, offset, count, for_ioctl)
int port;
size_t offset, count;
int for_ioctl;
{
	icmp_port_t *icmp_port;
	nwio_ipopt_t *ipopt;
	acc_t *data;
	int result;

	icmp_port= &icmp_port_table[port];

	if (icmp_port->icp_flags & ICPF_WRITE_IP)
	{
		if (!count)
		{
			bf_afree(icmp_port->icp_write_pack);
			icmp_port->icp_write_pack= 0;

			result= (int)offset;
			if (result<0)
			{
				where();
				printf("got write error %d\n", result);
			}
#if DEBUG & 256
 { where(); printf("ip_write completed\n"); }
#endif
			if (icmp_port->icp_flags & ICPF_WRITE_SP)
			{
				icmp_port->icp_flags &=
					~(ICPF_WRITE_IP|ICPF_WRITE_SP);
				icmp_write (icmp_port);
			}
			return NW_OK;
		}
		return bf_cut(icmp_port->icp_write_pack, offset, count);
	}
	switch (icmp_port->icp_state)
	{
	case ICPS_IPOPT:
		if (!count)
		{
			result= (int)offset;
			if (result < 0)
			{
				icmp_port->icp_state= ICPS_ERROR;
				break;
			}
			if (icmp_port->icp_flags & ICPF_SUSPEND)
				icmp_main(icmp_port);
			return NW_OK;
		}

assert (count == sizeof (*ipopt));
		data= bf_memreq (sizeof (*ipopt));
assert (data->acc_length == sizeof(*ipopt));
		ipopt= (nwio_ipopt_t *)ptr2acc_data(data);
		ipopt->nwio_flags= NWIO_COPY | NWIO_EN_LOC |
			NWIO_EN_BROAD | NWIO_REMANY | NWIO_PROTOSPEC |
			NWIO_HDR_O_ANY | NWIO_RWDATALL;
		ipopt->nwio_proto= IPPROTO_ICMP;
		return data;
	default:
		where();
		printf("unknown state %d\n", icmp_port->icp_state);
		return 0;
	}
}

PRIVATE int icmp_putdata(port, offset, data, for_ioctl)
int port;
size_t offset;
acc_t *data;
int for_ioctl;
{
	icmp_port_t *icmp_port;
	int result;

	icmp_port= &icmp_port_table[port];

	if (icmp_port->icp_flags & ICPF_READ_IP)
	{
assert (!for_ioctl);
		if (!data)
		{
			result= (int)offset;
			if (result<0)
			{
				where();
				printf("got read error %d\n", result);
			}
			if (icmp_port->icp_flags & ICPF_READ_SP)
			{
				icmp_port->icp_flags &=
					~(ICPF_READ_IP|ICPF_READ_SP);
				icmp_read (icmp_port);
			}
			return NW_OK;
		}
		process_data(icmp_port, data);
		return NW_OK;
	}
	switch (icmp_port->icp_state)
	{
	default:
		where();
		printf("unknown state %d\n", icmp_port->icp_state);
		return 0;
	}
}

PRIVATE void icmp_read(icmp_port)
icmp_port_t *icmp_port;
{
	int result;

assert (!(icmp_port->icp_flags & (ICPF_READ_IP|ICPF_READ_SP) || 
	(icmp_port->icp_flags & (ICPF_READ_IP|ICPF_READ_SP)) ==
	(ICPF_READ_IP|ICPF_READ_SP)));

	for (;;)
	{
		icmp_port->icp_flags |= ICPF_READ_IP;
		icmp_port->icp_flags &= ~ICPF_READ_SP;

		result= ip_read(icmp_port->icp_ipfd, ICMP_MAX_DATAGRAM);
		if (result == NW_SUSPEND)
		{
			icmp_port->icp_flags |= ICPF_READ_SP;
			return;
		}
	}
}

PUBLIC void icmp_frag_ass_tim(pack)
acc_t *pack;
{
	ip_warning(( "icmp_frag_ass() called" ));
	bf_afree(pack);
}

PUBLIC void icmp_getnetmask(ip_port)
int ip_port;
{
	ip_port_t *port;

#if DEBUG & 256
 { where(); printf("icmp.c: icmp_getnetmask(ip_port= %d)\n", ip_port); }
#endif
	port= &ip_port_table[ip_port];
#if DEBUG & 2
 { where(); printf ("icmp_getnetmask() NOT implemented\n"); }
#endif

	port->ip_netmask= HTONL(0xffffff00L);
	port->ip_flags |= IPF_NETMASKSET;
#if DEBUG & 256
 { where(); printf("icmp.c: setting netmask to "); 
   writeIpAddr(port->ip_netmask); printf("\n"); }
#endif
}

PUBLIC void icmp_dont_frag(pack)
acc_t *pack;
{
printf ("icmp_dont_frag() called\n");
	bf_afree(pack);
}

PUBLIC void icmp_ttl_exceded(pack)
acc_t *pack;
{
printf ("icmp_ttl_execeded() called\n");
	bf_afree(pack);
}

PRIVATE void process_data(icmp_port, data)
icmp_port_t *icmp_port;
acc_t *data;
{
	ip_hdr_t *ip_hdr;
	icmp_hdr_t *icmp_hdr;
	acc_t *icmp_data;
	int ip_hdr_len;
	size_t pack_len;

#if DEBUG & 256
 { where(); printf("got an icmp packet\n"); }
#endif
	data= bf_packIffLess(data, IP_MIN_HDR_SIZE);
	ip_hdr= (ip_hdr_t *)ptr2acc_data(data);
	ip_hdr_len= (ip_hdr->ih_vers_ihl & IH_IHL_MASK) << 2;

	if (ip_hdr_len>IP_MIN_HDR_SIZE)
	{
		data= bf_packIffLess(data, ip_hdr_len);
		ip_hdr= (ip_hdr_t *)ptr2acc_data(data);
	}

	pack_len= bf_bufsize(data);
	pack_len -= ip_hdr_len;
	if (pack_len < ICMP_MIN_HDR_LEN)
	{
#if DEBUG
 { where(); printf("got an incomplete icmp packet\n"); }
#endif
		bf_afree(data);
		return;
	}

	icmp_data= bf_cut(data, ip_hdr_len, pack_len);

	icmp_data= bf_packIffLess (icmp_data, ICMP_MIN_HDR_LEN);
	icmp_hdr= (icmp_hdr_t *)ptr2acc_data(icmp_data);


	if ((u16_t)~icmp_pack_oneCsum(icmp_data))
	{
#if DEBUG
 { where(); printf("got packet with bad checksum (= 0x%x)\n",
	(u16_t)~icmp_pack_oneCsum(icmp_data)); }
#endif
		bf_afree(data);
		bf_afree(icmp_data);
		return;
	}
	switch (icmp_hdr->ih_type)
	{
	case ICMP_TYPE_ECHO_REPL:
#if DEBUG
 { where(); printf("got an icmp echo reply\n"); }
#endif
		break;
	case ICMP_TYPE_DST_UNRCH:
		icmp_dst_unreach (icmp_port, data, ip_hdr_len, ip_hdr,
			icmp_data, pack_len, icmp_hdr);
		break;
	case ICMP_TYPE_REDIRECT:
		icmp_redirect (icmp_port, ip_hdr, icmp_data, pack_len,
			icmp_hdr);
		break;
	case ICMP_TYPE_ECHO_REQ:
		icmp_echo_request(icmp_port, data, ip_hdr_len, ip_hdr,
			icmp_data, pack_len, icmp_hdr);
		return;
	case ICMP_TYPE_ROUTER_ADVER:
		icmp_router_advertisement(icmp_port, icmp_data, pack_len, 
			icmp_hdr);
		break;
	case ICMP_TYPE_TIME_EXCEEDED:
		icmp_time_exceeded (icmp_port, data, ip_hdr_len, ip_hdr,
			icmp_data, pack_len, icmp_hdr);
		break;
	default:
#if DEBUG
 { where(); printf("got an unknown icmp (%d) from ", icmp_hdr->ih_type); 
	writeIpAddr(ip_hdr->ih_src); printf("\n"); }
#endif
		break;
	}
	bf_afree(data);
	bf_afree(icmp_data);
}

PRIVATE void icmp_echo_request(icmp_port, ip_data, ip_len, ip_hdr,
	icmp_data, icmp_len, icmp_hdr)
icmp_port_t *icmp_port;
acc_t *ip_data, *icmp_data;
int ip_len, icmp_len;
ip_hdr_t *ip_hdr;
icmp_hdr_t *icmp_hdr;
{
	acc_t *repl_ip_hdr, *repl_icmp;
	icmp_hdr_t *repl_icmp_hdr;
	u32_t tmp_chksum;
	u16_t u16;

	if (icmp_hdr->ih_code != 0)
	{
#if DEBUG
 { where(); printf("got an icmp echo request with unknown code (%d)\n",
		icmp_hdr->ih_code); }
#endif
		bf_afree(ip_data);
		bf_afree(icmp_data);
		return;
	}
	if (icmp_len < ICMP_MIN_HDR_LEN + sizeof(icmp_id_seq_t))
	{
#if DEBUG
 { where(); printf("got an incomplete icmp echo request\n"); }
#endif
		bf_afree(ip_data);
		bf_afree(icmp_data);
		return;
	}
#if DEBUG & 256
 { where(); printf("got an icmp echo request, ident= %u, seq= %u\n",
	ntohs(icmp_hdr->ih_hun.ihh_idseq.iis_id),
	ntohs(icmp_hdr->ih_hun.ihh_idseq.iis_seq)); }
#endif
	repl_ip_hdr= make_repl_ip(ip_hdr, ip_len);
	repl_icmp= bf_memreq (ICMP_MIN_HDR_LEN);
assert (repl_icmp->acc_length == ICMP_MIN_HDR_LEN);
	repl_icmp_hdr= (icmp_hdr_t *)ptr2acc_data(repl_icmp);
	repl_icmp_hdr->ih_type= ICMP_TYPE_ECHO_REPL;
	repl_icmp_hdr->ih_code= 0;

	tmp_chksum= ~icmp_hdr->ih_chksum - *(u16_t *)&icmp_hdr->ih_type+
		*(u16_t *)&repl_icmp_hdr->ih_type;
	tmp_chksum= (tmp_chksum >> 16) + (tmp_chksum & 0xffff);
	tmp_chksum= (tmp_chksum >> 16) + (tmp_chksum & 0xffff);
	repl_icmp_hdr->ih_chksum= ~tmp_chksum;

	repl_ip_hdr->acc_next= repl_icmp;
	repl_icmp->acc_next= bf_cut (icmp_data, ICMP_MIN_HDR_LEN,
		icmp_len - ICMP_MIN_HDR_LEN);

	bf_afree(ip_data);
	bf_afree(icmp_data);

	enqueue_pack(icmp_port, repl_ip_hdr);
}

PRIVATE u16_t icmp_pack_oneCsum(icmp_pack)
acc_t *icmp_pack;
{
	u16_t prev;
	int odd_byte;
	char *data_ptr;
	int length;
	char byte_buf[2];

	assert (icmp_pack);

	prev= 0;

	odd_byte= FALSE;
	for (; icmp_pack; icmp_pack= icmp_pack->acc_next)
	{
		
		data_ptr= ptr2acc_data(icmp_pack);
		length= icmp_pack->acc_length;

		if (!length)
			continue;
		if (odd_byte)
		{
			byte_buf[1]= *data_ptr;
			prev= oneC_sum(prev, (u16_t *)byte_buf, 2);
			data_ptr++;
			length--;
			odd_byte= FALSE;
		}
		if (length & 1)
		{
			odd_byte= TRUE;
			length--;
			byte_buf[0]= data_ptr[length];
		}
		if (!length)
			continue;
		prev= oneC_sum (prev, (u16_t *)data_ptr, length);
	}
	if (odd_byte)
		prev= oneC_sum (prev, (u16_t *)byte_buf, 1);
	return prev;
}

PRIVATE acc_t *make_repl_ip(ip_hdr, ip_len)
ip_hdr_t *ip_hdr;
int ip_len;
{
	ip_hdr_t *repl_ip_hdr;
	acc_t *repl;
	int repl_hdr_len;

	if (ip_len>IP_MIN_HDR_SIZE)
	{
#if DEBUG
 { where(); printf("ip_hdr options NOT supported (yet?)\n"); }
#endif
		ip_len= IP_MIN_HDR_SIZE;
	}

	repl_hdr_len= IP_MIN_HDR_SIZE;

	repl= bf_memreq(repl_hdr_len);
assert (repl->acc_length == repl_hdr_len);

	repl_ip_hdr= (ip_hdr_t *)ptr2acc_data(repl);

	repl_ip_hdr->ih_vers_ihl= repl_hdr_len >> 2;
	repl_ip_hdr->ih_tos= ip_hdr->ih_tos;
	repl_ip_hdr->ih_ttl= ICMP_DEF_TTL;
	repl_ip_hdr->ih_proto= IPPROTO_ICMP;
	repl_ip_hdr->ih_dst= ip_hdr->ih_src;
	repl_ip_hdr->ih_flags_fragoff= 0;

	return repl;
}

PRIVATE void enqueue_pack(icmp_port, reply_ip_hdr)
icmp_port_t *icmp_port;
acc_t *reply_ip_hdr;
{
	reply_ip_hdr->acc_ext_link= 0;

	if (icmp_port->icp_head_queue)
	{
		icmp_port->icp_tail_queue->acc_ext_link=
			reply_ip_hdr;
	}
	else
	{
		icmp_port->icp_head_queue= reply_ip_hdr;
		icmp_port->icp_tail_queue= reply_ip_hdr;
	}

	if (!(icmp_port->icp_flags & ICPF_WRITE_IP))
		icmp_write(icmp_port);
}

PRIVATE void icmp_write(icmp_port)
icmp_port_t *icmp_port;
{
	int result;

assert (!(icmp_port->icp_flags & (ICPF_WRITE_IP|ICPF_WRITE_SP) || 
	(icmp_port->icp_flags & (ICPF_WRITE_IP|ICPF_WRITE_SP)) ==
	(ICPF_WRITE_IP|ICPF_WRITE_SP)));

	for (;icmp_port->icp_head_queue;)
	{
		icmp_port->icp_write_pack= icmp_port->icp_head_queue;
		icmp_port->icp_head_queue= icmp_port->icp_head_queue->
			acc_ext_link;

		icmp_port->icp_flags |= ICPF_WRITE_IP;
		icmp_port->icp_flags &= ~ICPF_WRITE_SP;

#if DEBUG & 256
 { where(); printf("calling ip_write\n"); }
#endif
		result= ip_write(icmp_port->icp_ipfd,
			bf_bufsize(icmp_port->icp_write_pack));
		if (result == NW_SUSPEND)
		{
#if DEBUG & 256
 { where(); printf("ip_write replied NW_SUSPEND\n"); }
#endif
			icmp_port->icp_flags |= ICPF_WRITE_SP;
			return;
		}
#if DEBUG & 256
 { where(); printf("ip_write done\n"); }
#endif
	}
	icmp_port->icp_flags &= ~ICPF_WRITE_IP;
}

PRIVATE void icmp_buffree(priority, reqsize)
int priority;
size_t reqsize;
{
	acc_t *tmp_acc;
	int donesomething,i;
	icmp_port_t *icmp_port;

	donesomething= 0;

	if (priority < ICMP_PRI_QUEUE)
		return;

	while (bf_free_buffsize < reqsize)
	{
		for (i=0, icmp_port= icmp_port_table; i<ICMP_PORT_NR;
			i++, icmp_port++)
		{
			if (icmp_port->icp_head_queue)
			{
				tmp_acc= icmp_port->icp_head_queue;
				icmp_port->icp_head_queue= tmp_acc->
					acc_ext_link;
				bf_afree(tmp_acc);
				if (bf_free_buffsize >= reqsize)
					break;
				donesomething= 1;
			}
		}
		if (!donesomething)
			break;
	}
}

static void icmp_dst_unreach(icmp_port, ip_pack, ip_hdr_len, ip_hdr, icmp_pack,
	icmp_len, icmp_hdr)
icmp_port_t *icmp_port;
acc_t *ip_pack;
int ip_hdr_len;
ip_hdr_t *ip_hdr;
acc_t *icmp_pack;
int icmp_len;
icmp_hdr_t *icmp_hdr;
{
	acc_t *old_ip_pack;
	ip_hdr_t *old_ip_hdr;

	if (icmp_len < 8 + IP_MIN_HDR_SIZE)
	{
#if DEBUG
 { where(); printf("dest unrch with wrong size\n"); }
#endif
		return;
	}
	old_ip_pack= bf_cut (icmp_pack, 8, icmp_len-8);
	old_ip_pack= bf_packIffLess(old_ip_pack, IP_MIN_HDR_SIZE);
	old_ip_hdr= (ip_hdr_t *)ptr2acc_data(old_ip_pack);

	if (old_ip_hdr->ih_src != ip_hdr->ih_dst)
	{
#if DEBUG
 { where(); printf("dest unrch based on wrong packet\n"); }
#endif
		bf_afree(old_ip_pack);
		return;
	}

	switch(icmp_hdr->ih_code)
	{
	case ICMP_NET_UNRCH:
		ipr_destunrch (old_ip_hdr->ih_dst,
			ip_get_netmask(old_ip_hdr->ih_dst), IPR_UNRCH_TIMEOUT);
		break;
	case ICMP_HOST_UNRCH:
		ipr_destunrch (old_ip_hdr->ih_dst, (ipaddr_t)-1,
			IPR_UNRCH_TIMEOUT);
		break;
	default:
#if DEBUG
 { where(); printf("got strange code: %d\n", icmp_hdr->ih_code); }
#endif
		break;
	}
	bf_afree(old_ip_pack);
}

static void icmp_time_exceeded(icmp_port, ip_pack, ip_hdr_len, ip_hdr,
	icmp_pack, icmp_len, icmp_hdr)
icmp_port_t *icmp_port;
acc_t *ip_pack;
int ip_hdr_len;
ip_hdr_t *ip_hdr;
acc_t *icmp_pack;
int icmp_len;
icmp_hdr_t *icmp_hdr;
{
	acc_t *old_ip_pack;
	ip_hdr_t *old_ip_hdr;

	if (icmp_len < 8 + IP_MIN_HDR_SIZE)
	{
#if DEBUG
 { where(); printf("time exceeded with wrong size\n"); }
#endif
		return;
	}
	old_ip_pack= bf_cut (icmp_pack, 8, icmp_len-8);
	old_ip_pack= bf_packIffLess(old_ip_pack, IP_MIN_HDR_SIZE);
	old_ip_hdr= (ip_hdr_t *)ptr2acc_data(old_ip_pack);

	if (old_ip_hdr->ih_src != ip_hdr->ih_dst)
	{
#if DEBUG
 { where(); printf("time exceeded based on wrong packet\n"); }
#endif
		bf_afree(old_ip_pack);
		return;
	}

	switch(icmp_hdr->ih_code)
	{
	case ICMP_TTL_EXC:
		ipr_ttl_exc (old_ip_hdr->ih_dst, (ipaddr_t)-1,
			IPR_TTL_TIMEOUT);
		break;
	default:
		where();
		printf("got strange code: %d\n", icmp_hdr->ih_code);
		break;
	}
	bf_afree(old_ip_pack);
}

static void icmp_router_advertisement(icmp_port, icmp_pack, icmp_len, icmp_hdr)
icmp_port_t *icmp_port;
acc_t *icmp_pack;
int icmp_len;
icmp_hdr_t *icmp_hdr;
{
	int entries;
	int entry_size;
	u16_t lifetime;
	int i;
	char *bufp;

	if (icmp_len < 8)
	{
#if DEBUG
 { where(); printf("router advertisement with wrong size (%d)\n", icmp_len); }
#endif
		return;
	}
	if (icmp_hdr->ih_code != 0)
	{
#if DEBUG
 { where(); printf("router advertisement with wrong code (%d)\n", 
							icmp_hdr->ih_code); }
#endif
		return;
	}
	entries= icmp_hdr->ih_hun.ihh_ram.iram_na;
	entry_size= icmp_hdr->ih_hun.ihh_ram.iram_aes * 4;
	if (entries < 1)
	{
#if DEBUG
 { where(); printf("router advertisement with wrong number of entries (%d)\n", 
							entries); }
#endif
		return;
	}
	if (entry_size < 8)
	{
#if DEBUG
 { where(); printf("router advertisement with wrong entry size (%d)\n", 
							entry_size); }
#endif
		return;
	}
	if (icmp_len < 8 + entries * entry_size)
	{
#if DEBUG
 { where(); printf("router advertisement with wrong size\n"); 
	printf("\t(entries= %d, entry_size= %d, icmp_len= %d)\n", entries,
						entry_size, icmp_len); }
#endif
		return;
	}
	lifetime= ntohs(icmp_hdr->ih_hun.ihh_ram.iram_lt);
	if (lifetime > 9000)
	{
#if DEBUG
 { where(); printf("router advertisement with wrong lifetime (%d)\n",
								lifetime); }
#endif
		return;
	}
	for (i= 0, bufp= (char *)&icmp_hdr->ih_dun.uhd_data[0]; i< entries; i++,
		bufp += entry_size)
	{
		ipr_add_route(HTONL(0L), HTONL(0L), *(ipaddr_t *)bufp,
			icmp_port->icp_ipport, lifetime * HZ, 1, 0, 
			ntohl(*(i32_t *)(bufp+4)));
	}
}
		
static void icmp_redirect(icmp_port, ip_hdr, icmp_pack, icmp_len, icmp_hdr)
icmp_port_t *icmp_port;
ip_hdr_t *ip_hdr;
acc_t *icmp_pack;
int icmp_len;
icmp_hdr_t *icmp_hdr;
{
	acc_t *old_ip_pack;
	ip_hdr_t *old_ip_hdr;
	int port;

	if (icmp_len < 8 + IP_MIN_HDR_SIZE)
	{
#if DEBUG
 { where(); printf("redirect with wrong size\n"); }
#endif
		return;
	}
	old_ip_pack= bf_cut (icmp_pack, 8, icmp_len-8);
	old_ip_pack= bf_packIffLess(old_ip_pack, IP_MIN_HDR_SIZE);
	old_ip_hdr= (ip_hdr_t *)ptr2acc_data(old_ip_pack);

	port= icmp_port->icp_ipport;

	switch(icmp_hdr->ih_code)
	{
	case ICMP_REDIRECT_NET:
		ipr_redirect (old_ip_hdr->ih_dst,
			ip_get_netmask(old_ip_hdr->ih_dst),
			ip_hdr->ih_src, icmp_hdr->ih_hun.ihh_gateway, port, 
			IPR_REDIRECT_TIMEOUT);
		break;
	case ICMP_REDIRECT_HOST:
		ipr_redirect (old_ip_hdr->ih_dst, (ipaddr_t)-1,
			ip_hdr->ih_src, icmp_hdr->ih_hun.ihh_gateway, port, 
			IPR_REDIRECT_TIMEOUT);
		break;
	default:
		where();
		printf("got strange code: %d\n", icmp_hdr->ih_code);
		break;
	}
	bf_afree(old_ip_pack);
}
