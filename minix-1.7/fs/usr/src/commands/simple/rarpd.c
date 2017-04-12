/*
rarpd.c

Created:	Nov 12, 1992 by Philip Homburg

Changed:	May 13, 1995 by Kees J. Bot
		Rewrite to handle multiple ethernets.

Changed:	Jul 18, 1995 by Kees J. Bot
		Do RARP requests (formerly inet's job)
*/

#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/asynchio.h>
#include <net/hton.h>
#include <net/netlib.h>
#include <net/gen/socket.h>
#include <net/gen/netdb.h>
#include <net/gen/in.h>
#include <net/gen/inet.h>
#include <net/gen/ether.h>
#include <net/gen/eth_io.h>
#include <net/gen/if_ether.h>
#include <net/gen/ip_io.h>

#define MAX_RARP_RETRIES	5
#define RARP_TIMEOUT		5

char PATH_ETHERS[] = "/etc/ethers";

typedef struct rarp46
{
	ether_addr_t a46_dstaddr;
	ether_addr_t a46_srcaddr;
	ether_type_t a46_ethtype;
	union
	{
		struct
		{
			u16_t a_hdr, a_pro;
			u8_t a_hln, a_pln;
			u16_t a_op;
			ether_addr_t a_sha;
			u8_t a_spa[4];
			ether_addr_t a_tha;
			u8_t a_tpa[4];
		} a46_data;
		char    a46_dummy[ETH_MIN_PACK_SIZE-ETH_HDR_SIZE];
	} a46_data;
} rarp46_t;

#define a46_hdr a46_data.a46_data.a_hdr
#define a46_pro a46_data.a46_data.a_pro
#define a46_hln a46_data.a46_data.a_hln
#define a46_pln a46_data.a46_data.a_pln
#define a46_op a46_data.a46_data.a_op
#define a46_sha a46_data.a46_data.a_sha
#define a46_spa a46_data.a46_data.a_spa
#define a46_tha a46_data.a46_data.a_tha
#define a46_tpa a46_data.a46_data.a_tpa

#define RARP_ETHERNET	1

#define RARP_REQUEST	3
#define RARP_REPLY	4

