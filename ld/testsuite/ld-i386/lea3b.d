#source: lea3.s
#as: --32
#ld: -melf_i386
#objdump: -dw

.*: +file format .*


Disassembly of section .text:

#...
[ 	]*[a-f0-9]+:	67 e8 ([0-9a-f]{2} ){4}[ 	]*addr16 call [a-f0-9]+ <foo>
[ 	]*[a-f0-9]+:	67 e8 ([0-9a-f]{2} ){4}[ 	]*addr16 call [a-f0-9]+ <bar>
[ 	]*[a-f0-9]+:	[ a-f0-9]+       	jmp    [a-f0-9]+ <foo>
[ 	]*[a-f0-9]+:	90                   	nop
[ 	]*[a-f0-9]+:	[ a-f0-9]+       	jmp    [a-f0-9]+ <bar>
[ 	]*[a-f0-9]+:	90                   	nop
#pass
