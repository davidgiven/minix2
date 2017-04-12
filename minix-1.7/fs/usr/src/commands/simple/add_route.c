/*
add_route.c

Created August 7, 1991 by Philip Homburg
*/

#ifndef _POSIX2_SOURCE
#define _POSIX2_SOURCE	1
#endif

#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <net/hton.h>
#include <net/netlib.h>
#include <net/gen/netdb.h>
#include <net/gen/in.h>
#include <net/gen/inet.h>
#include <net/gen/route.h>
#include <net/gen/ip_io.h>

char *prog_name;

void main _ARGS(( int argc, char *argv[] ));
void usage _ARGS(( void ));

void main(argc, argv)
int argc;
char *argv[];
{
	int c;
	char *i_arg, *d_arg, *g_arg, *n_arg;
	struct hostent *hostent;
	ipaddr_t gateway, destination, netmask;
	u8_t high_byte;
	nwio_route_t route;
	int ip_fd;
	int result;

	prog_name= argv[0];

	i_arg= NULL;
	d_arg= NULL;
	g_arg= NULL;
	n_arg= NULL;

	while ((c= getopt(argc, argv, "?i:g:d:n:")) != -1)
	{
		switch(c)
		{
		case 'i':
			if (i_arg)
				usage();
			i_arg= optarg;
			break;
		case 'g':
			if (g_arg)
				usage();
			g_arg= optarg;
			break;
		case 'd':
			if (d_arg)
				usage();
			d_arg= optarg;
			break;
		case 'n':
			if (n_arg)
				usage();
			n_arg= optarg;
			break;
		case '?':
			usage();
		default:
			fprintf(stderr, "%s: getopt failed\n", prog_name);
			exit(1);
		}
	}
	if (optind != argc || !g_arg || (n_arg && !d_arg))
		usage();
	
	hostent= gethostbyname(g_arg);
	if (!hostent)
	{
		fprintf(stderr, "%s: unknown host '%s'\n", prog_name, g_arg);
		exit(1);
	}
	gateway= *(ipaddr_t *)(hostent->h_addr);

	destination= 0;
	netmask= 0;

	if (d_arg)
	{
		hostent= gethostbyname(d_arg);
		if (!hostent)
		{
			fprintf(stderr, "%s: unknown host '%s'\n", d_arg);
			exit(1);
		}
		destination= *(ipaddr_t *)(hostent->h_addr);
		high_byte= *(u8_t *)&destination;
		if (!(high_byte & 0x80))	/* class A or 0 */
		{
			if (destination)
				netmask= HTONL(0xff000000);
		}
		else if (!(high_byte & 0x40))	/* class B */
		{
			netmask= HTONL(0xffff0000);
		}
		else if (!(high_byte & 0x20))	/* class C */
		{
			netmask= HTONL(0xffffff00);
		}
		else				/* class D is multicast ... */
		{
			fprintf(stderr, "%s: warning marsian address '%s'\n",
				inet_ntoa(destination));
			netmask= HTONL(0xffffffff);
		}
	}

	if (n_arg)
	{
		hostent= gethostbyname(n_arg);
		if (!hostent)
		{
			fprintf(stderr, "%s: unknown host '%s'\n", n_arg);
			exit(1);
		}
		netmask= *(ipaddr_t *)(hostent->h_addr);
	}
		
	if (!i_arg)
		i_arg= getenv("IP_DEVICE");
	if (!i_arg)
		i_arg= IP_DEVICE;

	ip_fd= open(i_arg, O_RDWR);
	if (ip_fd == -1)
	{
		fprintf(stderr, "%s: unable to open('%s'): %s\n",
			prog_name, i_arg, strerror(errno));
		exit(1);
	}

	printf("adding route to %s ", inet_ntoa(destination));
	printf("with netmask %s ", inet_ntoa(netmask));
	printf("using gateway %s\n", inet_ntoa(gateway));

	route.nwr_dest= destination;
	route.nwr_netmask= netmask;
	route.nwr_gateway= gateway;
	route.nwr_dist= 1;
	route.nwr_flags= NWRF_FIXED;

	result= ioctl(ip_fd, NWIOIPSROUTE, &route);
	if (result == -1)
	{
		fprintf(stderr, "%s: NWIOIPSROUTE: %s\n",
			prog_name, strerror(errno));
		exit(1);
	}
	exit(0);
}

void usage()
{
	fprintf(stderr,
		"USAGE: %s -g <gateway> [-d <destination> [-n <netmask> ]]\n", 
			prog_name);
	fprintf(stderr, "\t\t\t\t[-i <ip device>]\n");
	exit(1);
}

