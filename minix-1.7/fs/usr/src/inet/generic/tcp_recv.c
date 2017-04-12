/*
tcp_recv.c
*/

#include "inet.h"
#include "buf.h"
#include "clock.h"
#include "type.h"

#include "tcp_int.h"
#include "tcp.h"
#include "assert.h"

INIT_PANIC();

FORWARD void create_RST ARGS(( tcp_conn_t *tcp_conn,
	ip_hdr_t *ip_hdr, tcp_hdr_t *tcp_hdr ));
FORWARD void process_data ARGS(( tcp_conn_t *tcp_conn,
	tcp_hdr_t *tcp_hdr, int tcp_hdr_len, acc_t *tcp_data,
	int data_len ));
FORWARD void process_advanced_data ARGS(( tcp_conn_t *tcp_conn,
	tcp_hdr_t *tcp_hdr, int tcp_hdr_len, acc_t *tcp_data,
	int data_len ));
FORWARD acc_t *merge_packs ARGS(( acc_t *first, acc_t *next ));
FORWARD void fd_read ARGS(( tcp_fd_t *tcp_fd ));
FORWARD void switch_read_fd ARGS(( tcp_conn_t *tcp_conn,
	tcp_fd_t *tcp_fd, tcp_fd_t **ref_urg_fd,
	tcp_fd_t **ref_norm_fd ));

