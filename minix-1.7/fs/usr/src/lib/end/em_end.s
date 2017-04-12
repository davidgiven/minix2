#
.sect .text
.sect .rom
.sect .data
.sect .bss
.define endtext,enddata,endbss,__end
.sect .text
	.align _EM_WSIZE
.sect .rom
	.align _EM_WSIZE
.sect .data
	.align _EM_WSIZE
.sect .bss
	.align _EM_WSIZE
.sect .end ! only for declaration of _end, __end and endbss.

	.sect .text
endtext:
	.sect .rom
endrom:
	.sect .data
enddata:
	.sect .end
__end:
endbss:
