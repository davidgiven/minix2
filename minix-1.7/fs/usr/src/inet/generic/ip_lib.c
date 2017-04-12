/*
ip_lib.c
*/

#include "inet.h"
#include "buf.h"
#include "type.h"

#include "assert.h"
#include "io.h"
#include "ip_int.h"

INIT_PANIC();

PUBLIC ipaddr_t ip_get_netmask (hostaddr)
ipaddr_t hostaddr;
{
	ipaddr_t host, mask;

#if DEBUG & 256
 { where(); printf("ip_lib.c: ip_get_netmask(hostaddr= ");
   writeIpAddr(hostaddr); printf(")\n"); }
#endif
	host= ntohl(hostaddr);
	if (!(host & 0x80000000L))
		mask= 0xff000000L;
	else if (!(host & 0x40000000L))
		mask= 0xffff0000L;
	else if (!(host & 0x20000000L))
		mask= 0xffffff00L;
	else
	{
#if DEBUG
 { where(); printf("ip.c: marsian address: "); writeIpAddr (hostaddr);
   printf("\n"); }
#endif
		mask= 0xffffffffL;
	}
	return htonl(mask);
}

PUBLIC int ip_chk_hdropt (opt, optlen)
u8_t *opt;
int optlen;
{
	int i, security_present= FALSE, lose_source_present= FALSE,
		strict_source_present= FALSE, record_route_present= FALSE,
		timestamp_present= FALSE;

#if DEBUG
 { where(); printf("ip_chk_hdropt(..., %d) called\n", optlen); }
#endif

assert (!(optlen & 3));
	i= 0;
	while (i<optlen)
	{
#if DEBUG
 { where(); printf("*opt= %d\n", *opt); }
#endif
		switch (*opt)
		{
		case 0x0:		/* End of Option list */
			return NW_OK;
		case 0x1:		/* No Operation */
			i++;
			opt++;
			break;
		case 0x82:		/* Security */
			if (security_present)
				return EINVAL;
			security_present= TRUE;
			if (opt[1] != 11)
				return EINVAL;
			i += opt[1];
			opt += opt[1];
			break;
		case 0x83:		/* Lose Source and Record Route */
			if (lose_source_present)
			{
#if DEBUG
 { where(); printf("snd lose soruce route\n"); }
#endif
				return EINVAL;
			}
			lose_source_present= TRUE;
			if (opt[1]<3)
			{
#if DEBUG
 { where(); printf("wrong length in source route\n"); }
#endif
				return EINVAL;
			}
			i += opt[1];
			opt += opt[1];
			break;
		case 0x89:		/* Strict Source and Record Route */
			if (strict_source_present)
				return EINVAL;
			strict_source_present= TRUE;
			if (opt[1]<3)
				return EINVAL;
			i += opt[1];
			opt += opt[1];
			break;
		case 0x7:		/* Record Route */
			if (record_route_present)
				return EINVAL;
			record_route_present= TRUE;
			if (opt[1]<3)
				return EINVAL;
			i += opt[1];
			opt += opt[1];
			break;
		case 0x88:
			if (timestamp_present)
				return EINVAL;
			timestamp_present= TRUE;
			if (opt[1] != 4)
				return EINVAL;
			switch (opt[3] & 0xff)
			{
			case 0:
			case 1:
			case 3:
				break;
			default:
				return EINVAL;
			}
			i += opt[1];
			opt += opt[1];
			break;
		default:
			return EINVAL;
		}
	}
	if (i > optlen)
	{
#if DEBUG
 { where(); printf("option of wrong length\n"); }
#endif
		return EINVAL;
	}
	return NW_OK;
}

void ip_print_frags(acc)
acc_t *acc;
{
	ip_hdr_t *ip_hdr;
	int first;

	if (!acc)
		printf("(null)");

	for (first= 1; acc; acc= acc->acc_ext_link, first= 0)
	{
assert (acc->acc_length >= IP_MIN_HDR_SIZE);
		ip_hdr= (ip_hdr_t *)ptr2acc_data(acc);
		if (first)
		{
			writeIpAddr(ip_hdr->ih_src);
			printf(" > ");
			writeIpAddr(ip_hdr->ih_dst);
		}
		printf(" {%x:%d@%d%c}", ip_hdr->ih_id,
			ntohs(ip_hdr->ih_length), 
			(ntohs(ip_hdr->ih_flags_fragoff) & IH_FRAGOFF_MASK)*8,
			(ntohs(ip_hdr->ih_flags_fragoff) & IH_MORE_FRAGS) ?
			'+' : '\0');
	}
}