PUBLIC void tcp_frag2conn(tcp_conn, ip_pack, tcp_pack)
tcp_conn_t *tcp_conn;
acc_t *ip_pack;
acc_t *tcp_pack;
{
	tcp_fd_t *connuser;
	int tcp_hdr_flags;
	ip_hdr_t *ip_hdr;
	tcp_hdr_t *tcp_hdr;
	int ip_hdr_len, tcp_hdr_len;
	u32_t seg_ack, seg_seq, rcv_hi;
	u16_t data_length, seg_wnd;
	int acceptable_ACK, segm_acceptable;

#if DEBUG & 256
 { where(); printf("tcp_frag2conn(&tcp_conn_table[%d],..) called\n",
	tcp_conn-tcp_conn_table); }
#endif
#if DEBUG & 256
 { where(); printf("tc_connuser= 0x%x\n", tcp_conn->tc_connuser); }
#endif

	ip_pack= bf_packIffLess(ip_pack, IP_MIN_HDR_SIZE);
	ip_hdr= (ip_hdr_t *)ptr2acc_data(ip_pack);
	ip_hdr_len= (ip_hdr->ih_vers_ihl & IH_IHL_MASK) << 2;
	if (ip_hdr_len>IP_MIN_HDR_SIZE)
	{
		ip_pack= bf_packIffLess(ip_pack, ip_hdr_len);
		ip_hdr= (ip_hdr_t *)ptr2acc_data(ip_pack);
	}

	tcp_pack= bf_packIffLess(tcp_pack, TCP_MIN_HDR_SIZE);
	tcp_hdr= (tcp_hdr_t *)ptr2acc_data(tcp_pack);
	tcp_hdr_len= (tcp_hdr->th_data_off & TH_DO_MASK) >> 2;
		/* actualy (>> 4) << 2 */
	if (tcp_hdr_len>TCP_MIN_HDR_SIZE)
	{
		tcp_pack= bf_packIffLess(tcp_pack, tcp_hdr_len);
		tcp_hdr= (tcp_hdr_t *)ptr2acc_data(tcp_pack);
	}
	data_length= tcp_hdr->th_chksum-tcp_hdr_len;
		/* th_chksum is used for packet size internally */
	tcp_hdr_flags= tcp_hdr->th_flags & TH_FLAGS_MASK;
	seg_ack= ntohl(tcp_hdr->th_ack_nr);
	seg_seq= ntohl(tcp_hdr->th_seq_nr);
	seg_wnd= ntohs(tcp_hdr->th_window);

	switch (tcp_conn->tc_state)
	{
	case TCS_CLOSED:
/*
CLOSED:
	discard all data.
	!RST ?
		ACK ?
			<SEQ=SEG.ACK><CTL=RST>
			exit
		:
			<SEQ=0><ACK=SEG.SEQ+SEG.LEN><CTL=RST,ACK>
			exit
	:
		discard packet
		exit
*/

		if (!(tcp_hdr_flags & THF_RST))
		{
			create_RST (tcp_conn, ip_hdr, tcp_hdr);
			tcp_restart_write(tcp_conn);
		}
		break;
	case TCS_LISTEN:
/*
LISTEN:
	RST ?
		discard packet
		exit
	ACK ?
		<SEQ=SEG.ACK><CTL=RST>
		exit
	SYN ?
		BUG: no security check
		RCV.NXT= SEG.SEQ+1
		IRS= SEG.SEQ
		ISS should already be selected
		<SEQ=ISS><ACK=RCV.NXT><CTL=SYN,ACK>
		SND.NXT=ISS+1
		SND.UNA=ISS
		state= SYN-RECEIVED
		exit
	:
		shouldnot occur
		discard packet
		exit
*/
#if DEBUG & 256
 { where(); printf("\n"); }
#endif
		if (tcp_hdr_flags & THF_RST)
			break;
		if (tcp_hdr_flags & THF_ACK)
		{
			create_RST (tcp_conn, ip_hdr, tcp_hdr);
			tcp_restart_write(tcp_conn);
			break;
		}
		if (tcp_hdr_flags & THF_SYN)
		{
			tcp_extract_ipopt(tcp_conn, ip_hdr);
			tcp_extract_tcpopt(tcp_conn, tcp_hdr);
			tcp_conn->tc_RCV_LO= seg_seq+1;
			tcp_conn->tc_RCV_NXT= seg_seq+1;
			tcp_conn->tc_RCV_HI= tcp_conn->tc_RCV_LO+
				tcp_conn->tc_rcv_wnd;
			tcp_conn->tc_RCV_UP= seg_seq;
			tcp_conn->tc_IRS= seg_seq;
			tcp_conn->tc_SND_UNA= tcp_conn->tc_ISS;
			tcp_conn->tc_SND_TRM= tcp_conn->tc_ISS;
			tcp_conn->tc_SND_NXT= tcp_conn->tc_ISS+1;
			tcp_conn->tc_SND_UP= tcp_conn->tc_ISS-1;
			tcp_conn->tc_SND_PSH= tcp_conn->tc_ISS-1;
			tcp_conn->tc_SND_WL1= seg_seq;
			tcp_conn->tc_state= TCS_SYN_RECEIVED;
			tcp_conn->tc_no_retrans= 0;
assert (tcp_check_conn(tcp_conn));
#if DEBUG & 2
 { where(); tcp_write_state(tcp_conn); }
#endif
			tcp_conn->tc_locaddr= ip_hdr->ih_dst;
			tcp_conn->tc_locport= tcp_hdr->th_dstport;
			tcp_conn->tc_remaddr= ip_hdr->ih_src;
			tcp_conn->tc_remport= tcp_hdr->th_srcport;
#if DEBUG & 256
 { where(); printf("calling tcp_restart_write(&tcp_conn_table[%d])\n",
	tcp_conn-tcp_conn_table); }
#endif
			tcp_restart_write(tcp_conn);
			break;
		}
#if DEBUG
 { where(); printf("this shouldn't happen\n"); }
#endif
		break;
	case TCS_SYN_SENT:
/*
SYN-SENT:
	ACK ?
		SEG.ACK <= ISS || SEG.ACK > SND.NXT ?
			RST ?
				discard packet
				exit
			:
				<SEQ=SEG.ACK><CTL=RST>
				exit
		SND.UNA <= SEG.ACK && SEG.ACK <= SND.NXT ?
			ACK is acceptable
		:
			ACK is !acceptable
	:
		ACK is !acceptable
	RST ?
		ACK acceptable ?
			discard segment
			state= CLOSED
			error "connection refused"
			exit
		:
			discard packet
			exit
	BUG: no security check
	SYN ?
		IRS= SEG.SEQ
		RCV.NXT= IRS+1
		ACK ?
			SND.UNA= SEG.ACK
		SND.UNA > ISS ?
			state= ESTABLISHED
			<SEQ=SND.NXT><ACK= RCV.NXT><CTL=ACK>
			process ev. URG and text
			exit
		:
			state= SYN-RECEIVED
			SND.WND= SEG.WND
			SND.WL1= SEG.SEQ
			SND.WL2= SEG.ACK
			<SEQ=ISS><ACK=RCV.NXT><CTL=SYN,ACK>
			exit
	:
		discard segment
		exit
*/
		if (tcp_hdr_flags & THF_ACK)
		{
			if (tcp_LEmod4G(seg_ack, tcp_conn->tc_ISS) ||
				tcp_Gmod4G(seg_ack, tcp_conn->tc_SND_NXT))
				if (tcp_hdr_flags & THF_RST)
					break;
				else
				{
					create_RST (tcp_conn, ip_hdr,
						tcp_hdr);
					tcp_restart_write(tcp_conn);
					break;
				}
			acceptable_ACK= (tcp_LEmod4G(tcp_conn->tc_SND_UNA,
				seg_ack) && tcp_LEmod4G(seg_ack,
				tcp_conn->tc_SND_NXT));
		}
		else
			acceptable_ACK= FALSE;
		if (tcp_hdr_flags & THF_RST)
		{
			if (acceptable_ACK)
			{
#if DEBUG & 256
 { where(); printf("calling tcp_close_connection\n"); }
#endif
				tcp_close_connection(tcp_conn,
					ECONNREFUSED);
			}
			break;
		}
		if (tcp_hdr_flags & THF_SYN)
		{
			tcp_conn->tc_RCV_LO= seg_seq+1;
			tcp_conn->tc_RCV_NXT= seg_seq+1;
			tcp_conn->tc_RCV_HI= tcp_conn->tc_RCV_LO +
				tcp_conn->tc_rcv_wnd;
			tcp_conn->tc_RCV_UP= seg_seq;
			tcp_conn->tc_IRS= seg_seq;
			if (tcp_hdr_flags & THF_ACK)
				tcp_conn->tc_SND_UNA= seg_ack;
			if (tcp_Gmod4G(tcp_conn->tc_SND_UNA,
				tcp_conn->tc_ISS))
			{
				tcp_conn->tc_state= TCS_ESTABLISHED;
assert (tcp_check_conn(tcp_conn));
#if DEBUG & 2
 { where(); tcp_write_state(tcp_conn); }
#endif
#if DEBUG & 256
 { where(); printf("ISS= 0x%lx\n", tcp_conn->tc_ISS); }
#endif
assert(tcp_conn->tc_connuser);
				tcp_restart_connect(tcp_conn-> tc_connuser);
				if (tcp_conn->tc_state == TCS_CLOSED)
				{
 { where(); printf("connection closed while inuse\n"); }
					break;
				}
				tcp_conn->tc_flags &= ~TCF_ACK_TIMER_SET;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
				tcp_conn->tc_flags |= TCF_SEND_ACK;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
				tcp_restart_write (tcp_conn);
				tcp_frag2conn(tcp_conn, ip_pack, tcp_pack);
				return;
				/* ip_pack and tcp_pack are already
					freed */
			}
			tcp_conn->tc_state= TCS_SYN_RECEIVED;
assert (tcp_check_conn(tcp_conn));
#if DEBUG
 { where(); tcp_write_state(tcp_conn); }
#endif
			tcp_conn->tc_SND_TRM= tcp_conn->tc_ISS;
			tcp_restart_write(tcp_conn);
		}
		break;
	case TCS_SYN_RECEIVED:
	case TCS_ESTABLISHED:
	case TCS_FIN_WAIT_1:
	case TCS_FIN_WAIT_2:
	case TCS_CLOSE_WAIT:
	case TCS_CLOSING:
	case TCS_LAST_ACK:
	case TCS_TIME_WAIT:
/*
SYN-RECEIVED:
ESTABLISHED:
FIN-WAIT-1:
FIN-WAIT-2:
CLOSE-WAIT:
CLOSING:
LAST-ACK:
TIME-WAIT:
	test if segment is acceptable:
	Segment	Receive	Test
	Length	Window
	0	0	SEG.SEQ == RCV.NXT
	0	>0	RCV.NXT <= SEG.SEQ && SEG.SEQ < RCV.NXT+RCV.WND
	>0	0	not acceptable
	>0	>0	(RCV.NXT <= SEG.SEQ && SEG.SEQ < RCV.NXT+RCV.WND)
			|| (RCV.NXT <= SEG.SEQ+SEG.LEN-1 &&
			SEG.SEQ+SEG.LEN-1 < RCV.NXT+RCV.WND)
	for urgent data: use RCV.WND+URG.WND for RCV.WND
*/
#if DEBUG & 256
 { where(); printf("\n"); }
#endif
		rcv_hi= tcp_conn->tc_RCV_HI;
		if (tcp_hdr_flags & THF_URG)
			rcv_hi= tcp_conn->tc_RCV_LO + tcp_conn->tc_rcv_wnd +
				tcp_conn->tc_urg_wnd;
		if (!data_length)
		{
			if (rcv_hi == tcp_conn->tc_RCV_NXT)
			{
#if DEBUG & 256
 { where(); printf("\n"); }
#endif
				segm_acceptable= (seg_seq == rcv_hi);
#if DEBUG
 if (!segm_acceptable)
 { where(); printf("!segm_acceptable\n"); }
#endif
			}
			else
			{
#if DEBUG & 256
 { where(); printf("\n"); }
#endif
assert (tcp_Gmod4G(rcv_hi, tcp_conn->tc_RCV_NXT));
				segm_acceptable= (tcp_LEmod4G(tcp_conn->
					tc_RCV_NXT, seg_seq) &&
					tcp_Lmod4G(seg_seq, rcv_hi));
#if DEBUG & 256
 if (!segm_acceptable)
 { where(); printf("RCV_NXT= 0x%lx, seg_seq= 0x%lx, rcv_hi= 0x%lx\n",
	tcp_conn->tc_RCV_NXT, seg_seq, rcv_hi); }
#endif
			}
		}
		else
		{
			if (tcp_Gmod4G(rcv_hi, tcp_conn->tc_RCV_NXT))
			{
#if DEBUG & 256
 { where(); printf("RCV_NXT= %lu, rcv_hi= %lu, seg_seq= %lu, data_length= %u\n",
	tcp_conn->tc_RCV_NXT, rcv_hi, seg_seq, data_length); }
#endif
				segm_acceptable= (tcp_LEmod4G(tcp_conn->
					tc_RCV_NXT, seg_seq) &&
					tcp_Lmod4G(seg_seq, rcv_hi)) ||
					(tcp_LEmod4G(tcp_conn->tc_RCV_NXT,
					seg_seq+data_length-1) &&
					tcp_Lmod4G(seg_seq+data_length-1,
					rcv_hi));
#if DEBUG & 256
 if (!segm_acceptable)
 { where(); printf("!segm_acceptable\n"); }
#endif
			}
			else
			{
#if DEBUG
 { where(); printf("\n"); }
#endif
				segm_acceptable= FALSE;
#if DEBUG
 if (!segm_acceptable)
 { where(); printf("!segm_acceptable\n"); }
#endif
			}
		}
/*
	!segment acceptable ?
		RST ?
			discard packet
			exit
		:
			<SEG=SND.NXT><ACK=RCV.NXT><CTL=ACK>
			exit
*/
#if DEBUG & 256
 { where(); printf("\n"); }
#endif
		if (!segm_acceptable)
		{
#if DEBUG & 256
 { where(); printf("segment is not acceptable\n"); }
#endif
			if (!(tcp_hdr_flags & THF_RST))
			{
#if DEBUG & 256
 { where(); printf("segment is not acceptable setting ack timer\n"); }
#endif
				tcp_set_ack_timer(tcp_conn);
			}
			break;
		}
/*
	RST ?
		state == SYN-SECEIVED ?
			initiated by a LISTEN ?
				state= LISTEN
				exit
			:
				state= CLOSED
				error "connection refused"
				exit
		state == CLOSING || state == LAST-ACK ||
			state == TIME-WAIT ?
			state= CLOSED
			exit
		:
			state= CLOSED
			error "connection reset"
			exit
*/
#if DEBUG & 256
 { where(); printf("\n"); }
#endif
		if (tcp_hdr_flags & THF_RST)
		{
			if (tcp_conn->tc_state == TCS_SYN_RECEIVED)
			{
				if (tcp_conn->tc_orglisten)
				{
					connuser= tcp_conn->tc_connuser;
					tcp_conn->tc_connuser= 0;
#if DEBUG
 { where(); printf("calling tcp_close_connection\n"); }
#endif
					tcp_close_connection (tcp_conn,
						ECONNREFUSED);
					if (connuser)
						(void)tcp_su4listen(
							connuser);
					break;
				}
				else
				{
#if DEBUG
 { where(); printf("calling tcp_close_connection\n"); }
#endif
					tcp_close_connection(tcp_conn,
						ECONNREFUSED);
					break;
				}
			}
			if (tcp_conn->tc_state == TCS_CLOSING ||
				tcp_conn->tc_state == TCS_LAST_ACK ||
				tcp_conn->tc_state == TCS_TIME_WAIT)
			{
#if DEBUG
 { where(); printf("calling tcp_close_connection\n"); }
#endif
				tcp_close_connection (tcp_conn,
					ENOCONN);
				break;
			}
#if DEBUG
 { where(); printf("calling tcp_close_connection\n"); }
#endif
			tcp_close_connection(tcp_conn, ECONNRESET);
			break;
		}
/*
	SYN in window ?
		state == SYN-RECEIVED && initiated by a LISTEN ?
			state= LISTEN
			exit
		:
			state= CLOSED
			error "connection reset"
			exit
*/
#if DEBUG  & 256
 { where(); printf("\n"); }
#endif
		if ((tcp_hdr_flags & THF_SYN) && tcp_GEmod4G(seg_seq,
			tcp_conn->tc_RCV_NXT))
		{
			if (tcp_conn->tc_state == TCS_SYN_RECEIVED &&
				tcp_conn->tc_orglisten)
			{
				connuser= tcp_conn->tc_connuser;
				tcp_conn->tc_connuser= 0;
#if DEBUG
 { where(); printf("calling tcp_close_connection\n"); }
#endif
				tcp_close_connection(tcp_conn,
					ECONNRESET);
				if (connuser)
					(void)tcp_su4listen(connuser);
				break;
			}
#if DEBUG
 { where(); printf("calling tcp_close_connection\n"); }
#endif
			tcp_close_connection(tcp_conn, ECONNRESET);
			break;
		}
/*
	!ACK ?
		discard packet
		exit
*/
#if DEBUG & 256
 { where(); printf("\n"); }
#endif
		if (!(tcp_hdr_flags & THF_ACK))
			break;
/*
	state == SYN-RECEIVED ?
		SND.UNA <= SEG.ACK <= SND.NXT ?
			state= ESTABLISHED
		:
			<SEG=SEG.ACK><CTL=RST>
			exit
*/
#if DEBUG & 256
 { where(); printf("\n"); }
#endif
	if (tcp_conn->tc_state == TCS_SYN_RECEIVED)
	{
		if (tcp_LEmod4G(tcp_conn->tc_SND_UNA, seg_ack) &&
			tcp_LEmod4G(seg_ack, tcp_conn->tc_SND_NXT))
		{
			tcp_conn->tc_state= TCS_ESTABLISHED;
assert (tcp_check_conn(tcp_conn));
#if DEBUG & 2
 { where(); tcp_write_state(tcp_conn); }
#endif
assert(tcp_conn->tc_connuser);
			tcp_restart_connect(tcp_conn->tc_connuser);
			if (tcp_conn->tc_state == TCS_CLOSED)
			{
#if DEBUG
 { where(); printf("connection closed while inuse\n"); }
#endif
				break;
			}
		}
		else
		{
			create_RST (tcp_conn, ip_hdr, tcp_hdr);
			tcp_restart_write(tcp_conn);
			break;
		}
	}
/*
	state == ESTABLISHED || state == FIN-WAIT-1 ||
		state == FIN-WAIT-2 || state == CLOSE-WAIT ||
		state == LAST-ACK || state == TIME_WAIT || state == CLOSING ?
		SND.UNA < SEG.ACK <= SND.NXT ?
			SND.UNA= SEG.ACK
			reply "send ok"
			SND.WL1 < SEG.SEQ || (SND.WL1 == SEG.SEQ &&
				SND.WL2 <= SEG.ACK ?
				SND.WND= SEG.WND
				SND.Wl1= SEG.SEQ
				SND.WL2= SEG.ACK
		SEG.ACK <= SND.UNA ?
			ignore ACK
		SEG.ACK > SND.NXT ?
			<SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>
			discard packet
			exit
*/
#if DEBUG & 256
 { where(); printf("\n"); }
#endif
		if (tcp_conn->tc_state == TCS_ESTABLISHED ||
			tcp_conn->tc_state == TCS_FIN_WAIT_1 ||
			tcp_conn->tc_state == TCS_FIN_WAIT_2 ||
			tcp_conn->tc_state == TCS_CLOSE_WAIT ||
			tcp_conn->tc_state == TCS_LAST_ACK ||
			tcp_conn->tc_state == TCS_TIME_WAIT ||
			tcp_conn->tc_state == TCS_CLOSING)
		{
			if (tcp_LEmod4G(tcp_conn->tc_SND_UNA, seg_ack)
				&& tcp_LEmod4G(seg_ack, tcp_conn->
				tc_SND_NXT))
			{
				if (tcp_Lmod4G(tcp_conn->tc_SND_WL1,
					seg_seq) || (tcp_conn->
					tc_SND_WL1==seg_seq &&
					tcp_LEmod4G(tcp_conn->
					tc_SND_WL2, seg_ack)))
				{
					if (seg_wnd > TCP_MAX_WND_SIZE)
						seg_wnd= TCP_MAX_WND_SIZE;
					if (!seg_wnd)
						seg_wnd++;
					tcp_conn->tc_SND_WL1= seg_seq;
					tcp_conn->tc_SND_WL2= seg_ack;
#if SUN_0WND_BUG
					if (seg_wnd && seg_ack == tcp_conn->
						tc_SND_UNA && tcp_LEmod4G(
						seg_ack + seg_wnd,
						tcp_conn->tc_SND_NXT) &&
						tcp_LEmod4G(seg_ack + seg_wnd,
						tcp_conn->tc_snd_cwnd))
						seg_wnd= 0;
#endif
				}
				else
				{
					seg_wnd= tcp_conn->tc_mss;
					/* assume 1 segment if not a valid
					 * window */
				}
				tcp_release_retrans(tcp_conn, seg_ack, seg_wnd);
				if (tcp_conn->tc_state == TCS_CLOSED)
				{
#if DEBUG
 { where(); printf("connection closed while inuse\n"); }
#endif
					break;
				}
			}
			else if (tcp_Gmod4G(seg_ack,
				tcp_conn->tc_SND_NXT))
			{
				tcp_set_ack_timer(tcp_conn);
#if DEBUG
 { where(); printf("got an ack of something I haven't send\n");
   printf("seg_ack= %lu, SND_NXT= %lu\n", seg_ack, tcp_conn->tc_SND_NXT); }
#endif
				break;
			}
#if DEBUG & 256
 if (!seg_wnd) { where(); printf("SND_UNA= %lu, SND_NXT= %lu\n",
	tcp_conn->tc_SND_UNA, tcp_conn->tc_SND_NXT); }
#endif
			if (!seg_wnd &&
			/* tcp_GEmod4G(seg_wnd, tcp_conn->tc_SND_UNA) &&
			*/
				tcp_Lmod4G(tcp_conn->tc_SND_UNA,
				tcp_conn->tc_SND_NXT))
			{	/* zero window */
#if DEBUG & 256
 { where(); printf("setting 0wnd_to\n"); }
#endif
				clck_untimer(&tcp_conn->tc_minor_timer);
				if (!tcp_conn->tc_0wnd_to)
				{
assert (tcp_conn->tc_rtt);
					tcp_conn->tc_0wnd_to=
						tcp_conn->tc_rtt;
				}
				clck_timer(&tcp_conn->tc_major_timer,
					get_time()+tcp_conn->tc_0wnd_to,
					tcp_zero_wnd_to, tcp_conn-
					tcp_conn_table);
			}
			else
			{
#if DEBUG & 256
 { where(); printf("not setting 0wnd_to\n"); }
#endif
				if (tcp_conn->tc_0wnd_to)
				{
#if DEBUG & 256
 { where(); printf("resetting 0wnd_to\n"); }
#endif
					tcp_conn->tc_0wnd_to= 0;
					tcp_conn->tc_SND_TRM=
						tcp_conn->tc_SND_UNA;
					clck_untimer(&tcp_conn->
						tc_major_timer);
					tcp_restart_write (tcp_conn);
				}
			}
		}
/*
	state == FIN-WAIT-1 && FIN acknowledged ?
		state= FIN-WAIT-2

	state == CLOSING && FIN acknowledged ?
		state= TIME-WAIT

	state == LAST-ACK && FIN acknowledged ?
		state= CLOSED

	state == TIME-WAIT ?
		<SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>
		restart 2 MSL timeout
*/
#if DEBUG & 256
 { where(); printf("\n"); }
#endif
		if (tcp_conn->tc_SND_UNA == tcp_conn->tc_SND_NXT)
		{
			switch (tcp_conn->tc_state)
			{
			case TCS_FIN_WAIT_1:

				tcp_conn->tc_state= TCS_FIN_WAIT_2;
assert (tcp_check_conn(tcp_conn));
#if DEBUG
 { where(); tcp_write_state(tcp_conn); }
#endif
				break;
			case TCS_CLOSING:
				tcp_conn->tc_state= TCS_TIME_WAIT;
assert (tcp_check_conn(tcp_conn));
#if DEBUG
 { where(); tcp_write_state(tcp_conn); }
#endif
				tcp_set_time_wait_timer(tcp_conn);
				break;
			case TCS_LAST_ACK:
#if DEBUG & 256
 { where(); printf("calling tcp_close_connection\n"); }
#endif
				tcp_close_connection(tcp_conn, ENOCONN);
				break;
			}
			if (!tcp_conn->tc_mainuser)
			{
				tcp_close_connection(tcp_conn, ENOCONN);
				break;
			}
		}
		if (tcp_conn->tc_state == TCS_TIME_WAIT)
		{
			tcp_set_ack_timer(tcp_conn);
			tcp_set_time_wait_timer(tcp_conn);
		}

/*
	process data...
*/
#if DEBUG & 256
 { where(); printf("\n"); }
#endif
		tcp_extract_ipopt(tcp_conn, ip_hdr);
		tcp_extract_tcpopt(tcp_conn, tcp_hdr);

		if (data_length)
		{
			if (tcp_LEmod4G(seg_seq, tcp_conn->tc_RCV_NXT))
				process_data (tcp_conn, tcp_hdr,
					tcp_hdr_len, tcp_pack,
					data_length);
			else
				process_advanced_data (tcp_conn,
					tcp_hdr, tcp_hdr_len, tcp_pack,
					data_length);
			if (tcp_conn->tc_state == TCS_CLOSED)
				break;
			tcp_conn->tc_flags |= TCF_SEND_ACK;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
			tcp_restart_write (tcp_conn);
			if (tcp_conn->tc_state == TCS_CLOSED)
				break;
		}
/*
	FIN ?
		reply pending receives
		advace RCV.NXT over the FIN
		<SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>

		state == SYN-RECEIVED || state == ESTABLISHED ?
			state= CLOSE-WAIT
		state == FIN-WAIT-1 ?
			state= CLOSING
		state == FIN-WAIT-2 ?
			state= TIME-WAIT
		state == TIME-WAIT ?
			restart the TIME-WAIT timer
	exit
*/
#if DEBUG & 256
 { where(); printf("\n"); }
#endif
		if ((tcp_hdr_flags & THF_FIN) && tcp_LEmod4G(seg_seq,
			tcp_conn->tc_RCV_NXT))
		{
			switch (tcp_conn->tc_state)
			{
			case TCS_SYN_RECEIVED:
				break;
			case TCS_ESTABLISHED:
				tcp_conn->tc_state= TCS_CLOSE_WAIT;
assert (tcp_check_conn(tcp_conn));
#if DEBUG
 { where(); tcp_write_state(tcp_conn); }
#endif
				break;
			case TCS_FIN_WAIT_1:
				tcp_conn->tc_state= TCS_CLOSING;
assert (tcp_check_conn(tcp_conn));
#if DEBUG
 { where(); tcp_write_state(tcp_conn); }
#endif
				break;
			case TCS_FIN_WAIT_2:
				tcp_conn->tc_state= TCS_TIME_WAIT;
assert (tcp_check_conn(tcp_conn));
#if DEBUG
 { where(); tcp_write_state(tcp_conn); }
#endif
				/* drops through */
			case TCS_TIME_WAIT:
				tcp_set_time_wait_timer(tcp_conn);
				break;
			}
			if (!(tcp_conn->tc_flags & TCF_FIN_RECV))
			{
				tcp_conn->tc_RCV_NXT++;
				tcp_conn->tc_flags |= TCF_FIN_RECV;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
			}
			tcp_set_ack_timer(tcp_conn);
			if (tcp_conn->tc_readuser)
				tcp_restart_fd_read(tcp_conn);
		}
		break;
	default:
		printf("unknown state: tcp_conn->tc_state== %d\n",
			tcp_conn->tc_state);
		break;
	}
	bf_afree(ip_pack);
	bf_afree(tcp_pack);
}


