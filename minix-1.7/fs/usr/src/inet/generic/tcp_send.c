/*
tcp_send.c
*/

#include "inet.h"
#include "buf.h"
#include "clock.h"
#include "type.h"

#include "assert.h"
#include "ip.h"
#include "tcp.h"
#include "tcp_int.h"

INIT_PANIC();

#if DEBUG
#include "tcp_delay.h"

int tcp_delay_on;
u32_t tcp_delay;
#endif

#if DEBUG & 2
#define d_bufcut 1
#endif

FORWARD void write2port ARGS(( tcp_port_t *tcp_port,
	acc_t *data ));
FORWARD void major_to ARGS(( int conn, struct timer *timer ));
FORWARD void minor_to ARGS(( int conn, struct timer *timer ));
FORWARD void ack_to ARGS(( int conn, struct timer *timer ));
FORWARD void time_wait_to ARGS(( int conn,
	struct timer *timer ));
FORWARD acc_t *make_pack ARGS(( tcp_conn_t *tcp_conn ));
FORWARD void fd_write ARGS(( tcp_fd_t *tcp_fd ));
FORWARD void switch_write_fd ARGS(( tcp_conn_t *tcp_conn,
	tcp_fd_t *new_fd, tcp_fd_t **ref_urgent_fd,
	tcp_fd_t **ref_normal_fd, tcp_fd_t **ref_shutdown_fd ));
FORWARD void tcp_restart_write_conn ARGS (( tcp_conn_t *tcp_conn ));
#if DEBUG
FORWARD void tcp_delay_to ARGS(( int ref, timer_t *timer ));
#endif

PUBLIC void tcp_restart_write (tcp_conn)
tcp_conn_t *tcp_conn;
{
	tcp_port_t *tcp_port;

#if DEBUG & 256
 { where(); printf("in tcp_restart_write\n"); }
#endif
assert (tcp_conn->tc_flags & TCF_INUSE);

	tcp_conn->tc_flags |= TCF_MORE2WRITE;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
	tcp_port= tcp_conn->tc_port;

	tcp_port->tp_flags |= TPF_MORE2WRITE;

	if (tcp_port->tp_flags & TPF_WRITE_IP)
		return;

	tcp_restart_write_port (tcp_port);
}

PUBLIC void tcp_restart_write_port (tcp_port)
tcp_port_t *tcp_port;
{
	tcp_conn_t *tcp_conn, *hi_conn;

#if DEBUG & 256
 { where(); printf("in tcp_restart_write_port\n"); }
#endif
assert (tcp_port->tp_flags & TPF_MORE2WRITE);
assert (!(tcp_port->tp_flags & TPF_WRITE_IP));

	while(tcp_port->tp_flags & TPF_MORE2WRITE)
	{
		tcp_port->tp_flags &= ~TPF_MORE2WRITE;
		for (tcp_conn= tcp_conn_table, hi_conn=
			&tcp_conn_table[TCP_CONN_NR]; tcp_conn<hi_conn;
			tcp_conn++)
		{
			if ((tcp_conn->tc_flags & (TCF_INUSE|
				TCF_MORE2WRITE)) != (TCF_INUSE|
				TCF_MORE2WRITE))
				continue;
			if (tcp_conn->tc_port != tcp_port)
				continue;
			tcp_conn->tc_flags &= ~TCF_MORE2WRITE;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
#if DEBUG & 256
 { where(); printf("calling tcp_restart_write_conn(&tcp_conn_table[%d])\n",
	tcp_conn - tcp_conn_table); }
#endif
			tcp_restart_write_conn (tcp_conn);
			if (tcp_port->tp_flags & TPF_WRITE_IP)
			{
#if DEBUG & 256
 { where(); printf("setting TCF_MORE2WRITE because of TPF_WRITE_IP\n"); }
#endif
				tcp_port->tp_flags |= TPF_MORE2WRITE;
				return;
			}
			tcp_port->tp_flags |= TPF_MORE2WRITE;
		}
	}
}

PRIVATE void tcp_restart_write_conn (tcp_conn)
tcp_conn_t *tcp_conn;
{
	ip_hdr_t *ip_hdr;
	tcp_hdr_t *tcp_hdr;
	acc_t *pack2write;

#if DEBUG & 256
 { where(); printf("in tcp_restart_write_conn\n"); }
#endif
assert (tcp_conn->tc_flags & TCF_INUSE);

	if (tcp_conn->tc_port->tp_flags & TPF_WRITE_IP)
	{
		tcp_conn->tc_flags |= TCF_MORE2WRITE;
#if DEBUG & 16
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
		tcp_conn->tc_port->tp_flags |= TPF_MORE2WRITE;
		return;
	}

	if (tcp_conn->tc_frag2send)
	{
		pack2write= tcp_conn->tc_frag2send;
		tcp_conn->tc_frag2send= 0;
#if DEBUG & 256
 { where(); printf("calling write2port\n"); }
#endif
		write2port(tcp_conn->tc_port, pack2write);

		if (tcp_conn->tc_port->tp_flags & TPF_WRITE_IP)
		{
			tcp_conn->tc_flags |= TCF_MORE2WRITE;
#if DEBUG & 16
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
			tcp_conn->tc_port->tp_flags |= TPF_MORE2WRITE;
			return;
		}
	}
	
	if (tcp_conn->tc_state == TCS_CLOSED || tcp_conn->tc_state ==
		TCS_LISTEN)
		return;


	if (tcp_conn->tc_no_retrans >
		tcp_conn->tc_max_no_retrans)
	{
#if DEBUG
 { where(); printf("calling tcp_close_connection\n"); }
#endif
		tcp_close_connection(tcp_conn, ETIMEDOUT);
		return;
	}

	while (pack2write= make_pack(tcp_conn))
	{
/*		if (tcp_conn->tc_state == TCS_CLOSED)
			return; */ /* XXX Why is this? */
		if (tcp_conn->tc_port->tp_flags & TPF_WRITE_IP)
		{
			tcp_conn->tc_frag2send= pack2write;
			tcp_conn->tc_flags |= TCF_MORE2WRITE;
#if DEBUG & 16
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
			tcp_conn->tc_port->tp_flags |= TPF_MORE2WRITE;
			return;
		}
		write2port(tcp_conn->tc_port, pack2write);
		if (tcp_conn->tc_port->tp_flags & TPF_WRITE_IP)
		{
			tcp_conn->tc_flags |= TCF_MORE2WRITE;
#if DEBUG & 16
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
			tcp_conn->tc_port->tp_flags |= TPF_MORE2WRITE;
			return;
		}
	}
}

