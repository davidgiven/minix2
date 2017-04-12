/*
tcp_lib.c
*/

#include "inet.h"
#include "buf.h"
#include "clock.h"
#include "io.h"
#include "type.h"

#include "assert.h"
#include "tcp_int.h"

INIT_PANIC();

PUBLIC int tcp_LEmod4G (n1, n2)
u32_t n1;
u32_t n2;
{
	return !((u32_t)(n2-n1) & 0x80000000L);
}

PUBLIC int tcp_GEmod4G (n1, n2)
u32_t n1;
u32_t n2;
{
	return !((u32_t)(n1-n2) & 0x80000000L);
}

PUBLIC int tcp_Lmod4G (n1, n2)
u32_t n1;
u32_t n2;
{
	return !!((u32_t)(n1-n2) & 0x80000000L);
}

PUBLIC int tcp_Gmod4G (n1, n2)
u32_t n1;
u32_t n2;
{
	return !!((u32_t)(n2-n1) & 0x80000000L);
}

PUBLIC void tcp_extract_ipopt(tcp_conn, ip_hdr)
tcp_conn_t *tcp_conn;
ip_hdr_t *ip_hdr;
{
	int ip_hdr_len;

	ip_hdr_len= (ip_hdr->ih_vers_ihl & IH_IHL_MASK) << 2;
	if (ip_hdr_len == IP_MIN_HDR_SIZE)
		return;

#if DEBUG
 { where(); printf("ip_hdr options NOT supported (yet?)\n"); }
#endif
}

PUBLIC void tcp_extract_tcpopt(tcp_conn, tcp_hdr)
tcp_conn_t *tcp_conn;
tcp_hdr_t *tcp_hdr;
{
	int tcp_hdr_len;

	tcp_hdr_len= (tcp_hdr->th_data_off & TH_DO_MASK) >> 2;
	if (tcp_hdr_len == TCP_MIN_HDR_SIZE)
		return;

#if DEBUG  & 2
 { where(); printf("tcp_hdr options NOT supported (yet?)\n"); }
#endif
}

