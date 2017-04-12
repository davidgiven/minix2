/*
arp.h
*/

#ifndef ARP_H
#define ARP_H

#define ARP_ETHERNET	1

#define ARP_REQUEST	1
#define ARP_REPLY	2
#define RARP_REQUEST	3
#define RARP_REPLY	4

/* Prototypes */

void arp_init ARGS(( void ));
int rarp_req ARGS(( int eth_port, int ref, 
	void (*func)(int fd, ipaddr_t ipaddr) ));
int arp_ip_eth_nonbl ARGS(( int eth_port, ipaddr_t ipaddr,
	ether_addr_t *ethaddr ));
int arp_ip_eth ARGS(( int eth_port, int ref, ipaddr_t,
	void (*func)(int fd, ether_addr_t*ethadd) ));
void set_ipaddr ARGS(( int eth_port, ipaddr_t ipaddr ));

#endif /* ARP_H */
