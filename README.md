# Minix 2 QD edition

## Introduction

This is a very quick-and-dirty distribution of the old Minix 2 operating
system by Andrew Tanenbaum. It's not official in any way.

### What is Minix 2?

Minix 2 is a pedagogical operating system designed for teaching an OS
course, developed around 1996. Despite being restricted in scope and
suitable for understanding by students, it's actually a surprisingly
powerful and thoroughly usable Unix clone which will run in tiny amounts of
RAM. The entire distribution, source code and C compiler included, will fit
in about 40MB of disk and it'll recompile its own kernel and userspace in
1MB of RAM. (Slowly.) It'll run entirely off floppy disk. It'll even boot on
an original IBM PC, with the 512MB upgrade, although even Minix has trouble
there.

What you get is: multiuser logins, all the standard Unix tools, a full
Bourne shell, an ANSI C89 C compiler, a Modula-2 compiler, TCP/IP
networking, a vi clone, an emacs clone, a pico clone, source for nearly
everything, awk, bc, a game, and huge amounts of other stuff. It's (almost)
completely self-hosted --- you can hack Minix on Minix.

This makes it both interesting simply as a work of art, but also quite
useful when retrocomputing.

## I don't care about instructions, I just want something I can run!

Go to the [release page](https://github.com/davidgiven/minix2/releases/latest);
download one of the `img.gz` files; decompress it and write it to the
appropriate media; then boot it in something.

## What's here?

What you will find here is the contents of the last Minix 2 release, 2.0.4,
extracted and massaged so that it'll live in a standard user directory, plus
a set of scripts that will take the distribution and turn it into a bootable
Minix hard drive image. Minix is self-hosting, so we need to bootstrap a
Minix somehow to build it on.

You won't find *all* the source here, because Minix came with most source,
but not all; the C compiler is ACKPACK, a special version of the [Amsterdam
Compiler Kit](http://tack.sf.net), carefully cut down and modified to run on
Minix. Back when this was released, this wasn't open source.

Note: this is Minix i86. The largest binaries can get is 64kB+64kB. This is
restrictive compared to Minix i386, but it'll also build and run on machines
with as little as 1MB of RAM, which is the main case I'm interested in.

## Instructions

You will need a Linux machine and `qemu-system-i386` and `rsync`.

### Building and booting

Do:

    $ ./mkall

This will rebuild all the installation images from the files in `fs`.
(Alternatively, you can download these from the
[release page](https://github.com/davidgiven/minix2/releases/latest).)

  * `combo-1440kB.img.gz`, `combo-1200kB.img.gz`: combined root/usr floppy
    disk images. These contain a single filesystem with no division into root
    and usr. You can boot these. By default, the disk image will be loaded
    into a ramdisk to free up the floppy disk drive, but if you have less than
    about 3MB of RAM you should mount them directly; at the boot monitor prompt.
    hit ESC, then type `rootdev=fd0`, then `boot`.

  * `hd-64MB.img.gz`: is a bootable hard drive image containing absolutely
    everything; compilers, games, all the source code, the lot. Don't be put
    off by the size. Use this if you want to run Minix in a virtual machine.

  * `root-720kB.img.gz`, `usr-720kB.img.gz`: split root and usr floppy disk
    images. A 720kB floppy isn't big enough for the combined disk, so you need
    to boot the root disk, let it load itself into a ramdisk, and then it'll
    prompt you to replace the floppy with the usr image. It'll ask for a device;
    you want `/dev/fd0`.

### Installing onto a hard drive

Once you have a floppy based system booted, you want to copy Minix onto your
computer's hard drive.

The detailed instructions are long and complicated, but the tl;dr version is
to log in as root, and then:

    $ part

...is the partition editor; the Minix partition type is 81. If you have less
than 4MB RAM, create a swap partition (of about 4MB). You'll probably want to
reboot (Minix sometimes forgets to reload the partition table). I'm going to
assume you have the swap in `/dev/c0d0p0` and your filesystem in
`/dev/c0d0p1`. Don't make your filesystem too big; 64MB is probably as large
as you want.

To mount the swap:

    # mkswap /dev/c0d0p0
    # mount -s /dev/c0d0p0

To create the filesystem and copy all the data:

    # mkfs /dev/c0d0p1
    # mount /dev/c0d0p1 /mnt
    # cpdir -vx / /mnt/
    # cpdir -vx /usr/ /mnt/usr/   # only if you're using the usr-720kB floppy
    # echo "root=c0d0p1" > /mnt/etc/fstab
    # echo "swap=c0d0p0" >> /mnt/etc/fstab   # vi is also available
    # umount /dev/c0d0p1   # note that you use a device name here, not a path

To make the hard drive bootable:

    # installboot -m /dev/c0d0 /usr/mdec/masterboot
    # installboot -d /dev/c0d0p1 /usr/mdec/bootblock boot
    # edparams /dev/c0d0p1
    c0d0p1> rootdev=c0d0p1
    c0d0p1> save
    c0d0p1> exit
    # shutdown -h now

...then reboot. Hopefully your new system will boot.

## Hacking it

If you just want to fiddle with it, or do builds, then this command:

    $ ./pack

...will create an `hd.img` file, similar to the `hd-64MB.img.gz` one above,
It's also lightly massaged for use as a build box, so when it boots it'll
take you straight to a shell, etc. This then allows you to then do:

    $ ./runqemu

That will run QEMU and you should see Minix boot. It'll take about a second.

(TODO: There will eventually be a script to extract the contents of the file
system again, but that's not done yet.)

## References

  * [The 'official' Minix 2 distribution
    page](https://minix1.woodhull.com/) (it's been
    abandoned for years).

  * [The Design and Implementation of Operating Systems (second
    edition)](https://www.amazon.co.uk/Operating-Systems-Implementation-Tanenbaum-1997-01-15/dp/B019NDOVWC/ref=sr_1_1)
    is the book which describes Minix 2. Despite being out of print, it's
    still available from Amazon for terrifying prices.

  * [Minix 3](http://www.minix3.org/) is the vastly updated and quite
    usable successor to Minix 2... but it won't boot on XT computers any
    more.

## License

Minix 2, and all my scripts, are BSD 3-clause. See the
[LICENSE](LICENSE.md).