PRIVATE void process_data(tcp_conn, tcp_hdr, tcp_hdr_len, tcp_data,
	data_len)
tcp_conn_t *tcp_conn;
tcp_hdr_t *tcp_hdr;
int tcp_hdr_len;
acc_t *tcp_data;
int data_len;
{
	u32_t lo_seq, hi_seq, urg_seq, seq_nr;
	u16_t urgptr;
	int tcp_hdr_flags;
	unsigned int offset;
	acc_t *all_data, *tmp_data, *rcvd_data;

#if DEBUG & 256
 { where(); printf("in process data\n"); }
#endif
	seq_nr= ntohl(tcp_hdr->th_seq_nr);
	urgptr= ntohs(tcp_hdr->th_urgptr);
	while (tcp_data)
	{
assert (tcp_check_conn(tcp_conn));
		all_data= bf_cut(tcp_data, tcp_hdr_len, data_len);
		tcp_data= 0;

		lo_seq= seq_nr;
		tcp_hdr_flags= tcp_hdr->th_flags & TH_FLAGS_MASK;

		if (tcp_hdr_flags & THF_URG)
		{
			urg_seq= lo_seq+ urgptr;
			tcp_conn->tc_RCV_HI= tcp_conn->tc_RCV_LO+
				tcp_conn->tc_rcv_wnd+tcp_conn->
				tc_urg_wnd;
			if (tcp_GEmod4G(urg_seq, tcp_conn->tc_RCV_HI))
				urg_seq= tcp_conn->tc_RCV_HI;
			if (tcp_Gmod4G(urg_seq, tcp_conn->tc_RCV_UP))
				tcp_conn->tc_RCV_UP= urg_seq;
		}
		if (tcp_hdr_flags & THF_SYN)
			lo_seq++;

		if (tcp_hdr_flags & THF_PSH)
		{
			tcp_conn->tc_flags |= TCF_RCV_PUSH;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
		}

		if (tcp_Lmod4G(lo_seq, tcp_conn->tc_RCV_NXT))
		{
			offset= tcp_conn->tc_RCV_NXT-lo_seq;
			tmp_data= bf_cut(all_data, offset, data_len-
				offset);
			bf_afree(all_data);
			lo_seq += offset;
			data_len -= offset;
			all_data= tmp_data;
			tmp_data= 0;
		}
		assert (lo_seq == tcp_conn->tc_RCV_NXT);

		hi_seq= lo_seq+data_len;
		if (tcp_Gmod4G(hi_seq, tcp_conn->tc_RCV_HI))
		{
			data_len= tcp_conn->tc_RCV_HI-lo_seq;
			tmp_data= bf_cut(all_data, 0, data_len);
			bf_afree(all_data);
			all_data= tmp_data;
			hi_seq= lo_seq+data_len;
			tcp_hdr_flags &= ~THF_FIN;
		}
		assert (tcp_LEmod4G (hi_seq, tcp_conn->tc_RCV_HI));

#if DEBUG & 256
 { where(); printf("in process data: lo_seq= %lu, hi_seq= %lu\n",
	lo_seq, hi_seq); }
#endif
		rcvd_data= tcp_conn->tc_rcvd_data;
		tcp_conn->tc_rcvd_data= 0;
		tmp_data= bf_append(rcvd_data, all_data);
		if (tcp_conn->tc_state == TCS_CLOSED)
		{
#if DEBUG
 { where(); printf("connection closed while inuse\n"); }
#endif
			bf_afree(tmp_data);
			return;
		}
		tcp_conn->tc_rcvd_data= tmp_data;
		tcp_conn->tc_RCV_NXT= hi_seq;

		assert (tcp_conn->tc_RCV_LO + bf_bufsize(tcp_conn->
			tc_rcvd_data) == tcp_conn->tc_RCV_NXT);
		
		if (tcp_hdr_flags & THF_FIN)
		{
#if DEBUG
 { where(); printf("got a FIN\n"); }
#endif
			tcp_conn->tc_RCV_NXT++;
			tcp_conn->tc_flags |= TCF_FIN_RECV;
#if DEBUG & 16
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
		}
		tcp_set_ack_timer(tcp_conn);
		while (tcp_conn->tc_rcv_queue)
		{
			tmp_data= tcp_conn->tc_rcv_queue;
			assert (tmp_data->acc_length >= TCP_MIN_HDR_SIZE);
			tcp_hdr= (tcp_hdr_t *)ptr2acc_data(tmp_data);
			lo_seq= tcp_hdr->th_seq_nr;
			/* th_seq_nr is changed to host byte order */
			if (tcp_Gmod4G(lo_seq, tcp_conn->tc_RCV_NXT))
				break;
			tcp_hdr_len= (tcp_hdr->th_data_off &
				TH_DO_MASK) >> 2;
			data_len= tcp_hdr->th_chksum-tcp_hdr_len;
			if (tcp_LEmod4G(lo_seq+data_len, tcp_conn->
				tc_RCV_NXT))
			{
				tcp_conn->tc_rcv_queue= tmp_data->
					acc_ext_link;
				bf_afree(tmp_data);
				continue;
			}
			tcp_data= tmp_data;
			seq_nr= tcp_hdr->th_seq_nr;
			urgptr= tcp_hdr->th_urgptr;
			break;
		}
	}
assert (tcp_check_conn(tcp_conn));
	if (tcp_conn->tc_readuser)
		tcp_restart_fd_read(tcp_conn);
	else if (!tcp_conn->tc_mainuser)
	{
#if DEBUG
 { where(); printf("calling tcp_close_connection\n"); }
#endif
		tcp_close_connection (tcp_conn, ENOCONN);
	}
assert (tcp_check_conn(tcp_conn));
}