PUBLIC u16_t tcp_pack_oneCsum(ip_pack, ip_pack_size)
acc_t *ip_pack;
size_t ip_pack_size;
{
	ip_hdr_t *ip_hdr;
	tcp_hdr_t *tcp_hdr;
	size_t ip_hdr_len;
	acc_t *tcp_pack, *save_pack;
	u16_t prev;
	u8_t byte_buf[2];
	u16_t word_buf;
	int odd_byte;
	char *data_ptr;
	int length;


assert (ip_pack);
	ip_pack->acc_linkC++;

	ip_pack= bf_packIffLess(ip_pack, IP_MIN_HDR_SIZE);
	ip_hdr= (ip_hdr_t *)ptr2acc_data(ip_pack);
	ip_hdr_len= (ip_hdr->ih_vers_ihl & IH_IHL_MASK) << 2;

	prev= 0;

	prev= oneC_sum(prev, (u16_t *)&ip_hdr->ih_src,
		sizeof(ip_hdr->ih_src));
		/* Source address in pseudo header */

	prev= oneC_sum(prev, (u16_t *)&ip_hdr->ih_dst,
		sizeof(ip_hdr->ih_dst));
		/* Destination address in pseudo header */
	
	byte_buf[0]= 0;
	byte_buf[1]= ip_hdr->ih_proto;
assert(ip_hdr->ih_proto == IPPROTO_TCP);

	prev= oneC_sum(prev, (u16_t *)byte_buf, 2);
		/* Protocol and zero byte */

	word_buf= htons(ntohs(ip_hdr->ih_length)-ip_hdr_len);
	prev= oneC_sum(prev, &word_buf, sizeof(word_buf));
assert (ntohs(ip_hdr->ih_length) == ip_pack_size);

	tcp_pack= bf_cut(ip_pack, ip_hdr_len, ip_pack_size-ip_hdr_len);
	bf_afree(ip_pack);
	tcp_pack= bf_packIffLess(tcp_pack, TCP_MIN_HDR_SIZE);
	tcp_hdr= (tcp_hdr_t *)ptr2acc_data(tcp_pack);

	save_pack= tcp_pack;

	odd_byte= FALSE;
	for (; tcp_pack; tcp_pack= tcp_pack->acc_next)
	{
		
		data_ptr= ptr2acc_data(tcp_pack);
		length= tcp_pack->acc_length;

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
	bf_afree(save_pack);
	return prev;
}

PUBLIC void tcp_get_ipopt(tcp_conn, ip_hdropt)
tcp_conn_t *tcp_conn;
ip_hdropt_t *ip_hdropt;
{
	if (!tcp_conn->tc_remipopt)
	{
		ip_hdropt->iho_opt_siz= 0;
		return;
	}
#if DEBUG
 { where(); printf("ip_hdr options NOT supported (yet?)\n"); }
#endif
	ip_hdropt->iho_opt_siz= 0;
	return;
}

PUBLIC void tcp_get_tcpopt(tcp_conn, tcp_hdropt)
tcp_conn_t *tcp_conn;
tcp_hdropt_t *tcp_hdropt;
{
	if (!tcp_conn->tc_remtcpopt)
	{
		tcp_hdropt->tho_opt_siz= 0;
		return;
	}

#if DEBUG
 { where(); printf("tcp_hdr options NOT supported (yet?)\n"); }
#endif
	tcp_hdropt->tho_opt_siz= 0;
	return;
}

PUBLIC acc_t *tcp_make_header(tcp_conn, ref_ip_hdr, ref_tcp_hdr, data)
tcp_conn_t *tcp_conn;
ip_hdr_t **ref_ip_hdr;
tcp_hdr_t **ref_tcp_hdr;
acc_t *data;
{
	ip_hdropt_t ip_hdropt;
	tcp_hdropt_t tcp_hdropt;
	ip_hdr_t *ip_hdr;
	tcp_hdr_t *tcp_hdr;
	acc_t *hdr_acc;
	char *ptr2hdr;
	int closed_connection;

	closed_connection= (tcp_conn->tc_state == TCS_CLOSED);

	tcp_get_ipopt (tcp_conn, &ip_hdropt);
	tcp_get_tcpopt (tcp_conn, &tcp_hdropt);
assert (!(ip_hdropt.iho_opt_siz & 3));
assert (!(tcp_hdropt.tho_opt_siz & 3));

	if (IP_MIN_HDR_SIZE+ip_hdropt.iho_opt_siz+
		TCP_MIN_HDR_SIZE+tcp_hdropt.tho_opt_siz <= BUF_S)
	{
		hdr_acc= bf_memreq(IP_MIN_HDR_SIZE+
			ip_hdropt.iho_opt_siz+TCP_MIN_HDR_SIZE+
			tcp_hdropt.tho_opt_siz);
		ptr2hdr= ptr2acc_data(hdr_acc);

		ip_hdr= (ip_hdr_t *)ptr2hdr;
		ptr2hdr += IP_MIN_HDR_SIZE;

		if (ip_hdropt.iho_opt_siz)
		{
#if DEBUG
 { where(); printf("doing memcpy\n"); }
#endif
			memcpy(ptr2hdr, (char *)ip_hdropt.iho_data,
				ip_hdropt.iho_opt_siz);
		}
		ptr2hdr += ip_hdropt.iho_opt_siz;

		tcp_hdr= (tcp_hdr_t *)ptr2hdr;
		ptr2hdr += TCP_MIN_HDR_SIZE;

		if (tcp_hdropt.tho_opt_siz)
		{
#if DEBUG
 { where(); printf("doing memcpy\n"); }
#endif
			memcpy (ptr2hdr, (char *)tcp_hdropt.tho_data,
				tcp_hdropt.tho_opt_siz);
		}
		hdr_acc->acc_next= data;
	}
	else
	{
		hdr_acc= bf_memreq(IP_MIN_HDR_SIZE+
			ip_hdropt.iho_opt_siz);
		ptr2hdr= ptr2acc_data(hdr_acc);

		ip_hdr= (ip_hdr_t *)ptr2hdr;
		ptr2hdr += IP_MIN_HDR_SIZE;

		if (ip_hdropt.iho_opt_siz)
		{
#if DEBUG
 { where(); printf("doing memcpy\n"); }
#endif
			memcpy (ptr2hdr, (char *)ip_hdropt.iho_data,
				ip_hdropt.iho_opt_siz);
		}
		
		hdr_acc->acc_next= bf_memreq(TCP_MIN_HDR_SIZE+
			tcp_hdropt.tho_opt_siz);

		ptr2hdr= ptr2acc_data(hdr_acc->acc_next);
		tcp_hdr= (tcp_hdr_t *)ptr2hdr;
		ptr2hdr += TCP_MIN_HDR_SIZE;

		if (tcp_hdropt.tho_opt_siz)
		{
#if DEBUG
 { where(); printf("doing memcpy\n"); }
#endif
			memcpy (ptr2hdr, (char *)tcp_hdropt.tho_data,
				tcp_hdropt.tho_opt_siz);
		}
		hdr_acc->acc_next->acc_next= data;
	}
	if (!closed_connection && (tcp_conn->tc_state == TCS_CLOSED))
	{
#if DEBUG
 { where(); printf("connection closed while inuse\n"); }
#endif
		bf_afree(hdr_acc);
		return 0;
	}

	ip_hdr->ih_vers_ihl= (IP_MIN_HDR_SIZE+
		ip_hdropt.iho_opt_siz) >> 2;
	ip_hdr->ih_tos= tcp_conn->tc_tos;
	ip_hdr->ih_ttl= tcp_conn->tc_ttl;
	ip_hdr->ih_proto= IPPROTO_TCP;
	ip_hdr->ih_src= tcp_conn->tc_locaddr;
	ip_hdr->ih_dst= tcp_conn->tc_remaddr;
	ip_hdr->ih_flags_fragoff= 0;

	tcp_hdr->th_srcport= tcp_conn->tc_locport;
	tcp_hdr->th_dstport= tcp_conn->tc_remport;
	tcp_hdr->th_seq_nr= tcp_conn->tc_RCV_NXT;
	tcp_hdr->th_flags= 0;
	tcp_hdr->th_data_off= (TCP_MIN_HDR_SIZE+
		tcp_hdropt.tho_opt_siz) << 2;
	tcp_hdr->th_window= htons(tcp_conn->tc_RCV_HI-
		tcp_conn->tc_RCV_LO);
	tcp_hdr->th_chksum= 0;
	*ref_ip_hdr= ip_hdr;
	*ref_tcp_hdr= tcp_hdr;
	return hdr_acc;
}

PUBLIC void tcp_write_state (tcp_conn)
tcp_conn_t *tcp_conn;
{
	printf("tcp_conn_table[%d]->tc_state= ", tcp_conn-
		tcp_conn_table);
	if (!(tcp_conn->tc_flags & TCF_INUSE))
	{
		printf("not inuse\n");
		return;
	}
	switch (tcp_conn->tc_state)
	{
	case TCS_CLOSED: printf("CLOSED"); break;
	case TCS_LISTEN: printf("LISTEN"); break;
	case TCS_SYN_RECEIVED: printf("SYN_RECEIVED"); break;
	case TCS_SYN_SENT: printf("SYN_SENT"); break;
	case TCS_ESTABLISHED: printf("ESTABLISHED"); break;
	case TCS_FIN_WAIT_1: printf("FIN_WAIT_1"); break;
	case TCS_FIN_WAIT_2: printf("FIN_WAIT_2"); break;
	case TCS_CLOSE_WAIT: printf("CLOSE_WAIT"); break;
	case TCS_CLOSING: printf("CLOSING"); break;
	case TCS_LAST_ACK: printf("LAST_ACK"); break;
	case TCS_TIME_WAIT: printf("TIME_WAIT"); break;
	default: printf("unknown (=%d)", tcp_conn->tc_state); break;
	}
	printf("\n");
}

PUBLIC int tcp_check_conn(tcp_conn)
tcp_conn_t *tcp_conn;
{
	int allright;
	u32_t lo_queue, hi_queue;
	int size;

#if DEBUG & 256
 { where(); printf("in tcp_check_conn\n"); }
#endif
	allright= TRUE;

	/* checking receive queue */
	lo_queue= tcp_conn->tc_RCV_LO;
	if (lo_queue == tcp_conn->tc_IRS)
		lo_queue++;
	if (lo_queue == tcp_conn->tc_RCV_NXT && (tcp_conn->tc_flags &
		TCF_FIN_RECV))
		lo_queue--;
	hi_queue= tcp_conn->tc_RCV_NXT;
	if (hi_queue == tcp_conn->tc_IRS)
		hi_queue++;
	if (tcp_conn->tc_flags & TCF_FIN_RECV)
		hi_queue--;

	size= hi_queue-lo_queue;
	if (size<0)
	{
		printf("RCV_NXT-RCV_LO < 0\n");
		allright= FALSE;
	}
	else if (!tcp_conn->tc_rcvd_data)
	{
		if (size)
		{
			printf("RCV_NXT-RCV_LO != 0\n");
			tcp_print_conn(tcp_conn);
			printf("lo_queue= %lu, hi_queue= %lu\n",
				lo_queue, hi_queue);
			allright= FALSE;
		}
	}
	else if (size != bf_bufsize(tcp_conn->tc_rcvd_data))
	{
		printf("RCV_NXT-RCV_LO != sizeof tc_rcvd_data\n");
		tcp_print_conn(tcp_conn);
		printf(
		"lo_queue= %lu, hi_queue= %lu, sizeof tc_rcvd_data= %d\n",
			lo_queue, hi_queue, bf_bufsize(tcp_conn->tc_rcvd_data));
		allright= FALSE;
	}

	/* checking send data */
	lo_queue= tcp_conn->tc_SND_UNA;
	if (lo_queue == tcp_conn->tc_ISS)
		lo_queue++;
	if (lo_queue == tcp_conn->tc_SND_NXT && (tcp_conn->tc_flags &
		TCF_FIN_SENT))
		lo_queue--;
	hi_queue= tcp_conn->tc_SND_NXT;
	if (hi_queue == tcp_conn->tc_ISS)
		hi_queue++;
	if (tcp_conn->tc_flags & TCF_FIN_SENT)
		hi_queue--;

	size= hi_queue-lo_queue;
	if (size<0)
	{
		printf("SND_NXT-SND_UNA < 0\n");
		allright= FALSE;
	}
	else if (!tcp_conn->tc_send_data)
	{
		if (size)
		{
			printf("SND_NXT-SND_UNA != 0\n");
			printf("SND_NXT= %d, SND_UNA= %d\n", 
				tcp_conn->tc_SND_NXT, tcp_conn->tc_SND_UNA);
			printf("lo_queue= %d, hi_queue= %d\n", 
				lo_queue, hi_queue);
			allright= FALSE;
		}
	}
	else if (size != bf_bufsize(tcp_conn->tc_send_data))
	{
		printf("SND_NXT-SND_UNA != sizeof tc_send_data\n");
		printf("SND_NXT= %d, SND_UNA= %d\n", 
			tcp_conn->tc_SND_NXT, tcp_conn->tc_SND_UNA);
		printf("lo_queue= %d, lo_queue= %d\n", 
			lo_queue, hi_queue);
		printf("bf_bufsize(data)= %d\n", 
			bf_bufsize(tcp_conn->tc_send_data));
		
		allright= FALSE;
	}

	/* checking counters */
	if (!tcp_GEmod4G(tcp_conn->tc_SND_UNA, tcp_conn->tc_ISS))
	{
		printf("SND_UNA < ISS\n");
		allright= FALSE;
	}
	if (!tcp_GEmod4G(tcp_conn->tc_SND_NXT, tcp_conn->tc_SND_UNA))
	{
		printf("SND_NXT<SND_UNA\n");
		allright= FALSE;
	}
	if (!tcp_GEmod4G(tcp_conn->tc_SND_TRM, tcp_conn->tc_SND_UNA))
	{
		printf("SND_TRM<SND_UNA\n");
		allright= FALSE;
	}
	if (!tcp_GEmod4G(tcp_conn->tc_SND_NXT, tcp_conn->tc_SND_TRM))
	{
		printf("SND_NXT<SND_TRM\n");
		allright= FALSE;
	}

#if DEBUG
 if (!allright)
 { printf("tcp_check_conn: not allright\n"); }
#endif
	return allright;
}

PUBLIC void tcp_print_pack(ip_hdr, tcp_hdr)
ip_hdr_t *ip_hdr;
tcp_hdr_t *tcp_hdr;
{
	int tcp_hdr_len;

assert(tcp_hdr);
	if (ip_hdr)
		writeIpAddr(ip_hdr->ih_src);
	else
		printf("???");
	printf(",%u ", ntohs(tcp_hdr->th_srcport));
	if (ip_hdr)
		writeIpAddr(ip_hdr->ih_dst);
	else
		printf("???");
	printf(",%u ", ntohs(tcp_hdr->th_dstport));
	printf(" 0x%lx", ntohl(tcp_hdr->th_seq_nr));
	if (tcp_hdr->th_flags & THF_FIN)
		printf(" <FIN>");
	if (tcp_hdr->th_flags & THF_SYN)
		printf(" <SYN>");
	if (tcp_hdr->th_flags & THF_RST)
		printf(" <RST>");
	if (tcp_hdr->th_flags & THF_PSH)
		printf(" <PSH>");
	if (tcp_hdr->th_flags & THF_ACK)
		printf(" <ACK 0x%x %u>", ntohl(tcp_hdr->th_ack_nr),
			ntohs(tcp_hdr->th_window));
	if (tcp_hdr->th_flags & THF_URG)
		printf(" <URG %u>", tcp_hdr->th_urgptr);
	tcp_hdr_len= (tcp_hdr->th_data_off & TH_DO_MASK) >> 2;
	if (tcp_hdr_len != TCP_MIN_HDR_SIZE)
		printf(" <options %d>", tcp_hdr_len-TCP_MIN_HDR_SIZE);
}

PUBLIC void tcp_print_conn(tcp_conn)
tcp_conn_t *tcp_conn;
{
	int iss, irs;

	iss= tcp_conn->tc_ISS;
	irs= tcp_conn->tc_IRS;

	tcp_write_state (tcp_conn);
	printf(" ISS 0x%lx UNA +0x%lx TRM +0x%lx NXT +0x%lx",
		iss, tcp_conn->tc_SND_UNA-iss, tcp_conn->tc_SND_TRM-iss,
		tcp_conn->tc_SND_NXT-iss);
	printf(" IRS 0x%lx LO +0x%x NXT +0x%x HI +0x%x",
		irs, tcp_conn->tc_RCV_LO-irs, tcp_conn->tc_RCV_NXT-irs,
		tcp_conn->tc_RCV_HI-irs);
	if (tcp_conn->tc_flags & TCF_INUSE)
		printf(" TCF_INUSE");
	if (tcp_conn->tc_flags & TCF_FIN_RECV)
		printf(" TCF_FIN_RECV");
	if (tcp_conn->tc_flags & TCF_RCV_PUSH)
		printf(" TCF_RCV_PUSH");
	if (tcp_conn->tc_flags & TCF_MORE2WRITE)
		printf(" TCF_MORE2WRITE");
	if (tcp_conn->tc_flags & TCF_SEND_ACK)
		printf(" TCF_SEND_ACK");
	if (tcp_conn->tc_flags & TCF_FIN_SENT)
		printf(" TCF_FIN_SENT");
	if (tcp_conn->tc_flags & TCF_ACK_TIMER_SET)
		printf(" TCF_ACK_TIMER_SET");
}
