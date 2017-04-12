/*
tcp_int.h
*/

#ifndef TCP_INT_H
#define TCP_INT_H

typedef struct tcp_port
{
	int tp_minor;
	int tp_ipdev;
	int tp_flags;
	int tp_state;
	int tp_ipfd;
	acc_t *tp_pack;
	ipaddr_t tp_ipaddr;
#if DEBUG
	timer_t tp_delay_tim;
#endif
} tcp_port_t;

#define TPF_EMPTY	0x0
#define TPF_SUSPEND	0x1
#define TPF_READ_IP	0x2
#define TPF_READ_SP	0x4
#define TPF_WRITE_IP	0x8
#define TPF_WRITE_SP	0x10
#define TPF_MORE2WRITE	0x20
#define TPF_DELAY_TCP	0x40

#define TPS_EMPTY	0
#define TPS_SETPROTO	1
#define TPS_GETCONF	2
#define TPS_MAIN	3
#define TPS_ERROR	4

typedef struct tcp_fd
{
	int tf_flags;
	tcp_port_t *tf_port;
	int tf_srfd;
	int tf_ioreq;
	nwio_tcpconf_t tf_tcpconf;
	get_userdata_t tf_get_userdata;
	put_userdata_t tf_put_userdata;
	struct tcp_conn *tf_conn;
	size_t tf_write_offset;
	size_t tf_write_count;
	size_t tf_read_offset;
	size_t tf_read_count;
} tcp_fd_t;

#define TFF_EMPTY	0x0
#define TFF_INUSE	0x1
#define TFF_IOCTL_IP	0x2
#define TFF_OPTSET	0x4
#define TFF_IOC_INIT_SP	0x8
#define TFF_CONNECT	0x20
#define TFF_WRITE_IP	0x80
#define TFF_WR_URG	0x100
#define TFF_PUSH_DATA	0x200
#define TFF_READ_IP	0x400
#define TFF_RECV_URG	0x800
#define TFF_CONNECTED	0x1000

typedef struct tcp_conn
{
	int tc_flags;
	tcpport_t tc_locport;
	ipaddr_t tc_locaddr;
	tcpport_t tc_remport;
	ipaddr_t tc_remaddr;
	int tc_state;
	tcp_fd_t *tc_mainuser;
	tcp_fd_t *tc_readuser;
	tcp_fd_t *tc_writeuser;
	tcp_fd_t *tc_connuser;
	int tc_orglisten;
	time_t tc_senddis;
	u32_t tc_SND_UNA;	/* least unacknowledged sequence number */
	u32_t tc_SND_TRM;	/* next sequence number to be transmitted */
	u32_t tc_SND_NXT;	/* next sequence number for new data */
	u32_t tc_SND_UP;	/* urgent pointer, first sequence number not 
				 * urgent */
	u32_t tc_SND_PSH;	/* push pointer, data should be pushed until the
				 * push pointer is reached */
	u32_t tc_SND_WL1;
	u32_t tc_SND_WL2;
	u32_t tc_ISS;		/* initial sequence number */
	u32_t tc_RCV_LO;
	u32_t tc_RCV_NXT;
	u32_t tc_RCV_HI;
	u32_t tc_RCV_UP;
	u32_t tc_IRS;
	tcp_port_t *tc_port;
	acc_t *tc_rcvd_data;
	acc_t *tc_rcv_queue;
	acc_t *tc_send_data;
	acc_t *tc_remipopt;
	acc_t *tc_remtcpopt;
	acc_t *tc_frag2send;
	u8_t tc_tos;
	u8_t tc_ttl;
	u16_t tc_rcv_wnd;
	u16_t tc_urg_wnd;
	int tc_no_retrans;
	int tc_max_no_retrans;
	time_t tc_0wnd_to;
	time_t tc_rtt;
	time_t tc_ett;
	struct timer tc_major_timer;
	struct timer tc_minor_timer;
	struct timer tc_ack_timer;
	struct timer tc_time_wait_timer;
	u16_t tc_mss;
	u32_t tc_snd_cwnd;	/* highest sequence number to be sent */
	u32_t tc_snd_cthresh;	/* threshold for send window */
	u32_t tc_snd_cinc;	/* increment for send window threshold */
	u16_t tc_snd_wnd;	/* max send queue size */
	int tc_error;
} tcp_conn_t;