PRIVATE void process_advanced_data(tcp_conn, tcp_hdr, tcp_hdr_len,
	tcp_data, data_len)
tcp_conn_t *tcp_conn;
tcp_hdr_t *tcp_hdr;
int tcp_hdr_len;
acc_t *tcp_data;
int data_len;
{
	u32_t seg_seq, seg_hi;
	u16_t seg_up;
	acc_t *hdr_acc, *next_acc, **tail_acc_ptr, *head_acc, *data_acc,
		*tmp_acc;
	tcp_hdr_t *tmp_hdr;
	int tmp_hdr_len;

#if DEBUG & 256
 { where(); printf ("processing advanced data\n"); }
#endif
	hdr_acc= bf_memreq(tcp_hdr_len);	/* make fresh header */
	if (tcp_conn->tc_state == TCS_CLOSED)
	{
#if DEBUG
 { where(); printf("connection closed while inuse\n"); }
#endif
		bf_afree(hdr_acc);
		return;
	}
	assert (hdr_acc->acc_length == tcp_hdr_len);
	tmp_hdr= (tcp_hdr_t *)ptr2acc_data(hdr_acc);
#if DEBUG & 256
 { where(); printf("doing memcpy\n"); }
#endif
	memcpy ((char *)tmp_hdr, (char *)tcp_hdr, tcp_hdr_len);
	tcp_hdr= tmp_hdr;

	data_acc= bf_cut (tcp_data, tcp_hdr_len, data_len);

	seg_seq= ntohl(tcp_hdr->th_seq_nr);
	tcp_hdr->th_seq_nr= seg_seq;	/* seq_nr in host format */

	if (tcp_hdr->th_flags & THF_URG)
	{
		seg_up= ntohs(tcp_hdr->th_urgptr);
		tcp_hdr->th_urgptr= seg_up;
					/* urgptr in host format */
		tcp_conn->tc_RCV_HI= tcp_conn->tc_RCV_LO+
			tcp_conn->tc_rcv_wnd+tcp_conn->tc_urg_wnd;
	}

	assert (!(tcp_hdr->th_flags & THF_SYN));
	assert (tcp_Gmod4G(seg_seq, tcp_conn->tc_RCV_NXT));

	tcp_hdr->th_flags &= ~THF_FIN;	/* it is too difficult to
		preserve a FIN */

	seg_hi= seg_seq + data_len;

	if (tcp_Gmod4G(seg_hi, tcp_conn->tc_RCV_HI))
	{
		seg_hi= tcp_conn->tc_RCV_HI;
		data_len= seg_hi-seg_seq;
		if (!data_len)
		{
			bf_afree(hdr_acc);
			bf_afree(data_acc);
			return;
		}
#if DEBUG
 { where(); printf("Cutting packet\n"); }
#endif
		tmp_acc= bf_cut(data_acc, 0, data_len);
		bf_afree(data_acc);
		data_acc= tmp_acc;
	}
	hdr_acc->acc_next= data_acc;
	hdr_acc->acc_ext_link= 0;

	head_acc= tcp_conn->tc_rcv_queue;
	tcp_conn->tc_rcv_queue= 0;
	tail_acc_ptr= 0;
	next_acc= head_acc;

	while (next_acc)
	{
		assert (next_acc->acc_length >= TCP_MIN_HDR_SIZE);
		tmp_hdr= (tcp_hdr_t *)ptr2acc_data(next_acc);
		if (tcp_Lmod4G(seg_seq, tmp_hdr->th_seq_nr))
		{
#if DEBUG & 256
 { where(); printf("calling merge_packs\n"); } 
#endif
			next_acc= merge_packs(hdr_acc, next_acc);
			hdr_acc= 0;
			if (tail_acc_ptr)
			{
assert (*tail_acc_ptr);
				(*tail_acc_ptr)->acc_ext_link= 0;
#if DEBUG & 256
 { where(); printf("calling merge_packs\n"); } 
#endif
				*tail_acc_ptr= merge_packs(
					*tail_acc_ptr, next_acc);
			}
			else
				head_acc= next_acc;
			break;
		}
		if (!tail_acc_ptr)
			tail_acc_ptr=  &head_acc;
		else
			tail_acc_ptr= &(*tail_acc_ptr)->acc_ext_link;
		next_acc= next_acc->acc_ext_link;
	}
	if (hdr_acc)
	{
		next_acc= hdr_acc;
		hdr_acc= 0;
		if (tail_acc_ptr)
		{
			if (*tail_acc_ptr)
			{
				(*tail_acc_ptr)->acc_ext_link= 0;
#if DEBUG & 256
 { where(); printf("calling merge_packs\n"); } 
#endif
				*tail_acc_ptr= merge_packs(
					*tail_acc_ptr, next_acc);
			}
			else
				*tail_acc_ptr= next_acc;
		}
		else
			head_acc= next_acc;
	}
	if (tcp_conn->tc_state == TCS_CLOSED)
	{
		while (head_acc)
		{
			next_acc= head_acc->acc_ext_link;
			bf_afree(head_acc);
			head_acc= next_acc;
		}
		return;
	}
	tcp_conn->tc_rcv_queue= head_acc;
}
				
