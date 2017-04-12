# Makefile for inet

# Directories
u = /usr
i = $u/include
s = $i/sys
h = $i/minix
n = $i/net
g = generic

# Programs, flags, etc.
CC =	exec cc
CFLAGS = -m -I. -I.. -I$i
LDFLAGS = -i

OBJ = 	buf.o clock.o mnx_eth.o mq.o inet.o sr.o \
	$g/arp.o $g/eth.o $g/icmp.o $g/io.o \
	$g/ip.o $g/ip_ioctl.o $g/ip_lib.o \
	$g/ip_read.o $g/ip_write.o $g/ipr.o \
	$g/tcp.o $g/tcp_lib.o $g/tcp_recv.o \
	$g/tcp_send.o $g/udp.o

inet:	inet.a putk.o
	$(CC) -o $@ $(LDFLAGS) inet.a putk.o
	install -S 10k $@

all install:	# Nothing

inet.a:	$(OBJ)
	@rm -f $@
	aal cr $@ $(OBJ)

clean:
	rm -f $(OBJ) inet.a putk.o inet *.bak .all

# Depencencies
.all:	const.h
.all:	$s/ioctl.h
.all:	$n/gen/udp_io.h
.all:	$n/gen/udp_hdr.h
.all:	$n/gen/udp.h
.all:	$n/gen/tcp_io.h
.all:	$n/gen/tcp_hdr.h
.all:	$n/gen/tcp.h
.all:	$n/gen/route.h
.all:	$n/gen/oneCsum.h
.all:	$n/gen/icmp_hdr.h
.all:	$n/gen/icmp.h
.all:	$n/gen/ip_io.h
.all:	$n/gen/ip_hdr.h
.all:	$n/gen/in.h
.all:	$n/gen/eth_io.h
.all:	$n/gen/eth_hdr.h
.all:	$n/gen/ether.h
.all:	$n/hton.h
.all:	$h/syslib.h
.all:	$h/const.h
.all:	$h/com.h
.all:	$h/type.h
.all:	$h/config.h
.all:	$i/string.h
.all:	$i/stdlib.h
.all:	$i/stddef.h
.all:	$i/errno.h
.all:	$i/ansi.h
.all:	$s/types.h
.all:	inet.h
	touch .all
a =	.all	# Trick to avoid many dependencies (that overload make)

buf.o:	$g/type.h
buf.o:	$g/buf.h
buf.o:	$g/assert.h
buf.o:	$i/string.h
buf.o:	$i/stdlib.h
buf.o:	$a

clock.o:	$g/type.h
clock.o:	$g/clock.h
clock.o:	$g/buf.h
clock.o:	$g/assert.h
clock.o:	proto.h
clock.o:	$a

mnx_eth.o:	$g/sr.h
mnx_eth.o:	$g/eth_int.h
mnx_eth.o:	$g/eth.h
mnx_eth.o:	$g/clock.h
mnx_eth.o:	osdep_eth.h
mnx_eth.o:	$g/buf.h
mnx_eth.o:	$g/assert.h
mnx_eth.o:	proto.h
mnx_eth.o:	$a

mq.o:	mq.h
mq.o:	$a

inet.o:	$i/unistd.h
inet.o:	$g/udp.h
inet.o:	$g/type.h
inet.o:	$g/tcp.h
inet.o:	$g/sr.h
inet.o:	$g/ip.h
inet.o:	$g/eth.h
inet.o:	$g/clock.h
inet.o:	$g/buf.h
inet.o:	$g/arp.h
inet.o:	$g/assert.h
inet.o:	proto.h
inet.o:	mq.h
inet.o:	$h/callnr.h
inet.o:	$i/limits.h
inet.o:	config.h
inet.o:	$a

putk.o:	$a

sr.o:	$g/type.h
sr.o:	$g/sr.h
sr.o:	$g/buf.h
sr.o:	$g/assert.h
sr.o:	proto.h
sr.o:	mq.h
sr.o:	$h/callnr.h
sr.o:	$a

