/*
ne2000.h

Created:	March 15, 1994 by Philip Homburg <philip@cs.vu.nl>
*/

#ifndef NE2000_H
#define NE2000_H

#define NE_DP8390	0x00
#define NE_DATA		0x10
#define NE_RESET	0x1F

#define NE1000_START	0x2000
#define NE1000_SIZE	0x2000
#define NE2000_START	0x4000
#define NE2000_SIZE	0x4000

#define inb_ne(dep, reg) (in_byte(dep->de_base_port+reg))
#define outb_ne(dep, reg, data) (out_byte(dep->de_base_port+reg, data))
#define inw_ne(dep, reg) (in_word(dep->de_base_port+reg))
#define outw_ne(dep, reg, data) (out_word(dep->de_base_port+reg, data))

#endif /* NE2000_H */

/*
 * $PchHeader: /mount/hd2/minix/sys/kernel/ibm/RCS/ne2000.h,v 1.1 1994/04/07 22:58:48 philip Exp $
 */