PRIVATE acc_t *make_pack(tcp_conn)
tcp_conn_t *tcp_conn;
{
	acc_t *pack2write, *tmp_pack;
	tcp_hdr_t *tcp_hdr;
	ip_hdr_t *ip_hdr;
	int tot_hdr_size;
	u32_t seg_seq, seg_lo_data, queue_lo_data, seg_hi, seg_hi_data;
	u16_t seg_up;
	u8_t seg_flags;
	time_t new_dis;
	size_t pack_size;
	time_t major_timeout, minor_timeout;

#if DEBUG & 256
 { where(); printf("make_pack called\n"); }
#endif
	switch (tcp_conn->tc_state)
	{
	case TCS_CLOSED:
		return 0;
	case TCS_SYN_RECEIVED:
	case TCS_SYN_SENT:
		if (tcp_conn->tc_SND_TRM == tcp_conn->tc_SND_NXT &&
			!(tcp_conn->tc_flags & TCF_SEND_ACK))
		{
#if DEBUG & 256
 { where(); printf("make_pack returned\n"); }
#endif
			return 0;
		}
		major_timeout= 0;
		tcp_conn->tc_flags &= ~TCF_SEND_ACK;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif

		pack2write= tcp_make_header(tcp_conn, &ip_hdr, &tcp_hdr, 
			(acc_t *)0);
		if (!pack2write)
		{
 { where(); printf("connection closed while inuse\n"); }
#if DEBUG
 { where(); printf("make_pack returned\n"); }
#endif
			return 0;
		}
		tot_hdr_size= bf_bufsize(pack2write);
		seg_seq= tcp_conn->tc_SND_TRM;
		if (tcp_conn->tc_state == TCS_SYN_SENT)
			seg_flags= 0;
		else
			seg_flags= THF_ACK;	/* except for TCS_SYN_SENT
						 * ack is always present */

		if (seg_seq == tcp_conn->tc_ISS)
		{
			seg_flags |= THF_SYN;
			tcp_conn->tc_SND_TRM++;
			if (!tcp_conn->tc_ett)
				tcp_conn->tc_ett= get_time();
				/* fill in estimated transmition time field */

			major_timeout= get_time() + (tcp_conn->tc_rtt *
				(tcp_conn->tc_no_retrans + 2));
		}
		tcp_hdr->th_seq_nr= htonl(seg_seq);
		tcp_hdr->th_ack_nr= htonl(tcp_conn->tc_RCV_NXT);
		tcp_hdr->th_flags= seg_flags;
		tcp_hdr->th_window= htons(tcp_conn->tc_mss);
			/* Initially we allow one segment */

		ip_hdr->ih_length= htons(tot_hdr_size);
		tcp_hdr->th_chksum= ~tcp_pack_oneCsum(pack2write, tot_hdr_size);

		new_dis= get_time() + 2*HZ*tcp_conn->tc_ttl;
		if (new_dis > tcp_conn->tc_senddis)
			tcp_conn->tc_senddis= new_dis;

		if (major_timeout)
		{
			tcp_conn->tc_no_retrans++;
#if DEBUG & 256
 { where(); printf("setting major_to\n"); }
#endif
			clck_timer(&tcp_conn->tc_major_timer, 
				major_timeout, major_to, 
				tcp_conn-tcp_conn_table);
		}
		if (tcp_conn->tc_flags & TCF_ACK_TIMER_SET)
		{
			tcp_conn->tc_flags &= ~TCF_ACK_TIMER_SET;
#if DEBUG & 16
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
			clck_untimer(&tcp_conn->tc_ack_timer);
		}
#if DEBUG & 256
 { where(); printf("make_pack returned\n"); }
#endif
		return pack2write;

		break;

	case TCS_ESTABLISHED:
	case TCS_FIN_WAIT_1:
	case TCS_FIN_WAIT_2:
	case TCS_CLOSE_WAIT:
	case TCS_CLOSING:
	case TCS_LAST_ACK:
	case TCS_TIME_WAIT:

#if DEBUG & 256
 { where(); printf("SND_TRM= 0x%x, snd_cwnd= 0x%x\n", tcp_conn->tc_SND_TRM, 
	tcp_conn->tc_snd_cwnd); }
#endif
assert (tcp_LEmod4G(tcp_conn->tc_SND_TRM, tcp_conn->tc_snd_cwnd));
assert (tcp_LEmod4G(tcp_conn->tc_SND_TRM, tcp_conn->tc_SND_NXT));

		if ((tcp_conn->tc_SND_TRM == tcp_conn->tc_snd_cwnd ||
			tcp_conn->tc_SND_TRM == tcp_conn->tc_SND_NXT) &&
			!(tcp_conn->tc_flags & TCF_SEND_ACK))
		{
#if DEBUG & 256
 { where(); printf("nothing to do\n"); }
#endif
#if DEBUG & 256
 { where(); printf("make_pack returned\n"); }
#endif
			return 0;
		}

		major_timeout= 0;
		tcp_conn->tc_flags &= ~TCF_SEND_ACK;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif

		pack2write= tcp_make_header (tcp_conn, &ip_hdr, 
			&tcp_hdr, (acc_t *)0);
		if (!pack2write)
		{
 { where(); printf("connection closed while inuse\n"); }
#if DEBUG
 { where(); printf("make_pack returned\n"); }
#endif
			return 0;
		}
bf_chkbuf(pack2write);
		tot_hdr_size= bf_bufsize(pack2write);
		seg_seq= tcp_conn->tc_SND_TRM;
		seg_flags= THF_ACK;
assert(tcp_Gmod4G(seg_seq, tcp_conn->tc_ISS));

		seg_lo_data= seg_seq;
		queue_lo_data= tcp_conn->tc_SND_UNA;
assert(tcp_Gmod4G(queue_lo_data, tcp_conn->tc_ISS));
assert (tcp_check_conn(tcp_conn));

		seg_hi= tcp_conn->tc_SND_NXT;
		seg_hi_data= seg_hi;
		if (tcp_conn->tc_flags & TCF_FIN_SENT)
		{
#if DEBUG & 256
 { where(); printf("Setting FIN flags\n"); }
#endif
			if (seg_seq != seg_hi)
				seg_flags |= THF_FIN;
			if (queue_lo_data == seg_hi_data)
				queue_lo_data--;
			if (seg_lo_data == seg_hi_data)
				seg_lo_data--;
			seg_hi_data--;
		}

		if (seg_hi_data - seg_lo_data > tcp_conn->tc_mss -
			tot_hdr_size)
		{
			seg_hi_data= seg_lo_data + tcp_conn->tc_mss -
				tot_hdr_size;
			seg_hi= seg_hi_data;
#if DEBUG & 256
 { where(); printf("Clearing FIN flags\n"); }
#endif
			seg_flags &= ~THF_FIN;
		}
		if (tcp_Gmod4G(seg_hi_data, tcp_conn->tc_snd_cwnd))
		{
			seg_hi_data= tcp_conn->tc_snd_cwnd;
			seg_hi= seg_hi_data;
#if DEBUG & 256
 { where(); printf("Clearing FIN flags\n"); }
#endif
			seg_flags &= ~THF_FIN;
		}

		if (tcp_Gmod4G(tcp_conn->tc_SND_UP, seg_lo_data) &&
			tcp_LEmod4G(tcp_conn->tc_SND_UP, seg_hi_data))
		{
			seg_up= tcp_conn->tc_SND_UP-seg_seq;
			seg_flags |= THF_URG;
#if DEBUG
 { where(); printf("seg_up= %d\n", seg_up); }
#endif
		}
		if (tcp_Gmod4G(tcp_conn->tc_SND_PSH, seg_lo_data) &&
			tcp_LEmod4G(tcp_conn->tc_SND_PSH, seg_hi_data))
		{
			seg_flags |= THF_PSH;
		}

		tcp_conn->tc_SND_TRM= seg_hi;

		if (seg_hi-seg_seq)
		{
			if (!tcp_conn->tc_ett)
				tcp_conn->tc_ett= get_time();

			if (seg_seq == tcp_conn->tc_SND_UNA)
				major_timeout= get_time() + (tcp_conn->tc_rtt *
				(tcp_conn->tc_no_retrans + 1));
		}
		if (seg_hi_data-seg_lo_data)
		{
			tmp_pack= pack2write;
			while (tmp_pack->acc_next)
				tmp_pack= tmp_pack->acc_next;
assert (tcp_check_conn(tcp_conn));
bf_chkbuf(pack2write);
			tmp_pack->acc_next= bf_cut(tcp_conn->tc_send_data, 
				(unsigned)(seg_lo_data-queue_lo_data), 
				(unsigned) (seg_hi_data-seg_lo_data));
bf_chkbuf(pack2write);
		}

		tcp_hdr->th_seq_nr= htonl(seg_seq);
		tcp_hdr->th_ack_nr= htonl(tcp_conn->tc_RCV_NXT);
		tcp_hdr->th_flags= seg_flags;
		tcp_hdr->th_window= htons(tcp_conn->tc_RCV_HI -
			tcp_conn->tc_RCV_NXT);
		tcp_hdr->th_urgptr= htons(seg_up);

		pack_size= bf_bufsize(pack2write);
		ip_hdr->ih_length= htons(pack_size);
bf_chkbuf(pack2write);
		tcp_hdr->th_chksum= ~tcp_pack_oneCsum(pack2write,
			pack_size);
bf_chkbuf(pack2write);
		new_dis= get_time() + 2*HZ*tcp_conn->tc_ttl;
		if (new_dis > tcp_conn->tc_senddis)
			tcp_conn->tc_senddis= new_dis;
bf_chkbuf(pack2write);
		if (major_timeout)
		{
			tcp_conn->tc_no_retrans++;

#if DEBUG & 256
 { where(); printf("setting major_to\n"); }
#endif
			clck_timer(&tcp_conn->tc_major_timer, 
				major_timeout,
				major_to, tcp_conn-tcp_conn_table);
		}
		if (tcp_conn->tc_flags & TCF_ACK_TIMER_SET)
		{
			tcp_conn->tc_flags &= ~TCF_ACK_TIMER_SET;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
			clck_untimer(&tcp_conn->tc_ack_timer);
		}
#if DEBUG & 256
 { where(); printf("make_pack returned\n"); }
#endif
		return pack2write;
	default:
#if DEBUG
 { where(); printf("tcp_conn_table[%d].tc_state= %d\n", tcp_conn-tcp_conn_table,
	tcp_conn->tc_state); }
#endif
		ip_panic(( "Illegal state" ));
	}
#if DEBUG
 { where(); printf("make_pack returned\n"); }
#endif
}

