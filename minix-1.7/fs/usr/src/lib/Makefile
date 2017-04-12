# Makefile for the libraries

# This makefile runs make in all the subdirectories of the src/lib tree.
# See ansi/Makefile for a further explanation.

MAKE	= exec make -$(MAKEFLAGS)

usage:
	@echo "Usage: make all" >&2	# Compile all library functions" >&2
	@echo "       make install	# Backup /usr/lib/*.a first!" >&2
	@echo "       make clean	# Delete .o and .a files" >&2
	@false

all:
	cd ansi && $(MAKE)
	cd curses && $(MAKE)
	cd editline && $(MAKE)
	cd end && $(MAKE)
	cd float && $(MAKE)
	cd fphook && $(MAKE)
	cd ip && $(MAKE)
	cd liby && $(MAKE)
	cd math && $(MAKE)
	cd other && $(MAKE)
	cd posix && $(MAKE)
	cd stdio && $(MAKE)
	cd syscall && $(MAKE)
	cd syslib && $(MAKE)
	cd `arch` && $(MAKE)

install:	all
	$(MAKE) install_`arch`

# Installation rules for PC Minix.
LIB=	/usr/lib

install_i86:	\
	$(LIB)/ncrtso.o \
	$(LIB)/libc.a \
	$(LIB)/libd.a \
	$(LIB)/libe.a \
	$(LIB)/libfp.a \
	$(LIB)/end.a \
	$(LIB)/libcurses.a \
	$(LIB)/libedit.a \
	$(LIB)/liby.a \

$(LIB)/ncrtso.o:	ncrtso.o
	install -c -o bin $? $@

$(LIB)/libc.a:	libc.a
	install -c -o bin $? $@

$(LIB)/libd.a:	libd.a
	install -c -o bin $? $@

$(LIB)/libe.a:	libe.a
	install -c -o bin $? $@

$(LIB)/libfp.a:	libfp.a
	install -c -o bin $? $@

$(LIB)/end.a:	end.a
	install -c -o bin $? $@

$(LIB)/libcurses.a:	libcurses.a
	install -c -o bin $? $@

$(LIB)/libedit.a:	libedit.a
	install -c -o bin $? $@

$(LIB)/liby.a:	liby.a
	install -c -o bin $? $@

# Installation rules for Minix-386.
LIB386=	/usr/lib/i386
LIB86=	/usr/lib/i86

install_i386:	\
	$(LIB386)/crtso.o \
	$(LIB386)/libc.a \
	$(LIB386)/libd.a \
	$(LIB386)/libe.a \
	$(LIB386)/libfp.a \
	$(LIB386)/end.a \
	$(LIB386)/libcurses.a \
	$(LIB386)/libedit.a \
	$(LIB386)/liby.a \
#	$(LIB86)/libc.a \
#	$(LIB86)/end.a \

$(LIB386)/crtso.o:	crtso.o
	install -c -o bin $? $@

$(LIB386)/libc.a:	libc.a
	install -c -o bin $? $@

$(LIB386)/libd.a:	libd.a
	install -c -o bin $? $@

$(LIB386)/libe.a:	libe.a
	install -c -o bin $? $@

$(LIB386)/libfp.a:	libfp.a
	install -c -o bin $? $@

$(LIB386)/end.a:	end.a
	install -c -o bin $? $@

$(LIB386)/libcurses.a:	libcurses.a
	install -c -o bin $? $@

$(LIB386)/libedit.a:	libedit.a
	install -c -o bin $? $@

$(LIB386)/liby.a:	liby.a
	install -c -o bin $? $@

$(LIB86)/libc.a:	libc86.a
	install -c -o bin $? $@

$(LIB86)/end.a:	end86.a
	install -c -o bin $? $@

clean:
	rm -rf *.[oa] */*.o */*.bak