#define TCF_EMPTY		0x0
#define TCF_INUSE		0x1
#define TCF_FIN_RECV		0x2
#define TCF_RCV_PUSH		0x4
#define TCF_MORE2WRITE		0x8
#define TCF_SEND_ACK		0x10
#define TCF_FIN_SENT		0x20
#define TCF_ACK_TIMER_SET	0x40

#define TCS_CLOSED		0
#define TCS_LISTEN		1
#define TCS_SYN_RECEIVED	2
#define TCS_SYN_SENT		3
#define TCS_ESTABLISHED		4
#define TCS_FIN_WAIT_1		5
#define TCS_FIN_WAIT_2		6
#define TCS_CLOSE_WAIT		7
#define TCS_CLOSING		8
#define TCS_LAST_ACK		9
#define TCS_TIME_WAIT		10

/* tcp_recv.c */
void tcp_frag2conn ARGS(( tcp_conn_t *tcp_conn,
	acc_t *ip_pack, acc_t *tcp_pack ));
void tcp_restart_fd_read ARGS(( tcp_conn_t *tcp_conn ));

/* tcp_send.c */
void tcp_restart_write ARGS(( tcp_conn_t *tcp_conn ));
void tcp_set_ack_timer ARGS(( tcp_conn_t *tcp_conn ));
void tcp_release_retrans ARGS(( tcp_conn_t *tcp_conn, u32_t seg_ack,
	U16_t new_win ));
void tcp_set_time_wait_timer ARGS(( tcp_conn_t *tcp_conn ));
void tcp_restart_fd_write ARGS(( tcp_conn_t *tcp_conn ));
void tcp_close_connection ARGS(( tcp_conn_t *tcp_conn,
	int error ));
void tcp_restart_write_port ARGS(( tcp_port_t *tcp_port ));
void tcp_zero_wnd_to ARGS(( int conn, struct timer *timer ));
void tcp_shutdown ARGS(( tcp_conn_t *tcp_conn ));

/* tcp_lib.c */
int tcp_LEmod4G ARGS(( u32_t n1, u32_t n2 ));
int tcp_Lmod4G ARGS(( u32_t n1, u32_t n2 ));
int tcp_GEmod4G ARGS(( u32_t n1, u32_t n2 ));
int tcp_Gmod4G ARGS(( u32_t n1, u32_t n2 ));
void tcp_write_state ARGS(( tcp_conn_t *tcp_conn ));

void tcp_extract_ipopt ARGS(( tcp_conn_t *tcp_conn,
	ip_hdr_t *ip_hdr ));
void tcp_extract_tcpopt ARGS(( tcp_conn_t *tcp_conn,
	tcp_hdr_t *tcp_hdr ));
void tcp_get_ipopt ARGS(( tcp_conn_t *tcp_conn, ip_hdropt_t
	*ip_hdropt ));
void tcp_get_tcpopt ARGS(( tcp_conn_t *tcp_conn, tcp_hdropt_t
	*tcp_hdropt ));
acc_t *tcp_make_header ARGS(( tcp_conn_t *tcp_conn,
	ip_hdr_t **ref_ip_hdr, tcp_hdr_t **ref_tcp_hdr, acc_t *data ));
u16_t tcp_pack_oneCsum ARGS(( acc_t *pack,
	size_t pack_length ));
int tcp_check_conn ARGS(( tcp_conn_t *tcp_conn ));
void tcp_print_pack ARGS(( ip_hdr_t *ip_hdr, tcp_hdr_t *tcp_hdr ));
void tcp_print_conn ARGS(( tcp_conn_t *tcp_conn ));

/* tcp.c */
void tcp_restart_connect ARGS(( tcp_fd_t *tcp_fd ));
int tcp_su4listen ARGS(( tcp_fd_t *tcp_fd ));
void tcp_reply_ioctl ARGS(( tcp_fd_t *tcp_fd, int reply ));
void tcp_reply_write ARGS(( tcp_fd_t *tcp_fd, size_t reply ));
void tcp_reply_read ARGS(( tcp_fd_t *tcp_fd, size_t reply ));

#define TCP_PORT_NR	1
#define TCP_FD_NR	20
#define TCP_CONN_NR	20

EXTERN tcp_port_t tcp_port_table[TCP_PORT_NR];
EXTERN tcp_conn_t tcp_conn_table[TCP_CONN_NR];
EXTERN tcp_fd_t tcp_fd_table[TCP_FD_NR];

#endif /* TCP_INT_H */