PRIVATE void write2port(tcp_port, data)
tcp_port_t *tcp_port;
acc_t *data;
{
	int result;

#if DEBUG & 256
 { where(); printf("in write2port\n"); }
#endif
	assert (!(tcp_port->tp_flags & TPF_WRITE_IP));

	tcp_port->tp_flags |= TPF_WRITE_IP;
	tcp_port->tp_pack= data;

bf_chkbuf(data);
#if DEBUG
	if (tcp_delay_on)
	{
		if (tcp_port->tp_flags & TPF_DELAY_TCP)
			tcp_port->tp_flags &= ~TPF_DELAY_TCP;
		else
		{
			tcp_port->tp_flags |= TPF_WRITE_SP;
			clck_timer(&tcp_port->tp_delay_tim,
				get_time() + tcp_delay,
				tcp_delay_to, tcp_port-
				tcp_port_table);
			return;
		}
	}
#endif
	result= ip_write (tcp_port->tp_ipfd, bf_bufsize(data));

	if (result == NW_SUSPEND)
	{
		tcp_port->tp_flags |= TPF_WRITE_SP;
		return;
	}
assert(result == NW_OK);
	tcp_port->tp_flags &= ~TPF_WRITE_IP;
assert(!(tcp_port->tp_flags & (TPF_WRITE_IP|TPF_WRITE_SP)));
}

