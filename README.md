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

You will need a Linux machine and `qemu-system-i386`.

    $ ./pack

...will take the contents of the `fs` directory and turn it into a 64MB hard
drive image called `hd.img`. **Read the script first.** It's crudely hacked
together and does stuff as root. Because we can't store stuff like device
nodes and special permission information in a VCS, there's also a script
`specials.sh` which fixes these up. `pack` also converts symlinks into
hardlinks (Minix 2 doesn't support symlinks).

 The file system image is based on `hd.img.gz`, which is a 64MB empty
 filesystem containing only the boot loader, which can only be installed from
 inside Minix itself.

Once that finishes, do:

    $ ./runqemu

That will run QEMU and you should see Minix boot. It'll take about a second.

 (There will eventually be a script to extract the contents of the file
 system again, but that's not done yet.)

## References

   * [The 'official' (it's been abandoned for years) Minix 2 distribution
   page.](https://minix1.woodhull.com/)

   * [The Design and Implementation of Operating Systems (second
   edition)](https://www.amazon.co.uk/Operating-Systems-Implementation-Tanenbaum-1997-01-15/dp/B019NDOVWC/ref=sr_1_1)
   is the book which describes Minix 2. Despite being out of print, it's
   still available from Amazon for terrifying prices.

   * [Minix 3](http://www.minix3.org/) is the vastly updated and quite usable
   successor to Minix 2... but it won't boot on XT computers any more.

## License

 Minix 2, and all my scripts, are BSD 3-clause. See the
 [LICENSE](LICENSE.md).