$g/arp.o:	$g/type.h
$g/arp.o:	$g/sr.h
$g/arp.o:	$g/io.h
$g/arp.o:	$g/eth.h
$g/arp.o:	$g/clock.h
$g/arp.o:	$g/buf.h
$g/arp.o:	$g/assert.h
$g/arp.o:	$g/arp.h
$g/arp.o:	$a
$g/arp.o:	$g/arp.c
	cd generic; $(CC) -c $(CFLAGS) arp.c

$g/eth.o:	$g/type.h
$g/eth.o:	$g/sr.h
$g/eth.o:	$g/io.h
$g/eth.o:	$g/eth_int.h
$g/eth.o:	$g/eth.h
$g/eth.o:	$g/assert.h
$g/eth.o:	osdep_eth.h
$g/eth.o:	$g/clock.h
$g/eth.o:	$g/buf.h
$g/eth.o:	$a
$g/eth.o:	$g/eth.c
	cd generic; $(CC) -c $(CFLAGS) eth.c

$g/icmp.o:	$g/ipr.h
$g/icmp.o:	$g/ip_int.h
$g/icmp.o:	$g/ip.h
$g/icmp.o:	$g/io.h
$g/icmp.o:	$g/icmp_lib.h
$g/icmp.o:	$g/icmp.h
$g/icmp.o:	$g/clock.h
$g/icmp.o:	$g/assert.h
$g/icmp.o:	$g/type.h
$g/icmp.o:	$g/buf.h
$g/icmp.o:	$a
$g/icmp.o:	$g/icmp.c
	cd generic; $(CC) -c $(CFLAGS) icmp.c

$g/io.o:	$g/io.h
$g/io.o:	$a
$g/io.o:	$g/io.c
	cd generic; $(CC) -c $(CFLAGS) io.c

$g/ip.o:	$g/sr.h
$g/ip.o:	$g/ipr.h
$g/ip.o:	$g/ip_int.h
$g/ip.o:	$g/ip.h
$g/ip.o:	$g/io.h
$g/ip.o:	$g/icmp_lib.h
$g/ip.o:	$g/icmp.h
$g/ip.o:	$g/eth.h
$g/ip.o:	$g/clock.h
$g/ip.o:	$g/assert.h
$g/ip.o:	$g/arp.h
$g/ip.o:	$g/type.h
$g/ip.o:	$g/buf.h
$g/ip.o:	$a
$g/ip.o:	$g/ip.c
	cd generic; $(CC) -c $(CFLAGS) ip.c

$g/ip_ioctl.o:	$g/ipr.h
$g/ip_ioctl.o:	$g/ip_int.h
$g/ip_ioctl.o:	$g/ip.h
$g/ip_ioctl.o:	$g/icmp_lib.h
$g/ip_ioctl.o:	$g/clock.h
$g/ip_ioctl.o:	$g/assert.h
$g/ip_ioctl.o:	$g/arp.h
$g/ip_ioctl.o:	$g/type.h
$g/ip_ioctl.o:	$g/buf.h
$g/ip_ioctl.o:	$a
$g/ip_ioctl.o:	$g/ip_ioctl.c
	cd generic; $(CC) -c $(CFLAGS) ip_ioctl.c

$g/ip_lib.o:	$g/ip_int.h
$g/ip_lib.o:	$g/io.h
$g/ip_lib.o:	$g/assert.h
$g/ip_lib.o:	$g/type.h
$g/ip_lib.o:	$g/buf.h
$g/ip_lib.o:	$a
$g/ip_lib.o:	$g/ip_lib.c
	cd generic; $(CC) -c $(CFLAGS) ip_lib.c