PRIVATE void major_to(conn, timer)
int conn;
struct timer *timer;
{
	tcp_conn_t *tcp_conn;
	u16_t mss, mss2;

#if DEBUG & 256
 { where(); printf("in major_to\n"); }
#endif
	tcp_conn= &tcp_conn_table[conn];
#if DEBUG & 256
 { where(); printf("major_to: snd_cwnd-SND_UNA= %lu, no_retrans: %d snd_cthresh= %d\n",
	tcp_conn->tc_snd_cwnd-tcp_conn->tc_SND_UNA, tcp_conn->tc_no_retrans,
	tcp_conn->tc_snd_cthresh); }
#endif
	assert(tcp_conn->tc_flags & TCF_INUSE);
	assert(tcp_conn->tc_state != TCS_CLOSED &&
		tcp_conn->tc_state != TCS_LISTEN);

	clck_untimer(&tcp_conn->tc_minor_timer);
	tcp_conn->tc_SND_TRM= tcp_conn->tc_SND_UNA;

	mss= tcp_conn->tc_mss;
	mss2= 2*mss;

	tcp_conn->tc_snd_cwnd= tcp_conn->tc_SND_TRM + mss2;
#if DEBUG & 256
 { where(); printf("snd_cwnd is now %d\n", tcp_conn->tc_snd_cwnd); }
#endif
	tcp_conn->tc_snd_cthresh /= 2;
	if (tcp_conn->tc_snd_cthresh < mss2)
		tcp_conn->tc_snd_cthresh= mss2;
	tcp_conn->tc_snd_cinc= ((unsigned long)mss*mss)/tcp_conn->
		tc_snd_cthresh;

	tcp_restart_write(tcp_conn);
}

PRIVATE void minor_to(conn, timer)
int conn;
struct timer *timer;
{
	tcp_conn_t *tcp_conn;

#if DEBUG
 { where(); printf("in minor_to\n"); }
#endif
	tcp_conn= &tcp_conn_table[conn];
	assert(tcp_conn->tc_flags & TCF_INUSE);
	assert(tcp_conn->tc_state != TCS_CLOSED &&
		tcp_conn->tc_state != TCS_LISTEN);

	tcp_restart_write(tcp_conn);
}

