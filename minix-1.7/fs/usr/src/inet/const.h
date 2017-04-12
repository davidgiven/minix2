/*
inet/const.h

Created:	Dec 30, 1991 by Philip Homburg
*/

#ifndef INET__CONST_H
#define INET__CONST_H

#ifndef DEBUG
#define DEBUG	0
#endif

#ifndef NDEBUG
#define NDEBUG	(!DEBUG)
#endif

#define printf	printk

#define where()	printf("%s, %d: ", __FILE__, __LINE__)

#define NW_SUSPEND	SUSPEND
#define NW_OK		OK

#define THIS_PROC       INET_PROC_NR

#define BUF_S		512

#endif /* INET__CONST_H */