PRIVATE acc_t *merge_packs(first, next)
acc_t *first;
acc_t *next;
{
	tcp_hdr_t *first_hdr, *next_hdr;
	int first_hdr_len, next_hdr_len, first_data_len, next_data_len;
	acc_t *next_acc, *tmp_acc;

	assert (first->acc_length >= TCP_MIN_HDR_SIZE);
	assert (next->acc_length >= TCP_MIN_HDR_SIZE);

	first_hdr= (tcp_hdr_t *)ptr2acc_data(first);
	next_hdr= (tcp_hdr_t *)ptr2acc_data(next);

	first_hdr_len= (first_hdr->th_data_off & TH_DO_MASK) >>2;
	next_hdr_len= (next_hdr->th_data_off & TH_DO_MASK) >> 2;

	first_data_len= first_hdr->th_chksum-first_hdr_len;
	next_data_len= next_hdr->th_chksum-next_hdr_len;

assert (tcp_LEmod4G(first_hdr->th_seq_nr, next_hdr->th_seq_nr));
assert (first_hdr_len + first_data_len == bf_bufsize(first));
#if DEBUG
 if (next_hdr_len + next_data_len != bf_bufsize(next))
 { ip_panic(( "fatal error: %d + %d != %d\n", next_hdr_len, next_data_len,
    bf_bufsize(next) )); }
#endif
assert (next_hdr_len + next_data_len == bf_bufsize(next));

	if (tcp_Lmod4G(first_hdr->th_seq_nr+first_data_len,
		next_hdr->th_seq_nr))
	{
		first->acc_ext_link= next;
		return first;
	}
	if (first_hdr->th_seq_nr == next_hdr->th_seq_nr)
		if (first_data_len <= next_data_len)
		{
			bf_afree(first);
			return next;
		}
		else
		{
			first->acc_ext_link= next->acc_ext_link;
			bf_afree(next);
			return first;
		}
	if (tcp_GEmod4G(first_hdr->th_seq_nr+first_data_len,
		next_hdr->th_seq_nr+next_data_len))
	{
		first->acc_ext_link= next->acc_ext_link;
		bf_afree(next);
		return first;
	}
	first_data_len= next_hdr->th_seq_nr-first_hdr->th_seq_nr;
	first_hdr->th_chksum= first_data_len+first_hdr_len+
		next_data_len;
	tmp_acc= bf_cut(first, 0, first_hdr_len + first_data_len);
	bf_afree(first);
	first= tmp_acc;

	if (next_hdr->th_flags & THF_PSH)
		first_hdr->th_flags |= THF_PSH;
	if (next_hdr->th_flags & THF_URG)
	{
		if (!(first_hdr->th_flags & THF_URG))
		{
			first_hdr->th_flags |= THF_URG;
			first_hdr->th_urgptr= next_hdr->th_seq_nr+
				next_hdr->th_urgptr-
				first_hdr->th_seq_nr;
		}
		else if (next_hdr->th_seq_nr+next_hdr->th_urgptr-
			first_hdr->th_seq_nr > first_hdr->th_urgptr)
			first_hdr->th_urgptr= next_hdr->th_seq_nr+
				next_hdr->th_urgptr-
				first_hdr->th_seq_nr;
	}

	next_acc= next->acc_ext_link;
	tmp_acc= bf_cut(next, next_hdr_len ,next_data_len);
	bf_afree(next);
	next= tmp_acc;
	tmp_acc= bf_append (first, next);
	tmp_acc->acc_ext_link= next_acc;
	return tmp_acc;
}