PUBLIC void tcp_release_retrans(tcp_conn, seg_ack, new_win)
tcp_conn_t *tcp_conn;
u32_t seg_ack;
u16_t new_win;
{
	size_t size, offset;
	acc_t *old_pack, *new_pack;
	time_t retrans_time, curr_time;
	u32_t queue_lo, queue_hi;
	u16_t cwnd, incr, mss, mss2, cthresh;

#if DEBUG & 256
 { where(); printf("in release_retrans(&tcp_conn_table[%d], 0x%x, %d)\n",
	tcp_conn-tcp_conn_table, seg_ack, new_win); }
#endif
assert (tcp_GEmod4G(seg_ack, tcp_conn->tc_SND_UNA));
assert (tcp_LEmod4G(seg_ack, tcp_conn->tc_SND_NXT));

	if (tcp_Gmod4G(seg_ack, tcp_conn->tc_SND_UNA))
	{
		tcp_conn->tc_no_retrans= 0;
		curr_time= get_time();
		if (curr_time < tcp_conn->tc_ett)
			retrans_time= 0;
		else
			retrans_time= curr_time-tcp_conn->tc_ett;
		if (tcp_conn->tc_rtt*2 < retrans_time)
		{
			tcp_conn->tc_rtt *= 2;
assert (tcp_conn->tc_rtt);
		}
		else if (tcp_conn->tc_rtt > retrans_time*2)
		{
			tcp_conn->tc_rtt= tcp_conn->tc_rtt/2+1;
assert (tcp_conn->tc_rtt);
		}

		if (seg_ack == tcp_conn->tc_SND_NXT)
			tcp_conn->tc_ett= 0;
		else
		{
			tcp_conn->tc_ett += (seg_ack-tcp_conn->
				tc_SND_UNA) * tcp_conn->tc_rtt /
				(tcp_conn->tc_SND_NXT-tcp_conn->
				tc_SND_UNA);
		}
		queue_lo= tcp_conn->tc_SND_UNA;
		queue_hi= tcp_conn->tc_SND_NXT;

		tcp_conn->tc_SND_UNA= seg_ack;
		if (tcp_Lmod4G(tcp_conn->tc_SND_TRM, seg_ack))
			tcp_conn->tc_SND_TRM= seg_ack;
		if (tcp_Lmod4G(tcp_conn->tc_snd_cwnd, seg_ack))
		{
			tcp_conn->tc_snd_cwnd= seg_ack;
#if DEBUG & 256
 { where(); printf("snd_cwnd is now %d\n", tcp_conn->tc_snd_cwnd); }
#endif
		}

		if (queue_lo == tcp_conn->tc_ISS)
			queue_lo++;

		if (tcp_conn->tc_flags & TCF_FIN_SENT)
		{
			if (seg_ack == queue_hi)
				seg_ack--;
			if (queue_lo == queue_hi)
				queue_lo--;
			queue_hi--;
		}

		offset= seg_ack - queue_lo;
		size= queue_hi - seg_ack;
		old_pack= tcp_conn->tc_send_data;
		tcp_conn->tc_send_data= 0;

		if (!size)
		{
			new_pack= 0;
			tcp_conn->tc_snd_cwnd= tcp_conn->tc_SND_UNA + 
				2*tcp_conn->tc_mss;
			/* Reset window if a write is completed */
#if DEBUG & 256
 { where(); printf("snd_cwnd is now %d\n", tcp_conn->tc_snd_cwnd); }
#endif
		}
		else
			new_pack= bf_cut(old_pack, offset, size);
		bf_afree(old_pack);
		tcp_conn->tc_send_data= new_pack;
assert (tcp_check_conn(tcp_conn));

		if (tcp_conn->tc_state == TCS_CLOSED)
		{
	 { where(); printf("connection closed while inuse\n"); }
			return;
		}

		if (tcp_conn->tc_ett)
		{
#if DEBUG & 256
 { where(); printf("setting major_to\n"); }
#endif
			clck_timer(&tcp_conn->tc_major_timer,
			tcp_conn->tc_ett + tcp_conn->tc_rtt,
			major_to, tcp_conn-tcp_conn_table);
		}
	}

	mss= tcp_conn->tc_mss;
	cthresh= tcp_conn->tc_snd_cthresh;
	mss2= 2*mss;

	if (new_win < mss2)
	{
		cwnd= 0;
		if (new_win >= mss)
			incr= mss;
		else
		{
			if (new_win)
				incr= new_win;
			else
				incr= 1;
		}
	}
	else
	{
		cwnd= tcp_conn->tc_snd_cwnd - tcp_conn->tc_SND_UNA;
		incr= mss2;
		if (cwnd+incr > new_win)
		{
			incr= mss;
			if (cwnd+incr > new_win)
				incr= 0;
		}
	}

assert (cthresh >= mss2);

	if (incr && cwnd+incr >  cthresh)
	{
		incr -= mss;
		if (incr && cwnd+incr >  cthresh)
			incr -= mss;
	}
	if (cwnd+incr+mss>cthresh && cthresh<tcp_conn->tc_snd_wnd)
		tcp_conn->tc_snd_cthresh += tcp_conn->tc_snd_cinc;

	tcp_conn->tc_snd_cwnd= tcp_conn->tc_SND_UNA+cwnd+incr;
#if DEBUG & 256
 { where(); printf("snd_cwnd is now 0x%x\n", tcp_conn->tc_snd_cwnd); }
#endif
	if (tcp_Gmod4G(tcp_conn->tc_SND_TRM, tcp_conn->tc_snd_cwnd))
		tcp_conn->tc_SND_TRM= tcp_conn->tc_snd_cwnd;

	if (tcp_Gmod4G(tcp_conn->tc_snd_cwnd, tcp_conn->tc_SND_TRM) &&
		tcp_Gmod4G(tcp_conn->tc_SND_NXT, tcp_conn->tc_SND_TRM))
		tcp_restart_write(tcp_conn);

	if (tcp_conn->tc_writeuser)
		tcp_restart_fd_write(tcp_conn);

assert (tcp_check_conn(tcp_conn));

}

