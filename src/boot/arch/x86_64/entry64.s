/*_
 * Copyright 2013 Scyphus Solutions Co. Ltd.  All rights reserved.
 *
 * Authors:
 *      Hirochika Asai  <asai@scyphus.co.jp>
 */

	.include	"asmconst.h"
	.file		"entry64.s"

/* Text section */
	.text

	.globl	entry64		/* Entry point */


	.align	16
	.code64

entry64:
	xorl	%eax,%eax
	movl	%eax,%ds
	movl	%eax,%es
	movl	%eax,%ss
	movl	%eax,%fs
	movl	%eax,%gs

	/* Clear screen */
	movl	$0xb8000,%edi
	movw	$0x0f20,%ax
	movl	$80*25,%ecx
	rep	stosw

	/* Reset cursor */
	movw	$0x000f,%ax	/* %al=0xf: cursor location low, %ah: xx00 */
	movw	$0x3d4,%dx
	outw	%ax,%dx
	movw	$0x000e,%ax	/* %al=0xe: cursor location high, %ah: 00xx */
	movw	$0x3d4,%dx
	outw	%ax,%dx

	/* Jump to the kernel main function */
	pushq	$GDT_CODE64_SEL
	pushq	$KERNEL_MAIN
	lretq


/* Halt (64bit mode) */
halt64:
	hlt
	jmp	halt64



/* Data section */
	.align	16
	.data
cursor:
	.word	0x0
/* Messages */
msg_welcome:
	.asciz	"Welcome to 64bit long mode\r\n\n"