PRIVATE void create_RST(tcp_conn, ip_hdr, tcp_hdr)
tcp_conn_t *tcp_conn;
ip_hdr_t *ip_hdr;
tcp_hdr_t *tcp_hdr;
{
	acc_t *tmp_ipopt, *tmp_tcpopt;
	ip_hdropt_t ip_hdropt;
	tcp_hdropt_t tcp_hdropt;
	acc_t *RST_acc;
	ip_hdr_t *RST_ip_hdr;
	tcp_hdr_t *RST_tcp_hdr;
	char *ptr2RSThdr;
	size_t pack_size;

	tmp_ipopt= tcp_conn->tc_remipopt;
	if (tmp_ipopt)
		tmp_ipopt->acc_linkC++;
	tmp_tcpopt= tcp_conn->tc_remtcpopt;
	if (tmp_tcpopt)
		tmp_tcpopt->acc_linkC++;

	tcp_extract_ipopt (tcp_conn, ip_hdr);
	tcp_extract_tcpopt (tcp_conn, tcp_hdr);

	RST_acc= tcp_make_header (tcp_conn, &RST_ip_hdr, &RST_tcp_hdr,
		(acc_t *)0);
	if (!RST_acc)
	{
#if DEBUG
 { where(); printf("connection closed while inuse\n"); }
#endif
		return;
	}

	if (tcp_conn->tc_remipopt)
		bf_afree(tcp_conn->tc_remipopt);
	tcp_conn->tc_remipopt= tmp_ipopt;
	if (tcp_conn->tc_remtcpopt)
		bf_afree(tcp_conn->tc_remtcpopt);
	tcp_conn->tc_remtcpopt= tmp_tcpopt;

	RST_ip_hdr->ih_src= ip_hdr->ih_dst;
	RST_ip_hdr->ih_dst= ip_hdr->ih_src;

	RST_tcp_hdr->th_srcport= tcp_hdr->th_dstport;
	RST_tcp_hdr->th_dstport= tcp_hdr->th_srcport;
	if (tcp_hdr->th_flags & THF_ACK)
	{
		RST_tcp_hdr->th_seq_nr= tcp_hdr->th_ack_nr;
		RST_tcp_hdr->th_flags= THF_RST;
	}
	else
	{
		RST_tcp_hdr->th_seq_nr= 0;
		RST_tcp_hdr->th_ack_nr= htonl(ntohl(tcp_hdr->th_seq_nr)+
			tcp_hdr->th_chksum-((tcp_hdr->th_data_off &
			TH_DO_MASK) >> 2)+ (tcp_hdr->th_flags &
			THF_SYN ? 1 : 0) + (tcp_hdr->th_flags &
			THF_FIN ? 1 : 0));
		RST_tcp_hdr->th_flags= THF_RST|THF_ACK;
	}

	pack_size= bf_bufsize(RST_acc);
	RST_ip_hdr->ih_length= htons(pack_size);
	RST_tcp_hdr->th_window= htons(tcp_conn->tc_rcv_wnd);
	RST_tcp_hdr->th_chksum= 0;
	RST_tcp_hdr->th_chksum= ~tcp_pack_oneCsum (RST_acc, pack_size);
	
	if (tcp_conn->tc_frag2send)
		bf_afree(tcp_conn->tc_frag2send);
	tcp_conn->tc_frag2send= RST_acc;
}