PUBLIC void tcp_restart_fd_write(tcp_conn)
tcp_conn_t *tcp_conn;
{
	tcp_fd_t *urgent_fd, *normal_fd, *shutdown_fd,
		*new_fd, *hi_fd, *tcp_fd;
	int closed_connection;

#if DEBUG & 256
 { where(); printf("in restart_fd_write\n"); }
#endif
	do
	{
		tcp_fd= tcp_conn->tc_writeuser;

		closed_connection= (tcp_conn->tc_state == TCS_CLOSED);
		if (tcp_fd)
			fd_write(tcp_fd);
		else
			tcp_fd= &tcp_fd_table[TCP_FD_NR-1];

		if (!closed_connection &&
			tcp_conn->tc_state == TCS_CLOSED)
		{
#if DEBUG
  { where(); printf("connection closed while inuse\n"); }
#endif
			return;
		}

		if (!tcp_conn->tc_writeuser)
		{
			urgent_fd= 0;
			normal_fd= 0;
			shutdown_fd= 0;
			for (new_fd= tcp_fd+1, hi_fd=
				&tcp_fd_table[TCP_FD_NR]; new_fd<hi_fd;
				new_fd++)
				switch_write_fd(tcp_conn, new_fd,
					&urgent_fd, &normal_fd,
					&shutdown_fd);
			for (new_fd= tcp_fd_table, hi_fd= tcp_fd+1;
				new_fd < hi_fd; new_fd++)
				switch_write_fd(tcp_conn, new_fd,
					&urgent_fd, &normal_fd,
					&shutdown_fd);
			if (urgent_fd)
				tcp_fd= urgent_fd;
			else if (normal_fd)
				tcp_fd= normal_fd;
			else
				tcp_fd= shutdown_fd;
			tcp_conn->tc_writeuser= tcp_fd;
		}
		else
			return;
	} while (tcp_conn->tc_writeuser);
}



PRIVATE void switch_write_fd (tcp_conn, new_fd, ref_urg_fd,
	ref_norm_fd, ref_shut_fd)
tcp_conn_t *tcp_conn;
tcp_fd_t *new_fd, **ref_urg_fd, **ref_norm_fd, **ref_shut_fd;
{
	if (!(new_fd->tf_flags & TFF_INUSE))
		return;
	if (new_fd->tf_conn != tcp_conn)
		return;
	if (new_fd->tf_flags & TFF_WRITE_IP)
		if (new_fd->tf_flags & TFF_WR_URG)
		{
			if (!*ref_urg_fd)
				*ref_urg_fd= new_fd;
		}
		else
		{
			if (!*ref_norm_fd)
				*ref_norm_fd= new_fd;
		}
	else if ((new_fd->tf_flags & TFF_IOCTL_IP) &&
		new_fd->tf_ioreq == NWIOTCPSHUTDOWN)
		if (!*ref_shut_fd)
			*ref_shut_fd= new_fd;
}

PRIVATE void fd_write (tcp_fd)
tcp_fd_t *tcp_fd;
{
	tcp_conn_t *tcp_conn;
	int urg, push;
	u32_t max_seq;
	size_t max_count, max_trans, write_count, send_count;
	acc_t *data, *tmp_acc, *send_data;
	int restart_write;

	restart_write= FALSE;
	tcp_conn= tcp_fd->tf_conn;

	if (tcp_fd->tf_flags & TFF_IOCTL_IP)
	{
assert (tcp_fd->tf_ioreq == NWIOTCPSHUTDOWN);
		if (tcp_conn->tc_state == TCS_CLOSED)
		{
			tcp_conn->tc_writeuser= 0;
			tcp_reply_ioctl (tcp_fd, tcp_conn->tc_error);
			return;
		}
		tcp_conn->tc_writeuser= 0;
		tcp_reply_ioctl (tcp_fd, NW_OK);
		tcp_shutdown (tcp_conn);
		return;
	}
	assert (tcp_fd->tf_flags & TFF_WRITE_IP);
	if (tcp_conn->tc_state == TCS_CLOSED)
	{
		tcp_conn->tc_writeuser= 0;
#if DEBUG & 256
 { where(); printf("calling tcp_reply_write()\n"); }
#endif
		if (tcp_fd->tf_write_offset)
			tcp_reply_write(tcp_fd,
				tcp_fd->tf_write_offset);
		else
			tcp_reply_write(tcp_fd, tcp_conn->tc_error);
		return;
	}
assert (!(tcp_conn->tc_flags & TCF_FIN_SENT));
assert (tcp_conn->tc_SND_UNA != tcp_conn->tc_ISS);

	urg= (tcp_fd->tf_flags & TFF_WR_URG);
	push= (tcp_fd->tf_flags & TFF_PUSH_DATA);
#if DEBUG & 256
 if (push) { where(); printf("pushing data\n"); }
#endif

	max_seq= tcp_conn->tc_SND_UNA + tcp_conn->tc_snd_wnd;
	if (urg)
		max_seq += tcp_conn->tc_urg_wnd;
	max_count= max_seq - tcp_conn->tc_SND_UNA;
	max_trans= max_seq - tcp_conn->tc_SND_NXT;
	if (tcp_fd->tf_write_count <= max_trans)
		write_count= tcp_fd->tf_write_count;
	else if (!urg && max_trans < max_count/2)
		return;
	else
		write_count= max_trans;
	if (write_count)
	{
		data= (*tcp_fd->tf_get_userdata)
			(tcp_fd->tf_srfd, tcp_fd->tf_write_offset,
			write_count, FALSE);
		if (tcp_conn->tc_state == TCS_CLOSED)
		{
 { where(); printf("connection closed while inuse\n"); }
			if (data)
				bf_afree(data);
			return;
		}
		if (!data)
		{
			tcp_conn->tc_writeuser= 0;
assert(data);
#if DEBUG
 { where(); printf("calling tcp_reply_write()\n"); }
#endif
			if (tcp_fd->tf_write_offset)
				tcp_reply_write(tcp_fd,
					tcp_fd->tf_write_offset);
			else
				tcp_reply_write(tcp_fd, EFAULT);
			return;
		}
		tcp_fd->tf_write_offset += write_count;
		tcp_fd->tf_write_count -= write_count;

		send_data= tcp_conn->tc_send_data;
		tcp_conn->tc_send_data= 0;
		send_data= bf_append(send_data, data);
		if (tcp_conn->tc_state == TCS_CLOSED)
		{
 { where(); printf("connection closed while inuse\n"); }
			bf_afree(send_data);
			return;
		}
		tcp_conn->tc_send_data= send_data;
		tcp_conn->tc_SND_NXT += write_count;
		if (urg)
			tcp_conn->tc_SND_UP= tcp_conn->tc_SND_NXT;
		if (push && !tcp_fd->tf_write_count)
			tcp_conn->tc_SND_PSH= tcp_conn->tc_SND_NXT;

assert (tcp_check_conn(tcp_conn));
		if (tcp_Gmod4G(tcp_conn->tc_SND_NXT, tcp_conn->tc_SND_TRM) &&
			tcp_Lmod4G(tcp_conn->tc_SND_TRM, tcp_conn->tc_snd_cwnd))
			restart_write= TRUE;
	}
	if (!tcp_fd->tf_write_count)
	{
		tcp_conn->tc_writeuser= 0;
#if DEBUG & 256
 { where(); printf("calling tcp_reply_write()\n"); }
#endif
		tcp_reply_write(tcp_fd, tcp_fd->tf_write_offset);
	}
	if (restart_write)
		tcp_restart_write(tcp_conn);
}

