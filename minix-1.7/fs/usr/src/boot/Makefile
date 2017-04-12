# Makefile for the boot monitor package.
#
CC=	exec cc
CC86=	exec cc -mi86 -Was-ncc

CFLAGS=	-I.. -m -O
LD=	$(CC) -s -.o
LD86=	$(CC86) -.o
BIN=	/usr/bin
MDEC=	/usr/mdec

all:	bootblock boot masterboot extboot installboot edparams

installboot.o bootimage.o: image.h
boot.o bootimage.o: boot.h
rawfs.o rawfs86.o installboot.o boot.o bootimage.o: rawfs.h

bootblock:	bootblock.s
	$(LD86) bootblock.s -o bootblock

masterboot:	masterboot.s
	$(LD86) masterboot.s -o masterboot

extboot:	extboot.s
	$(LD86) extboot.s -o extboot

boot.o:	boot.c
	$(CC86) $(CFLAGS) -c boot.c

bootimage.o:	bootimage.c
	$(CC86) $(CFLAGS) -c bootimage.c

rawfs86.o:	rawfs.c rawfs.o
	ln -f rawfs.c rawfs86.c
	$(CC86) $(CFLAGS) -c rawfs86.c
	rm rawfs86.c
	-cmp -s rawfs.o rawfs86.o && ln -f rawfs.o rawfs86.o

boot:	boothead.s boot.o bootimage.o rawfs86.o
	$(LD86) boothead.s -i boot.o bootimage.o rawfs86.o -o boot
	install -S 12kb boot

installboot:	installboot.o rawfs.o
	$(CC) $(STRIP) -i -o installboot installboot.o rawfs.o
	install -S 6kw installboot

edparams:	edparams.c
	$(CC) $(CFLAGS) $(STRIP) -i -o edparams edparams.c
	install -S 16kw edparams

install:	$(MDEC)/bootblock $(MDEC)/boot $(MDEC)/masterboot \
		$(MDEC)/extboot $(BIN)/installboot $(BIN)/edparams

$(MDEC)/bootblock:	bootblock
	install -cs -o bin -m 644 $? $@

$(MDEC)/boot:		boot
	install -cs -o bin -m 644 $? $@

$(MDEC)/masterboot:	masterboot
	install -cs -o bin -m 644 $? $@

$(MDEC)/extboot:	extboot
	install -cs -o bin -m 644 $? $@

$(BIN)/installboot:	installboot
	install -cs -o bin $? $@

$(BIN)/edparams:	edparams
	install -cs -o bin $? $@

clean:
	rm -f *.bak *.o
	rm -f bootblock installboot boot masterboot extboot edparams
