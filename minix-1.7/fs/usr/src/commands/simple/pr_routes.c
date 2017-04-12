/*
ucb/pr_routes.c
*/

#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <net/netlib.h>
#include <net/gen/in.h>
#include <net/gen/route.h>
#include <net/gen/netdb.h>
#include <net/gen/inet.h>

char *prog_name;

int main _ARGS(( int argc, char *argv[] ));
void print_route _ARGS(( nwio_route_t *route ));
void usage _ARGS(( void ));

int main (argc, argv)
int argc;
char *argv[];
{
	int nr_routes, i;
	nwio_route_t route;
	int argind;
	char *ip_dev;
	int ip_fd;
	int result;

	prog_name= argv[0];
	ip_dev= NULL;
	for (argind= 1; argind < argc; argind++)
	{
		if (!strcmp(argv[argind], "-?"))
			usage();
		if (!strcmp(argv[argind], "-i"))
		{
			if (ip_dev)
				usage();
			argind++;
			if (argind >= argc)
				usage();
			ip_dev= argv[argind];
			continue;
		}
		usage();
	}
	if (!ip_dev)
	{
		ip_dev= getenv("IP_DEVICE");
	}
	if (!ip_dev)
		ip_dev= IP_DEVICE;
		
	ip_fd= open(ip_dev, O_RDWR);
	if (ip_fd == -1)
	{
		fprintf(stderr, "%s: unable to open %s: %s\n", prog_name,
			ip_dev, strerror(errno));
		exit(1);
	}

	route.nwr_ent_no= 0;
	result= ioctl(ip_fd, NWIOIPGROUTE, &route);
	if (result == -1)
	{
		fprintf(stderr, "%s: unable to NWIOIPGROUTE: %s\n",
			argv[0], strerror(errno));
		exit(1);
	}
	nr_routes= route.nwr_ent_count;
	print_route(&route);
	for (i= 1; i<nr_routes; i++)
	{
		route.nwr_ent_no= i;
		result= ioctl(ip_fd, NWIOIPGROUTE, &route);
		if (result == -1)
		{
			fprintf(stderr, "%s: unable to NWIOIPGROUTE: %s\n",
				argv[0], strerror(errno));
			exit(1);
		}
		print_route(&route);
	}
	exit(0);
}

void print_route(route)
nwio_route_t *route;
{
	if (!(route->nwr_flags & NWRF_INUSE))
		return;

	printf("%d ", route->nwr_ent_no);
	printf("DEST= %s, ", inet_ntoa(route->nwr_dest));
	printf("NETMASK= %s, ", inet_ntoa(route->nwr_netmask));
	printf("GATEWAY= %s, ", inet_ntoa(route->nwr_gateway));
	printf("dist= %d ", route->nwr_dist);
	printf("pref= %d", route->nwr_pref);
	if (route->nwr_flags & NWRF_FIXED)
		printf(" fixed");
	printf("\n");
}

void usage()
{
	fprintf(stderr, "USAGE: %s [ -i <ip-device> ]\n", prog_name);
	exit(1);
}