ether_addr_t BCAST_ETH_ADDR =	{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
ether_addr_t NULL_ETH_ADDR /* =	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } */;

#if __minix_vmd

/* VMD Minix has two ethernets. */
#define N_ETHS		2
char an_ethdev[]=	"/dev/eth0";
char an_ipdev[]=	"/dev/ip0";
#define ethdev(i)	(an_ethdev[8]= '0' + (i), an_ethdev)
#define ipdev(i)	(an_ipdev[7]= '0' + (i), an_ipdev)

#else /* !__minix_vmd */

/* Standard Minix has only one ethernet. */
#define N_ETHS		1
char an_ethdev[]=	"/dev/eth";
char an_ipdev[]=	"/dev/ip";
#define ethdev(i)	an_ethdev
#define ipdev(i)	an_ipdev

#endif /* !__minix_vmd */

/* Network state: Sink mode or not there, unknown IP address, known IP address
 * but not set, network fully configured.
 */
typedef enum state { SINK, UNKNOWN, KNOWN, CONFIGURED } state_t;

typedef struct ethernet {
	int		n;		/* Network number. */
	state_t		state;		/* Any good? */
	int		eth_fd;		/* Open low level ethernet device. */
	ether_addr_t	eth_addr;	/* Ethernet address of this net. */
	char		packet[ETH_MAX_PACK_SIZE];	/* Incoming packet. */
	ipaddr_t	ip_addr;	/* IP address of this net. */
	ipaddr_t	ip_mask;	/* Associated netmask. */
} ethernet_t;

ethernet_t ethernets[N_ETHS];

char *prog_name;
int debug;

/* Old file reading function to map a name to an address. */
struct hostent *_gethostbyname(char *);

void onsig(int sig)
{
	switch (sig) {
	case SIGALRM:	alarm(1);	break;
	case SIGUSR1:	debug++;	break;
	case SIGUSR2:	debug= 0;	break;
	}
}

void rarp_request(ethernet_t *ep)
{
	rarp46_t rarp46;

	/* Construct a RARP request packet and send it. */
	rarp46.a46_dstaddr= BCAST_ETH_ADDR;
	rarp46.a46_hdr= HTONS(RARP_ETHERNET);
	rarp46.a46_pro= HTONS(ETH_IP_PROTO);
	rarp46.a46_hln= 6;
	rarp46.a46_pln= 4;
	rarp46.a46_op= HTONS(RARP_REQUEST);
	rarp46.a46_sha= ep->eth_addr;
	rarp46.a46_tha= ep->eth_addr;

	if (debug) {
		printf("%s: %s: requesting IP address of %s\n",
			prog_name, ethdev(ep->n),
			ether_ntoa(&ep->eth_addr));
	}
	(void) write(ep->eth_fd, &rarp46, sizeof(rarp46));
}

void rarp_reply(ethernet_t *ep, char *hostname, ipaddr_t ip_addr,
						ether_addr_t eth_addr)
{
	rarp46_t rarp46;

	/* Construct a RARP reply packet and send it. */
	rarp46.a46_dstaddr= eth_addr;
	rarp46.a46_hdr= HTONS(RARP_ETHERNET);
	rarp46.a46_pro= HTONS(ETH_IP_PROTO);
	rarp46.a46_hln= 6;
	rarp46.a46_pln= 4;
	rarp46.a46_op= HTONS(RARP_REPLY);
	rarp46.a46_sha= ep->eth_addr;
	memcpy(rarp46.a46_spa, &ep->ip_addr, sizeof(ipaddr_t));
	rarp46.a46_tha= eth_addr;
	memcpy(rarp46.a46_tpa, &ip_addr, sizeof(ipaddr_t));

	if (debug) {
		printf("%s: %s: replying IP address %s for host %s\n",
			prog_name, ethdev(ep->n),
			inet_ntoa(ip_addr),
			hostname);
	}
	(void) write(ep->eth_fd, &rarp46, sizeof(rarp46));
}

int main(int argc, char *argv[])
{
	int i;
	ethernet_t *ep;
	nwio_ethopt_t ethopt;
	nwio_ethstat_t ethstat;
	char hostname[1024];
	struct hostent *hostent;
	struct sigaction sa;
	int ip_fd;
	nwio_ipconf_t ipconf;
	asynchio_t asyn;
	ssize_t n;
	ipaddr_t ip_addr;
	rarp46_t rarp46;
	time_t then;
	int req_ct;
	int do_req;

	prog_name= argv[0];
	asyn_init(&asyn);

	if (argc == 2 && strcmp(argv[1], "-d") == 0) {
		debug= 1;
	} else {
		if (argc > 1) {
			fprintf(stderr, "Usage: %s [-d]\n", prog_name);
			exit(1);
		}
	}

	for (i= 0; i < N_ETHS; i++) {
		ep= &ethernets[i];
		ep->n= i;
		ep->state= SINK;
		if ((ep->eth_fd= open(ethdev(i), O_RDWR)) < 0) {
			if (errno == ENOENT) continue;
			fprintf(stderr, "%s: %s: %s\n", prog_name,
					ethdev(i), strerror(errno));
			exit(1);
		}

		if (ioctl(ep->eth_fd, NWIOGETHSTAT, &ethstat) < 0) {
			fprintf(stderr,
				"%s: %s: unable to get eth statistics: %s\n",
				prog_name, ethdev(i), strerror(errno));
			exit(1);
		}
		ep->eth_addr= ethstat.nwes_addr;
		if (debug) {
			printf("%s: %s: ethernet address is %s\n",
				prog_name, ethdev(i),
				ether_ntoa(&ep->eth_addr));
		}

		if (memcmp(&ep->eth_addr, &NULL_ETH_ADDR,
						sizeof(ep->eth_addr)) == 0) {
			/* This net is in sink mode. */
			close(ep->eth_fd);
			ep->eth_fd= -1;
			continue;
		}

		ethopt.nweo_flags= NWEO_COPY | NWEO_EN_LOC | NWEO_EN_BROAD |
			NWEO_TYPESPEC;
		ethopt.nweo_type= HTONS(ETH_RARP_PROTO);

		if (ioctl(ep->eth_fd, NWIOSETHOPT, &ethopt) < 0) {
			fprintf(stderr,
				"%s: %s: unable to set eth options: %s\n",
				prog_name, ethdev(i), strerror(errno));
			close(ep->eth_fd);
			ep->eth_fd= -1;
			continue;
		}

		/* Address is as yet unknown. */
		ep->state= UNKNOWN;

		/* Already configured by ifconfig? */
		if ((ip_fd= open(ipdev(i), O_RDWR|O_NONBLOCK)) < 0) {
			fprintf(stderr, "%s: %s: %s\n",
				prog_name, ipdev(i), strerror(errno));
			exit(1);
		}
		if (ioctl(ip_fd, NWIOGIPCONF, &ipconf) < 0) {
			if (errno != EAGAIN) {
				fprintf(stderr,
				"%s: %s: can't get IP address: %s\n",
					prog_name, ipdev(i),
					strerror(errno));
				exit(1);
			}
		} else {
			/* Address is already set. */
			ep->ip_addr= ipconf.nwic_ipaddr;
			ep->state= CONFIGURED;
			if (debug) {
				printf("%s: %s: address %s (ifconfig?)\n",
					prog_name, ipdev(i),
					inet_ntoa(ep->ip_addr));
			}
		}
		close(ip_fd);

		/* Try to find the ethernet address in the ethers file. */
		if (ep->state == UNKNOWN
			&& ether_ntohost(hostname, &ep->eth_addr) == 0
			&& (hostent= _gethostbyname(hostname)) != NULL
		) {
			memcpy(&ep->ip_addr, hostent->h_addr, sizeof(ipaddr_t));
			ep->state= KNOWN;
			if (debug) {
				printf("%s: %s: address %s (ethers file)\n",
					prog_name, ipdev(i),
					inet_ntoa(ep->ip_addr));
			}
		}
	}

	then= time(NULL);
	do_req= 1;
	req_ct= 0;
	while (req_ct <= MAX_RARP_RETRIES) {
		int any= 0;
		struct timeval tv;

		fflush(NULL);

		/* Read RARP replies. */
		for (i= 0; i < N_ETHS; i++) {
			ep= &ethernets[i];
			if (ep->state != UNKNOWN) continue;
			any= 1;

			n= asyn_read(&asyn, ep->eth_fd, ep->packet,
							sizeof(ep->packet));
			if (n != -1) break;
			if (errno != EINPROGRESS) {
				fprintf(stderr, "%s: %s: read error: %s\n",
					prog_name, ethdev(i), strerror(errno));
				exit(1);
			}
		}

		if (i < N_ETHS
			&& n >= sizeof(rarp46)
			&& (memcpy(&rarp46, ep->packet, sizeof(rarp46)), 1)
			&& rarp46.a46_hdr == HTONS(RARP_ETHERNET)
			&& rarp46.a46_pro == HTONS(ETH_IP_PROTO)
			&& rarp46.a46_hln == 6
			&& rarp46.a46_pln == 4
			&& rarp46.a46_op == HTONS(RARP_REPLY)
			&& memcmp(&rarp46.a46_tha, &ep->eth_addr,
						sizeof(ether_addr_t)) == 0
		) {
			/* It is a RARP reply! */
			memcpy(&ep->ip_addr, rarp46.a46_tpa, sizeof(ipaddr_t));
			ep->state= KNOWN;
			if (debug) {
				ipaddr_t server_ip;

				memcpy(&server_ip, rarp46.a46_spa,
							sizeof(ipaddr_t));
				printf("%s: %s: address %s",
					prog_name, ipdev(i),
					inet_ntoa(ep->ip_addr));
				printf(" (rarp reply from %s)\n",
					inet_ntoa(server_ip));
			}
		}

		/* Configure known addresses. */
		for (i= 0; i < N_ETHS; i++) {
			ep= &ethernets[i];
			if (ep->state != KNOWN) continue;

			if ((ip_fd= open(ipdev(i), O_RDWR)) < 0) {
				fprintf(stderr, "%s: %s: %s\n",
					prog_name, ipdev(i), strerror(errno));
				exit(1);
			}
			ipconf.nwic_flags= NWIC_IPADDR_SET;
			ipconf.nwic_ipaddr= ep->ip_addr;
			if (ioctl(ip_fd, NWIOSIPCONF, &ipconf) < 0) {
				if (errno != EINTR) {
					fprintf(stderr,
					"%s: %s: can't set IP address: %s\n",
						prog_name, ipdev(i),
						strerror(errno));
					exit(1);
				}
			}
			close(ip_fd);
			ep->state= CONFIGURED;
		}

		/* Are there still unknown addresses? */
		if (!any) break;

		/* Send out RARP requests. */
		if (do_req) {
			for (i= 0; i < N_ETHS; i++) {
				ep= &ethernets[i];
				if (ep->state != UNKNOWN) continue;

				rarp_request(ep);
			}
			do_req= 0;
		}

		/* Wait for replies. */
		tv.tv_sec= then + (RARP_TIMEOUT * (req_ct + 1));
		tv.tv_usec= 0;
		if (asyn_wait(&asyn, 0, &tv) < 0) {
			if (errno == EINTR) {
				if (++req_ct < MAX_RARP_RETRIES) do_req= 1;
			} else {
				fprintf(stderr, "%s: asyn_wait(): %s\n",
					prog_name, strerror(errno));
			}
		}
	}

	sa.sa_handler= onsig;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags= 0;
	sigaction(SIGALRM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);

	/* Get the IP addresses and netmasks of all configured networks.  The
	 * addresses may be different then we have set them to!
	 */
	for (i= 0; i < N_ETHS; i++) {
		ep= &ethernets[i];
		if (ep->state == SINK) continue;

		if ((ip_fd= open(ipdev(i), O_RDWR)) < 0) {
			fprintf(stderr, "%s: %s: %s\n",
				prog_name, ipdev(i), strerror(errno));
			exit(1);
		}
		alarm(1);
		if (ioctl(ip_fd, NWIOGIPCONF, &ipconf) < 0) {
			if (errno != EINTR) {
				fprintf(stderr,
				"%s: %s: can't get IP configuration: %s\n",
					prog_name, ipdev(i), strerror(errno));
				exit(1);
			}
			/* Forget this net. */
			if (debug) {
				printf("%s: %s: not configured\n",
					prog_name, ipdev(i));
			}
			close(ep->eth_fd);
			ep->eth_fd= -1;
			ep->state= SINK;
		} else {
			/* Remember IP address and netmask. */
			ep->ip_addr= ipconf.nwic_ipaddr;
			ep->ip_mask= ipconf.nwic_netmask;
			ep->state= CONFIGURED;
			if (debug) {
				printf("%s: %s: address %s, ",
					prog_name, ipdev(i),
					inet_ntoa(ep->ip_addr));
				printf("netmask %s\n",
					inet_ntoa(ep->ip_mask));
			}
		}
		close(ip_fd);
	}
	alarm(0);

	/* There must be something to do. */
	for (i= 0; i < N_ETHS && ethernets[i].state != CONFIGURED; i++) {}
	if (i == N_ETHS) {
		if (debug) printf("%s: no active ethernets\n", prog_name);
		exit(0);
	}
	if (access(PATH_ETHERS, F_OK) < 0) {
		if (errno != ENOENT) {
			fprintf(stderr, "%s: %s: %s\n", prog_name,
				PATH_ETHERS, strerror(errno));
			exit(1);
		}
		if (debug) printf("%s: no ethers file\n", prog_name);
		exit(0);
	}

	/* Wait for RARP requests, reply, repeat. */
	for(;;) {
		fflush(NULL);

		/* Wait for a RARP request. */
		for (i= 0; i < N_ETHS; i++) {
			ep= &ethernets[i];
			if (ep->state != CONFIGURED) continue;

			n= asyn_read(&asyn, ep->eth_fd, ep->packet,
							sizeof(ep->packet));
			if (n != -1) break;
			if (errno != EINPROGRESS) {
				fprintf(stderr, "%s: %s: read error: %s\n",
					prog_name, ethdev(i), strerror(errno));
				exit(1);
			}
		}

		/* RARP request? */
		if (i < N_ETHS
			&& n >= sizeof(rarp46)
			&& (memcpy(&rarp46, ep->packet, sizeof(rarp46)), 1)
			&& rarp46.a46_hdr == HTONS(RARP_ETHERNET)
			&& rarp46.a46_pro == HTONS(ETH_IP_PROTO)
			&& rarp46.a46_hln == 6
			&& rarp46.a46_pln == 4
			&& rarp46.a46_op == HTONS(RARP_REQUEST)
			&& ether_ntohost(hostname, &rarp46.a46_tha) == 0
			&& (hostent= gethostbyname(hostname)) != NULL
			&& hostent->h_addrtype == AF_INET
		) {
			memcpy(&ip_addr, hostent->h_addr, sizeof(ipaddr_t));

			/* Check if the host is on this network. */
			if ((ip_addr & ep->ip_mask)
					!= (ep->ip_addr & ep->ip_mask)) {
				if (debug) {
					printf(
			"%s: %s: wrong net for host '%s' (address %s)\n",
						prog_name, ethdev(i),
						hostname, inet_ntoa(ip_addr));
				}
			} else {
				rarp_reply(ep, hostname, ip_addr,
							rarp46.a46_tha);
			}
		}

		/* Wait for another request. */
		if (asyn_wait(&asyn, 0, NULL) < 0) {
			fprintf(stderr, "%s: asyn_wait(): %s\n",
				prog_name, strerror(errno));
		}
	}
	exit(0);
}