PUBLIC void tcp_shutdown(tcp_conn)
tcp_conn_t *tcp_conn;
{
	int closed_connection;

#if DEBUG & 256
 { where(); printf("in tcp_shutdown\n"); }
#endif
	if (tcp_conn->tc_flags & TCF_FIN_SENT)
		return;
	tcp_conn->tc_flags |= TCF_FIN_SENT;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
	tcp_conn->tc_SND_NXT++;

	switch (tcp_conn->tc_state)
	{
	case TCS_CLOSED:
	case TCS_LISTEN:
	case TCS_SYN_SENT:
	case TCS_SYN_RECEIVED:
#if DEBUG & 256
 { where(); printf("calling tcp_close_connection\n"); }
#endif
		tcp_close_connection(tcp_conn, ENOTCONN);
		break;
	case TCS_ESTABLISHED:
		tcp_conn->tc_state= TCS_FIN_WAIT_1;
#if DEBUG
 { where(); tcp_write_state(tcp_conn); }
#endif
		break;
	case TCS_CLOSE_WAIT:
		tcp_conn->tc_state= TCS_LAST_ACK;
#if DEBUG
 { where(); tcp_write_state(tcp_conn); }
#endif
		break;
	}
	closed_connection= (tcp_conn->tc_state == TCS_CLOSED);

assert (tcp_check_conn(tcp_conn));

#if DEBUG & 256
 { where(); printf("calling tcp_restart_write\n"); }
#endif
		tcp_restart_write(tcp_conn);
}

PUBLIC void tcp_set_time_wait_timer(tcp_conn)
tcp_conn_t *tcp_conn;
{
	assert (tcp_conn->tc_state == TCS_TIME_WAIT);

#if DEBUG & 256
 { where(); printf("tcp_set_time_wait_timer, ttl= %d\n", tcp_conn->tc_ttl); }
#endif
	clck_timer(&tcp_conn->tc_time_wait_timer, get_time() +
		tcp_conn->tc_ttl * 2L * HZ, time_wait_to, tcp_conn-
		tcp_conn_table);
}

PUBLIC void tcp_set_ack_timer (tcp_conn)
tcp_conn_t *tcp_conn;
{
	if (tcp_conn->tc_flags & TCF_ACK_TIMER_SET)
		return;

	tcp_conn->tc_flags |= TCF_ACK_TIMER_SET;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif

#if DEBUG & 256
 { where(); printf("setting ack_timer\n"); };
#endif
	clck_timer(&tcp_conn->tc_ack_timer, get_time() +
		TCP_ACK_DELAY, ack_to, tcp_conn-tcp_conn_table);
}

/*
tcp_close_connection

*/