$g/ip_read.o:	$g/ip_int.h
$g/ip_read.o:	$g/ip.h
$g/ip_read.o:	$g/io.h
$g/ip_read.o:	$g/icmp_lib.h
$g/ip_read.o:	$g/assert.h
$g/ip_read.o:	$g/type.h
$g/ip_read.o:	$g/clock.h
$g/ip_read.o:	$g/buf.h
$g/ip_read.o:	$a
$g/ip_read.o:	$g/ip_read.c
	cd generic; $(CC) -c $(CFLAGS) ip_read.c

$g/ip_write.o:	$g/ipr.h
$g/ip_write.o:	$g/ip_int.h
$g/ip_write.o:	$g/ip.h
$g/ip_write.o:	$g/io.h
$g/ip_write.o:	$g/icmp_lib.h
$g/ip_write.o:	$g/eth.h
$g/ip_write.o:	$g/clock.h
$g/ip_write.o:	$g/assert.h
$g/ip_write.o:	$g/arp.h
$g/ip_write.o:	$g/type.h
$g/ip_write.o:	$g/buf.h
$g/ip_write.o:	$a
$g/ip_write.o:	$g/ip_write.c
	cd generic; $(CC) -c $(CFLAGS) ip_write.c

$g/ipr.o:	$g/ipr.h
$g/ipr.o:	$g/io.h
$g/ipr.o:	$g/assert.h
$g/ipr.o:	$g/clock.h
$g/ipr.o:	$a
$g/ipr.o:	$g/ipr.c
	cd generic; $(CC) -c $(CFLAGS) ipr.c

$g/tcp.o:	$g/tcp_int.h
$g/tcp.o:	$g/tcp.h
$g/tcp.o:	$g/assert.h
$g/tcp.o:	$g/sr.h
$g/tcp.o:	$g/ip.h
$g/tcp.o:	$g/io.h
$g/tcp.o:	$g/type.h
$g/tcp.o:	$g/clock.h
$g/tcp.o:	$g/buf.h
$g/tcp.o:	$a
$g/tcp.o:	$g/tcp.c
	cd generic; $(CC) -c $(CFLAGS) tcp.c

$g/tcp_lib.o:	$g/tcp_int.h
$g/tcp_lib.o:	$g/assert.h
$g/tcp_lib.o:	$g/type.h
$g/tcp_lib.o:	$g/io.h
$g/tcp_lib.o:	$g/clock.h
$g/tcp_lib.o:	$g/buf.h
$g/tcp_lib.o:	$a
$g/tcp_lib.o:	$g/tcp_lib.c
	cd generic; $(CC) -c $(CFLAGS) tcp_lib.c

$g/tcp_recv.o:	$g/assert.h
$g/tcp_recv.o:	$g/tcp.h
$g/tcp_recv.o:	$g/tcp_int.h
$g/tcp_recv.o:	$g/type.h
$g/tcp_recv.o:	$g/clock.h
$g/tcp_recv.o:	$g/buf.h
$g/tcp_recv.o:	$a
$g/tcp_recv.o:	$g/tcp_recv.c
	cd generic; $(CC) -c $(CFLAGS) tcp_recv.c

$g/tcp_send.o:	$g/tcp_int.h
$g/tcp_send.o:	$g/tcp.h
$g/tcp_send.o:	$g/clock.h
$g/tcp_send.o:	$g/assert.h
$g/tcp_send.o:	$g/type.h
$g/tcp_send.o:	$g/buf.h
$g/tcp_send.o:	$g/ip.h
$g/tcp_send.o:	$a
$g/tcp_send.o:	$g/tcp_send.c
	cd generic; $(CC) -c $(CFLAGS) tcp_send.c

$g/udp.o:	$g/assert.h
$g/udp.o:	$g/buf.h
$g/udp.o:	$g/clock.h
$g/udp.o:	$g/io.h
$g/udp.o:	$g/ip.h
$g/udp.o:	$g/sr.h
$g/udp.o:	$g/type.h
$g/udp.o:	$g/udp.h
$g/udp.o:	$a
$g/udp.o:	$g/udp.c
	cd generic; $(CC) -c $(CFLAGS) udp.c
