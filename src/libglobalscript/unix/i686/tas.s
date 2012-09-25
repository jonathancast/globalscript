/* §source.file Translation of Plan 9's 386/tas.s, here so we can use these routines on Unix too */

.text
.global _tas

_tas:
	MOVL	$0xdeadead,%eax
	MOVL	4(%esp),%ebx
	XCHGL	%eax,(%ebx)
	RET