PUBLIC void tcp_close_connection(tcp_conn, error)
tcp_conn_t *tcp_conn;
int error;
{
#if DEBUG & 256
 { where(); printf("closing connection\n"); }
#endif

assert (tcp_check_conn(tcp_conn));
assert (tcp_conn->tc_flags & TCF_INUSE);

	tcp_conn->tc_error= error;
	if (tcp_conn->tc_state == TCS_CLOSED)
		return;

	clck_untimer (&tcp_conn->tc_major_timer);
	clck_untimer (&tcp_conn->tc_minor_timer);
#if DEBUG & 256
 { where(); printf("clearing ack_timer\n"); }
#endif
	clck_untimer (&tcp_conn->tc_ack_timer);
	clck_untimer (&tcp_conn->tc_time_wait_timer);

	tcp_conn->tc_state= TCS_CLOSED;
#if DEBUG & 16
 { where(); tcp_write_state(tcp_conn); }
#endif

	if (tcp_conn->tc_readuser)
		tcp_restart_fd_read (tcp_conn);
assert (!tcp_conn->tc_readuser);

	if (tcp_conn->tc_writeuser)
		tcp_restart_fd_write (tcp_conn);
assert (!tcp_conn->tc_writeuser);

	if (tcp_conn->tc_connuser)
	{
#if DEBUG & 256
 { where(); printf("closing and connuser present\n"); }
#endif
		tcp_restart_connect (tcp_conn->tc_connuser);
	}
assert (!tcp_conn->tc_connuser);

	if (tcp_conn->tc_rcvd_data)
	{
		bf_afree(tcp_conn->tc_rcvd_data);
		tcp_conn->tc_rcvd_data= 0;
	}
	tcp_conn->tc_flags &= ~TCF_FIN_RECV;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
	tcp_conn->tc_RCV_LO= tcp_conn->tc_RCV_NXT;

	if (tcp_conn->tc_rcv_queue)
	{
		bf_afree(tcp_conn->tc_rcv_queue);
		tcp_conn->tc_rcv_queue= 0;
	}

	if (tcp_conn->tc_send_data)
	{
#if DEBUG & 256
 { where(); printf("releasing data\n"); }
#endif
#if DEBUG & 256
 { where(); printf("SND_TRM= 0x%lx, tc_SND_NXT= 0x%lx, SND_UNA=  0x%lx\n",
	tcp_conn->tc_SND_TRM, tcp_conn->tc_SND_NXT, tcp_conn->tc_SND_UNA); }
#endif
		bf_afree(tcp_conn->tc_send_data);
		tcp_conn->tc_send_data= 0;
		tcp_conn->tc_SND_TRM=
			tcp_conn->tc_SND_NXT= tcp_conn->tc_SND_UNA;
	}
	tcp_conn->tc_SND_TRM= tcp_conn->tc_SND_NXT= tcp_conn->tc_SND_UNA;

	if (tcp_conn->tc_remipopt)
	{
		bf_afree(tcp_conn->tc_remipopt);
		tcp_conn->tc_remipopt= 0;
	}

	if (tcp_conn->tc_remtcpopt)
	{
		bf_afree(tcp_conn->tc_remtcpopt);
		tcp_conn->tc_remtcpopt= 0;
	}

	if (tcp_conn->tc_frag2send)
	{
		bf_afree(tcp_conn->tc_frag2send);
		tcp_conn->tc_remtcpopt= 0;
	}
					/* clear all flags but TCF_INUSE */
	tcp_conn->tc_flags &= TCF_INUSE;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
assert (tcp_check_conn(tcp_conn));
}

PRIVATE void ack_to(conn, timer)
int conn;
struct timer *timer;
{
	tcp_conn_t *tcp_conn;

#if DEBUG & 256
 { where(); printf("in ack_to\n"); }
#endif
	tcp_conn= &tcp_conn_table[conn];

	assert (&tcp_conn->tc_ack_timer == timer);

	assert (tcp_conn->tc_flags & TCF_ACK_TIMER_SET);

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
}

PRIVATE void time_wait_to(conn, timer)
int conn;
struct timer *timer;
{
	tcp_conn_t *tcp_conn;

	tcp_conn= &tcp_conn_table[conn];

	assert (tcp_conn->tc_state == TCS_TIME_WAIT);
	assert (&tcp_conn->tc_time_wait_timer == timer);

#if DEBUG
 { where(); printf("calling tcp_close_connection\n"); }
#endif
	tcp_close_connection (tcp_conn, ENOCONN);
}

PUBLIC void tcp_zero_wnd_to(conn, timer)
int conn;
struct timer *timer;
{
	tcp_conn_t *tcp_conn;

#if DEBUG & 256
 { where(); printf("in tcp_zero_wnd_to\n"); }
#endif
	tcp_conn= &tcp_conn_table[conn];

	assert (&tcp_conn->tc_major_timer == timer);
	assert (tcp_conn->tc_0wnd_to);

	tcp_conn->tc_0wnd_to *= 2;
	tcp_conn->tc_SND_TRM= tcp_conn->tc_SND_UNA;

	tcp_restart_write (tcp_conn);
}

#if DEBUG
PRIVATE void tcp_delay_to(ref, timer)
int ref;
timer_t *timer;
{
	tcp_port_t *tcp_port;

assert(ref >= 0 && ref < TCP_PORT_NR);
	tcp_port= &tcp_port_table[ref];
assert(timer == &tcp_port->tp_delay_tim);

	tcp_port->tp_flags &= ~(TPF_WRITE_SP|TPF_WRITE_IP);
	tcp_port->tp_flags |= TPF_DELAY_TCP;
	if (tcp_port->tp_flags & TPF_MORE2WRITE)
	{
#if DEBUG & 256
{ where(); printf("calling tcp_restart_write_port\n"); }
#endif
		write2port(tcp_port, tcp_port->tp_pack);
	}
}
#endif
