mkdir dev fd0 fd1 mnt root tmp
mkdir -p usr/tmp
mkdir -p usr/preserve
mkdir -p usr/spool/lpd
mkdir -p usr/spool/at/past
mkdir -p usr/run
mknod dev/ram b 0x1 0x0
mknod dev/fd0 b 0x2 0x0
mknod dev/fd1 b 0x2 0x1
mknod dev/fd0p0 b 0x2 0x70
mknod dev/fd0p1 b 0x2 0x74
mknod dev/fd0p2 b 0x2 0x78
mknod dev/fd0p3 b 0x2 0x7c
mknod dev/fd1p0 b 0x2 0x71
mknod dev/fd1p1 b 0x2 0x75
mknod dev/fd1p2 b 0x2 0x79
mknod dev/fd1p3 b 0x2 0x7d
mknod dev/c0d0 b 0x3 0x0
mknod dev/c0d0p0 b 0x3 0x1
mknod dev/c0d0p1 b 0x3 0x2
mknod dev/c0d0p2 b 0x3 0x3
mknod dev/c0d0p3 b 0x3 0x4
mknod dev/c0d0p0s0 b 0x3 0x80
mknod dev/c0d0p0s1 b 0x3 0x81
mknod dev/c0d0p0s2 b 0x3 0x82
mknod dev/c0d0p0s3 b 0x3 0x83
mknod dev/c0d0p1s0 b 0x3 0x84
mknod dev/c0d0p1s1 b 0x3 0x85
mknod dev/c0d0p1s2 b 0x3 0x86
mknod dev/c0d0p1s3 b 0x3 0x87
mknod dev/c0d0p2s0 b 0x3 0x88
mknod dev/c0d0p2s1 b 0x3 0x89
mknod dev/c0d0p2s2 b 0x3 0x8a
mknod dev/c0d0p2s3 b 0x3 0x8b
mknod dev/c0d0p3s0 b 0x3 0x8c
mknod dev/c0d0p3s1 b 0x3 0x8d
mknod dev/c0d0p3s2 b 0x3 0x8e
mknod dev/c0d0p3s3 b 0x3 0x8f
mknod dev/c0d1 b 0x3 0x5
mknod dev/c0d1p0 b 0x3 0x6
mknod dev/c0d1p1 b 0x3 0x7
mknod dev/c0d1p2 b 0x3 0x8
mknod dev/c0d1p3 b 0x3 0x9
mknod dev/c0d1p0s0 b 0x3 0x90
mknod dev/c0d1p0s1 b 0x3 0x91
mknod dev/c0d1p0s2 b 0x3 0x92
mknod dev/c0d1p0s3 b 0x3 0x93
mknod dev/c0d1p1s0 b 0x3 0x94
mknod dev/c0d1p1s1 b 0x3 0x95
mknod dev/c0d1p1s2 b 0x3 0x96
mknod dev/c0d1p1s3 b 0x3 0x97
mknod dev/c0d1p2s0 b 0x3 0x98
mknod dev/c0d1p2s1 b 0x3 0x99
mknod dev/c0d1p2s2 b 0x3 0x9a
mknod dev/c0d1p2s3 b 0x3 0x9b
mknod dev/c0d1p3s0 b 0x3 0x9c
mknod dev/c0d1p3s1 b 0x3 0x9d
mknod dev/c0d1p3s2 b 0x3 0x9e
mknod dev/c0d1p3s3 b 0x3 0x9f
mknod dev/c0d2 b 0x3 0xa
mknod dev/c0d2p0 b 0x3 0xb
mknod dev/c0d2p0s0 b 0x3 0xa0
mknod dev/c0d2p0s1 b 0x3 0xa1
mknod dev/c0d2p0s2 b 0x3 0xa2
mknod dev/c0d2p0s3 b 0x3 0xa3
mknod dev/c0d2p1 b 0x3 0xc
mknod dev/c0d2p1s0 b 0x3 0xa4
mknod dev/c0d2p1s1 b 0x3 0xa5
mknod dev/c0d2p1s2 b 0x3 0xa6
mknod dev/c0d2p1s3 b 0x3 0xa7
mknod dev/c0d2p2 b 0x3 0xd
mknod dev/c0d2p2s0 b 0x3 0xa8
mknod dev/c0d2p2s1 b 0x3 0xa9
mknod dev/c0d2p2s2 b 0x3 0xaa
mknod dev/c0d2p2s3 b 0x3 0xab
mknod dev/c0d2p3 b 0x3 0xe
mknod dev/c0d2p3s0 b 0x3 0xac
mknod dev/c0d2p3s1 b 0x3 0xad
mknod dev/c0d2p3s2 b 0x3 0xae
mknod dev/c0d2p3s3 b 0x3 0xaf
mknod dev/c0d3 b 0x3 0xf
mknod dev/c0d3p0 b 0x3 0x10
mknod dev/c0d3p0s0 b 0x3 0xb0
mknod dev/c0d3p0s1 b 0x3 0xb1
mknod dev/c0d3p0s2 b 0x3 0xb2
mknod dev/c0d3p0s3 b 0x3 0xb3
mknod dev/c0d3p1 b 0x3 0x11
mknod dev/c0d3p1s0 b 0x3 0xb4
mknod dev/c0d3p1s1 b 0x3 0xb5
mknod dev/c0d3p1s2 b 0x3 0xb6
mknod dev/c0d3p1s3 b 0x3 0xb7
mknod dev/c0d3p2 b 0x3 0x12
mknod dev/c0d3p2s0 b 0x3 0xb8
mknod dev/c0d3p2s1 b 0x3 0xb9
mknod dev/c0d3p2s2 b 0x3 0xba
mknod dev/c0d3p2s3 b 0x3 0xbb
mknod dev/c0d3p3 b 0x3 0x13
mknod dev/c0d3p3s0 b 0x3 0xbc
mknod dev/c0d3p3s1 b 0x3 0xbd
mknod dev/c0d3p3s2 b 0x3 0xbe
mknod dev/c0d3p3s3 b 0x3 0xbf
mknod dev/mem c 0x1 0x1
mknod dev/kmem c 0x1 0x2
mknod dev/null c 0x1 0x3
mknod dev/console c 0x4 0x0
mknod dev/tty c 0x5 0x0
mknod dev/lp c 0x6 0x0
mknod dev/log c 0x4 0xf
mknod dev/ttyc1 c 0x4 0x1
mknod dev/ttyc2 c 0x4 0x2
mknod dev/ttyc3 c 0x4 0x3
mknod dev/tty00 c 0x4 0x10
mknod dev/tty01 c 0x4 0x11
mknod dev/ttyp0 c 0x4 0x80
mknod dev/ptyp0 c 0x4 0xc0
mknod dev/ttyp1 c 0x4 0x81
mknod dev/ptyp1 c 0x4 0xc1
mknod dev/ttyp2 c 0x4 0x82
mknod dev/ptyp2 c 0x4 0xc2
mknod dev/ttyp3 c 0x4 0x83
mknod dev/ptyp3 c 0x4 0xc3
mknod dev/eth0 c 0x7 0x0
mknod dev/ip0 c 0x7 0x1
mknod dev/tcp0 c 0x7 0x2
mknod dev/udp0 c 0x7 0x3
mknod dev/eth c 0x7 0x0
mknod dev/ip c 0x7 0x1
mknod dev/tcp c 0x7 0x2
mknod dev/udp c 0x7 0x3
chmod 755 dev
chmod 600 dev/ram
chmod 640 dev/mem
chmod 640 dev/kmem
chmod 666 dev/null
chmod 666 dev/fd0
chmod 666 dev/fd1
chmod 666 dev/fd0p0
chmod 666 dev/fd0p1
chmod 666 dev/fd0p2
chmod 666 dev/fd0p3
chmod 666 dev/fd1p0
chmod 666 dev/fd1p1
chmod 666 dev/fd1p2
chmod 666 dev/fd1p3
chmod 600 dev/c0d0
chmod 600 dev/c0d0p0
chmod 600 dev/c0d0p1
chmod 600 dev/c0d0p2
chmod 600 dev/c0d0p3
chmod 600 dev/c0d0p0s0
chmod 600 dev/c0d0p0s1
chmod 600 dev/c0d0p0s2
chmod 600 dev/c0d0p0s3
chmod 600 dev/c0d0p1s0
chmod 600 dev/c0d0p1s1
chmod 600 dev/c0d0p1s2
chmod 600 dev/c0d0p1s3
chmod 600 dev/c0d0p2s0
chmod 600 dev/c0d0p2s1
chmod 600 dev/c0d0p2s2
chmod 600 dev/c0d0p2s3
chmod 600 dev/c0d0p3s0
chmod 600 dev/c0d0p3s1
chmod 600 dev/c0d0p3s2
chmod 600 dev/c0d0p3s3
chmod 600 dev/c0d1
chmod 600 dev/c0d1p0
chmod 600 dev/c0d1p1
chmod 600 dev/c0d1p2
chmod 600 dev/c0d1p3
chmod 600 dev/c0d1p0s0
chmod 600 dev/c0d1p0s1
chmod 600 dev/c0d1p0s2
chmod 600 dev/c0d1p0s3
chmod 600 dev/c0d1p1s0
chmod 600 dev/c0d1p1s1
chmod 600 dev/c0d1p1s2
chmod 600 dev/c0d1p1s3
chmod 600 dev/c0d1p2s0
chmod 600 dev/c0d1p2s1
chmod 600 dev/c0d1p2s2
chmod 600 dev/c0d1p2s3
chmod 600 dev/c0d1p3s0
chmod 600 dev/c0d1p3s1
chmod 600 dev/c0d1p3s2
chmod 600 dev/c0d1p3s3
chmod 600 dev/c0d2
chmod 600 dev/c0d2p0
chmod 600 dev/c0d2p0s0
chmod 600 dev/c0d2p0s1
chmod 600 dev/c0d2p0s2
chmod 600 dev/c0d2p0s3
chmod 600 dev/c0d2p1
chmod 600 dev/c0d2p1s0
chmod 600 dev/c0d2p1s1
chmod 600 dev/c0d2p1s2
chmod 600 dev/c0d2p1s3
chmod 600 dev/c0d2p2
chmod 600 dev/c0d2p2s0
chmod 600 dev/c0d2p2s1
chmod 600 dev/c0d2p2s2
chmod 600 dev/c0d2p2s3
chmod 600 dev/c0d2p3
chmod 600 dev/c0d2p3s0
chmod 600 dev/c0d2p3s1
chmod 600 dev/c0d2p3s2
chmod 600 dev/c0d2p3s3
chmod 600 dev/c0d3
chmod 600 dev/c0d3p0
chmod 600 dev/c0d3p0s0
chmod 600 dev/c0d3p0s1
chmod 600 dev/c0d3p0s2
chmod 600 dev/c0d3p0s3
chmod 600 dev/c0d3p1
chmod 600 dev/c0d3p1s0
chmod 600 dev/c0d3p1s1
chmod 600 dev/c0d3p1s2
chmod 600 dev/c0d3p1s3
chmod 600 dev/c0d3p2
chmod 600 dev/c0d3p2s0
chmod 600 dev/c0d3p2s1
chmod 600 dev/c0d3p2s2
chmod 600 dev/c0d3p2s3
chmod 600 dev/c0d3p3
chmod 600 dev/c0d3p3s0
chmod 600 dev/c0d3p3s1
chmod 600 dev/c0d3p3s2
chmod 600 dev/c0d3p3s3
chmod 600 dev/console
chmod 666 dev/tty
chmod 200 dev/lp
chmod 222 dev/log
chmod 600 dev/ttyc1
chmod 600 dev/ttyc2
chmod 600 dev/ttyc3
chmod 666 dev/tty00
chmod 666 dev/tty01
chmod 620 dev/ttyp0
chmod 666 dev/ptyp0
chmod 620 dev/ttyp1
chmod 666 dev/ptyp1
chmod 666 dev/ttyp2
chmod 666 dev/ptyp2
chmod 666 dev/ttyp3
chmod 666 dev/ptyp3
chmod 600 dev/eth0
chmod 600 dev/ip0
chmod 666 dev/tcp0
chmod 666 dev/udp0
chmod 600 dev/eth
chmod 600 dev/ip
chmod 666 dev/tcp
chmod 666 dev/udp
chmod 4754 usr/bin/shutdown
chmod 4755 bin/mount
chmod 4755 bin/umount
chown 0:0 dev
chown 8:0 dev/ram
chown 8:0 dev/mem
chown 8:0 dev/kmem
chown 8:0 dev/null
chown 0:0 dev/fd0
chown 0:0 dev/fd1
chown 0:0 dev/fd0p0
chown 0:0 dev/fd0p1
chown 0:0 dev/fd0p2
chown 0:0 dev/fd0p3
chown 0:0 dev/fd1p0
chown 0:0 dev/fd1p1
chown 0:0 dev/fd1p2
chown 0:0 dev/fd1p3
chown 0:0 dev/c0d0
chown 0:0 dev/c0d0p0
chown 0:0 dev/c0d0p1
chown 0:0 dev/c0d0p2
chown 0:0 dev/c0d0p3
chown 0:0 dev/c0d0p0s0
chown 0:0 dev/c0d0p0s1
chown 0:0 dev/c0d0p0s2
chown 0:0 dev/c0d0p0s3
chown 0:0 dev/c0d0p1s0
chown 0:0 dev/c0d0p1s1
chown 0:0 dev/c0d0p1s2
chown 0:0 dev/c0d0p1s3
chown 0:0 dev/c0d0p2s0
chown 0:0 dev/c0d0p2s1
chown 0:0 dev/c0d0p2s2
chown 0:0 dev/c0d0p2s3
chown 0:0 dev/c0d0p3s0
chown 0:0 dev/c0d0p3s1
chown 0:0 dev/c0d0p3s2
chown 0:0 dev/c0d0p3s3
chown 0:0 dev/c0d1
chown 0:0 dev/c0d1p0
chown 0:0 dev/c0d1p1
chown 0:0 dev/c0d1p2
chown 0:0 dev/c0d1p3
chown 0:0 dev/c0d1p0s0
chown 0:0 dev/c0d1p0s1
chown 0:0 dev/c0d1p0s2
chown 0:0 dev/c0d1p0s3
chown 0:0 dev/c0d1p1s0
chown 0:0 dev/c0d1p1s1
chown 0:0 dev/c0d1p1s2
chown 0:0 dev/c0d1p1s3
chown 0:0 dev/c0d1p2s0
chown 0:0 dev/c0d1p2s1
chown 0:0 dev/c0d1p2s2
chown 0:0 dev/c0d1p2s3
chown 0:0 dev/c0d1p3s0
chown 0:0 dev/c0d1p3s1
chown 0:0 dev/c0d1p3s2
chown 0:0 dev/c0d1p3s3
chown 0:0 dev/c0d2
chown 0:0 dev/c0d2p0
chown 0:0 dev/c0d2p0s0
chown 0:0 dev/c0d2p0s1
chown 0:0 dev/c0d2p0s2
chown 0:0 dev/c0d2p0s3
chown 0:0 dev/c0d2p1
chown 0:0 dev/c0d2p1s0
chown 0:0 dev/c0d2p1s1
chown 0:0 dev/c0d2p1s2
chown 0:0 dev/c0d2p1s3
chown 0:0 dev/c0d2p2
chown 0:0 dev/c0d2p2s0
chown 0:0 dev/c0d2p2s1
chown 0:0 dev/c0d2p2s2
chown 0:0 dev/c0d2p2s3
chown 0:0 dev/c0d2p3
chown 0:0 dev/c0d2p3s0
chown 0:0 dev/c0d2p3s1
chown 0:0 dev/c0d2p3s2
chown 0:0 dev/c0d2p3s3
chown 0:0 dev/c0d3
chown 0:0 dev/c0d3p0
chown 0:0 dev/c0d3p0s0
chown 0:0 dev/c0d3p0s1
chown 0:0 dev/c0d3p0s2
chown 0:0 dev/c0d3p0s3
chown 0:0 dev/c0d3p1
chown 0:0 dev/c0d3p1s0
chown 0:0 dev/c0d3p1s1
chown 0:0 dev/c0d3p1s2
chown 0:0 dev/c0d3p1s3
chown 0:0 dev/c0d3p2
chown 0:0 dev/c0d3p2s0
chown 0:0 dev/c0d3p2s1
chown 0:0 dev/c0d3p2s2
chown 0:0 dev/c0d3p2s3
chown 0:0 dev/c0d3p3
chown 0:0 dev/c0d3p3s0
chown 0:0 dev/c0d3p3s1
chown 0:0 dev/c0d3p3s2
chown 0:0 dev/c0d3p3s3
chown 0:0 dev/console
chown 0:0 dev/tty
chown 1:1 dev/lp
chown 0:0 dev/log
chown 0:0 dev/ttyc1
chown 4:0 dev/ttyc2
chown 4:0 dev/ttyc3
chown 4:0 dev/tty00
chown 4:0 dev/tty01
chown 4:2 dev/ttyp0
chown 4:0 dev/ptyp0
chown 4:2 dev/ttyp1
chown 4:0 dev/ptyp1
chown 4:0 dev/ttyp2
chown 4:0 dev/ptyp2
chown 4:0 dev/ttyp3
chown 4:0 dev/ptyp3
chown 0:0 dev/eth0
chown 0:0 dev/ip0
chown 0:0 dev/tcp0
chown 0:0 dev/udp0
chown 0:0 dev/eth
chown 0:0 dev/ip
chown 0:0 dev/tcp
chown 0:0 dev/udp
