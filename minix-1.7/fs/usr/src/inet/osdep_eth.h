/*
inet/osdep_eth.h

Created:	Dec 30, 1991 by Philip Homburg
*/

#ifndef INET__OSDEP_ETH_H
#define INET__OSDEP_ETH_H

#define IOVEC_NR	16
#define RD_IOVEC	((ETH_MAX_PACK_SIZE + BUF_S -1)/BUF_S)

typedef struct osdep_eth_port
{
	int etp_minor;
	int etp_task;
	int etp_port;
	iovec_t etp_wr_iovec[IOVEC_NR];
	iovec_t etp_rd_iovec[RD_IOVEC];
} osdep_eth_port_t;

#endif /* INET__OSDEP_ETH_H */
