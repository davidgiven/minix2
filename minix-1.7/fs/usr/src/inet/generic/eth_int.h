/*
eth_int.h
*/

#ifndef ETH_INT_H
#define ETH_INT_H

#define ETH_PORT_NR	1	/* 1 ethernet connection */

typedef struct eth_port
{
	int etp_flags;
	ether_addr_t etp_ethaddr;
	acc_t *etp_wr_pack, *etp_rd_pack;

	osdep_eth_port_t etp_osdep;
} eth_port_t;

#define EPF_EMPTY	0x0
#define EPF_ENABLED	0x1
#define EPF_WRITE_IP	0x2
#define EPF_WRITE_SP	0x4
#define EPF_MORE2WRITE	0x8
#define EPF_READ_IP	0x10
#define EPF_READ_SP	0x20

#define EPS_EMPTY	0x0
#define EPS_LOC		0x1
#define EPS_BROAD	0x2
#define EPS_MULTI	0x4
#define EPS_PROMISC	0x8

extern eth_port_t eth_port_table[ETH_PORT_NR];

void eth_init0 ARGS(( void ));
int eth_get_stat ARGS(( eth_port_t *eth_port, eth_stat_t *eth_stat ));
void eth_write_port ARGS(( eth_port_t *eth_port ));
void eth_arrive ARGS(( eth_port_t *port, acc_t *pack ));
void eth_set_rec_conf ARGS(( eth_port_t *eth_port, u32_t flags ));
int eth_get_work ARGS(( eth_port_t *eth_port ));

#endif /* ETH_INT_H */
