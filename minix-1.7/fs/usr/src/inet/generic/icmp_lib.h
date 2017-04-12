/*
icmp_lib.h

Created Sept 30, 1991 by Philip Homburg
*/

#ifndef ICMP_LIB_H
#define ICMP_LIB_H

/* Prototypes */

void icmp_getnetmask ARGS((  int ip_port ));
void icmp_parmproblem ARGS(( acc_t *pack ));
void icmp_frag_ass_tim ARGS(( acc_t *pack ));
void icmp_dont_frag ARGS(( acc_t *pack ));
void icmp_ttl_exceded ARGS(( acc_t *pack ));

#endif /* ICMP_LIB_H */