PUBLIC void tcp_restart_fd_read (tcp_conn)
tcp_conn_t *tcp_conn;
{
	tcp_fd_t *new_fd, *hi_fd, *urgent_fd, *normal_fd, *tcp_fd;

#if DEBUG & 256
 { where(); printf("tcp_restart_fd_read called\n"); }
#endif
	do
	{
		tcp_fd= tcp_conn->tc_readuser;

		if (tcp_fd)
			fd_read (tcp_fd);
		else
			tcp_fd= &tcp_fd_table[TCP_FD_NR-1];

		if (!tcp_conn->tc_readuser)
		{
			urgent_fd= 0;
			normal_fd= 0;
			for (new_fd= tcp_fd+1, hi_fd=
				&tcp_fd_table[TCP_FD_NR]; new_fd<hi_fd;
				new_fd++)
				switch_read_fd(tcp_conn, new_fd,
					&urgent_fd, &normal_fd);
			for (new_fd= tcp_fd_table, hi_fd= tcp_fd+1;
				new_fd < hi_fd; new_fd++)
				switch_read_fd(tcp_conn, new_fd,
					&urgent_fd, &normal_fd);
			if (urgent_fd)
				tcp_fd= urgent_fd;
			else
				tcp_fd= normal_fd;
			tcp_conn->tc_readuser= tcp_fd;
		}
		else
			return;
	} while (tcp_conn->tc_readuser);
}

PRIVATE void switch_read_fd (tcp_conn, new_fd, ref_urg_fd,
	ref_norm_fd)
tcp_conn_t *tcp_conn;
tcp_fd_t *new_fd, **ref_urg_fd, **ref_norm_fd;
{
	if (!(new_fd->tf_flags & TFF_INUSE))
		return;
	if (new_fd->tf_conn != tcp_conn)
		return;
	if (!(new_fd->tf_flags & TFF_READ_IP))
		return;
	if (new_fd->tf_flags & TFF_RECV_URG)
	{
		if (!*ref_urg_fd)
			*ref_urg_fd= new_fd;
	}
	else
	{
		if (!*ref_norm_fd)
			*ref_norm_fd= new_fd;
	}
}

PRIVATE void fd_read(tcp_fd)
tcp_fd_t *tcp_fd;
{
	tcp_conn_t *tcp_conn;
	size_t data_size, read_size;
	acc_t * data;
	int urg, result;
	int more2write;

	more2write= FALSE;
	tcp_conn= tcp_fd->tf_conn;

	assert (tcp_fd->tf_flags & TFF_READ_IP);
	if (tcp_conn->tc_state == TCS_CLOSED)
	{
		tcp_conn->tc_readuser= 0;
#if DEBUG
 { where(); printf("calling tcp_reply_read\n"); }
#endif
		if (tcp_fd->tf_read_offset)
			tcp_reply_read (tcp_fd, tcp_fd->tf_read_offset);
		else
			tcp_reply_read (tcp_fd, tcp_conn->tc_error);
		return;
	}

#if URG_PROPERLY_IMPLEMENTED
	urg= (tcp_GEmod4G(tcp_conn->tc_RCV_UP, tcp_conn->tc_RCV_LO) &&
		tcp_Lmod4G(tcp_conn->tc_RCV_UP, tcp_conn->tc_RCV_NXT));
#else
#define urg 0
#endif
	if (urg && !(tcp_fd->tf_flags & TFF_RECV_URG))
	{
		tcp_conn->tc_readuser= 0;
#if DEBUG
 { where(); printf("calling tcp_reply_read\n"); }
#endif
		if (tcp_fd->tf_read_offset)
			tcp_reply_read (tcp_fd, tcp_fd->tf_read_offset);
		else
			tcp_reply_read (tcp_fd, EURG);
		return;
	}
	else if (!urg && (tcp_fd->tf_flags & TFF_RECV_URG))
	{
		tcp_conn->tc_readuser= 0;
#if DEBUG
 { where(); printf("calling tcp_reply_read\n"); }
#endif
		if (tcp_fd->tf_read_offset)
			tcp_reply_read (tcp_fd, tcp_fd->tf_read_offset);
		else
			tcp_reply_read(tcp_fd, ENOURG);
		return;
	}
	data_size= tcp_conn->tc_RCV_NXT-tcp_conn->tc_RCV_LO;
	if (tcp_conn->tc_flags & TCF_FIN_RECV)
		data_size--;
	if (urg)
		read_size= tcp_conn->tc_RCV_UP+1-tcp_conn->tc_RCV_LO;
	else
		read_size= data_size;

	if (read_size>tcp_fd->tf_read_count)
		read_size= tcp_fd->tf_read_count;

	if (read_size)
	{
		if (read_size == data_size)
			data= bf_dupacc(tcp_conn->tc_rcvd_data);
		else
			data= bf_cut(tcp_conn->tc_rcvd_data, 0, read_size);
		result= (*tcp_fd->tf_put_userdata) (tcp_fd->tf_srfd,
			tcp_fd->tf_read_offset, data, FALSE);
		if (tcp_conn->tc_state == TCS_CLOSED)
		{
#if DEBUG
 { where(); printf("connection closed while inuse\n"); }
#endif
			return;
		}
		if (result<0)
		{
			tcp_conn->tc_readuser= 0;
#if DEBUG
 { where(); printf("calling tcp_reply_read\n"); }
#endif
			if (tcp_fd->tf_read_offset)
				tcp_reply_read(tcp_fd, tcp_fd->
					tf_read_offset);
			else
				tcp_reply_read(tcp_fd, result);
			return;
		}
		tcp_fd->tf_read_offset += read_size;
		tcp_fd->tf_read_count -= read_size;

		if (data_size == read_size)
		{
			bf_afree(tcp_conn->tc_rcvd_data);
			tcp_conn->tc_rcvd_data= 0;
		}
		else
		{
			data= tcp_conn->tc_rcvd_data;
			tcp_conn->tc_rcvd_data= bf_cut(data,
				read_size, data_size-read_size);
			bf_afree(data);
		}
		tcp_conn->tc_RCV_LO += read_size;
		data_size -= read_size;
	}
	if (tcp_conn->tc_RCV_HI-tcp_conn->tc_RCV_LO < (tcp_conn->
		tc_rcv_wnd >> 2))
	{
		tcp_conn->tc_RCV_HI= tcp_conn->tc_RCV_LO + 
			tcp_conn->tc_rcv_wnd;
		tcp_conn->tc_flags |= TCF_SEND_ACK;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
		more2write= TRUE;
	}
	if (!data_size && (tcp_conn->tc_flags & TCF_RCV_PUSH))
	{
		tcp_conn->tc_flags &= ~TCF_RCV_PUSH;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
		if (tcp_fd->tf_read_offset)
		{
			tcp_conn->tc_readuser= 0;
#if DEBUG & 256
 { where(); printf("calling tcp_reply_read\n"); }
#endif
			tcp_reply_read (tcp_fd, tcp_fd->tf_read_offset);
			if (more2write)
				tcp_restart_write(tcp_conn);
			return;
		}
	}
	if ((tcp_conn->tc_flags & TCF_FIN_RECV) ||
		!tcp_fd->tf_read_count)
	{
		tcp_conn->tc_readuser= 0;
#if DEBUG & 256
 { where(); printf("calling tcp_reply_read\n"); }
#endif
		tcp_reply_read (tcp_fd, tcp_fd->tf_read_offset);
		if (more2write)
			tcp_restart_write(tcp_conn);
		return;
	}
	if (more2write)
		tcp_restart_write(tcp_conn);
}
