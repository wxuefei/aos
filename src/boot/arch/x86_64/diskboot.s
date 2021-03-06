/*_
 * Copyright (c) 2013 Scyphus Solutions Co. Ltd.
 * Copyright (c) 2014 Hirochika Asai
 * All rights reserved.
 *
 * Authors:
 *      Hirochika Asai  <asai@jar.jp>
 */

	.set	BOOTMON_SEG,0x0900	/* Memory where to load kernel loader */
	.set	BOOTMON_OFF,0x0000	/*  segment and offset [0900:0000] */

	/* Disk information */
	.set	NUM_RETRIES,3		/* Number of times to retry to read */
	.set	ERRCODE_TIMEOUT,0x80	/* Error code: timeout */
	.set	ERRCODE_CONTROLLER,0x20	/* Error code: controller failure */

	.set	STAGE2_LBA,442		/* Stage 2 LBA info (lba, size) */

	.file	"diskboot.s"

/* Text section */
	.text

	.code16			/* 16bit real mode */
	.globl	start		/* Entry point */

/* Start
 *   %cs:%ip=0x0000:0x7c00
 *   %dl: drive
 */
start:
/* Setup the stack and segment registers */
	cld			/* Clear direction flag */
				/* (inc di/si for str ops) */
	xorw	%ax,%ax		/* %ax=0 */
/* Setup stack */
	cli
	movw	%ax,%ss
	movw	$start,%sp
/* Setup data segments (%ds=0, %es=0) */
	movw	%ax,%ds
	movw	%ax,%es
	sti
/* Save BIOS boot drive */
	movb	%dl,drive
/* Set VGA mode to 16bit color text mode */
	movb	$0x03,%al
	movb	$0x00,%ah
	int	$0x10
/* For Intel Core i7-3770K processor */
	.rept 10
	nop
	.endr
/* Display the welcome message */
	movw	$msg_welcome,%si	/* %ds:(%si) -> welcome message */
	call	putstr

/* Drive information */
	xorw	%ax,%ax
	//movw	%ax,%es
	//movw	%ax,%di
	movb	$0x08,%ah
	movb	drive,%dl
	int	$0x13
	jc	read.error
	incb	%dh
	movb	%dh,heads
	movb	%cl,%al
	andb	$0x3f,%al
	movb	%al,sectors
	movb	%ch,%al
	shrb	$6,%cl
	andb	$0x3,%cl
	movb	%cl,%ah
	incw	%ax
	movw	%ax,cylinders

/* Check the stage 2 information */
	movw	$start,%bp
	movw	STAGE2_LBA(%bp),%ax
	movw	STAGE2_LBA+2(%bp),%cx

load_stage2:
	pushw	%bp		/* Save the base pointer */
	movw	%sp,%bp		/* Copy the stack pointer to the base pointer */
/* Save registers */
	movw	%ax,-2(%bp)
	movw	%cx,-4(%bp)
	subw	$4,%sp

	movw	$BOOTMON_SEG,%ax
	movw	%ax,%es
	movw	$BOOTMON_OFF,%bx
	movb	$1,%dh
	movb	drive,%dl
1:
	movw	-2(%bp),%ax
	call	read
	incw	%ax
	movw	%ax,-2(%bp)
	movw	%es,%ax
	addw	$0x20,%ax
	movw	%ax,%es
	movw	-4(%bp),%ax
	decw	%ax
	movw	%ax,-4(%bp)
	testw	%ax,%ax
	jnz	1b

/* Restore the stack pointer and base pointer */
	movw	%bp,%sp
	popw	%bp

	ljmp	$BOOTMON_SEG,$BOOTMON_OFF


/* Read %dh sectors starting at LBA (logical block address) %ax on drive %dl
   into %es:[%bx] */
read:
	pushw	%bp		/* Save the base pointer */
	movw	%sp,%bp		/* Copy the stack pointer to the base pointer */
/* Save registers */
	movw	%ax,-2(%bp)
	movw	%bx,-4(%bp)
	movw	%cx,-6(%bp)
	movw	%dx,-8(%bp)
	/* u16 track:sector -10(%bp) */
	/* u16 counter -12(%bp) */
	subw	$12,%sp

/* Convert LBA to CHS */
	call	lba2chs		/* Convert LBA %ax to CHS (%ch,%dh,%cl) */
	movw	%cx,-10(%bp)	/* Save %cx to the stack */
/* Restore %bx */
	movw	-4(%bp),%bx
/* Reset counter */
	xorw	%cx,%cx
	movw	%cx,-12(%bp)
read.retry:
	call	twiddle         /* Display twiddling bar to the user */
	movw	-8(%bp),%ax	/* Get the saved %dx from the stack */
	movb	%ah,%al		/*  (Note: same as movb -7(%bp),%al */
	movb	$0x02,%ah       /* BIOS: Read sectors from drive */
	movw	-10(%bp),%cx	/* Get the saved %cx from the stack */
	int	$0x13		/* CHS=%ch,%dh,%cl, drive=%dl, count=%al */
				/*  to %es:[%bx] (results in %ax,%cf)  */
	jc	read.fail	/* Fail (%cf=1) */

/* Restore registers */
	movw	-8(%bp),%dx
	movw	-6(%bp),%cx
	movw	-4(%bp),%bx
	movw	-2(%bp),%ax
	movw	%bp,%sp		/* Restore the stack pointer and base pointer */
	popw	%bp
	ret
read.fail:
	movw	-12(%bp),%cx
	incw	%cx
	movw	%cx,-12(%bp)
	cmpw	$NUM_RETRIES,%cx
	ja	read.error		/* Exceed retries */
	cmpb	$ERRCODE_TIMEOUT,%ah	/* Timeout? */
	je	read.retry		/* Yes, then retry */
read.error:				/* We do not restore the stack */
	movb	%ah,%al			/* Save error */
	movw	$hex_error,%di		/* Format it as hex */
	xorw	%bx,%bx
	movw	%bx,%es
	call	hex8
	movw	$msg_readerror,%si	/* Display the read error message */
	jmp	error

/* LBA: %ax into CHS %ch,%dh,%cl */
lba2chs:
	pushw	%bx		/* Save */
	pushw	%dx
/* Compute sector */
	xorw	%dx,%dx
	movw	%dx,%bx
	movb	sectors,%bl
	divw	%bx		/* %dx:%ax / %bx; %ax:quotient, %dx:remainder */
	incb	%dl
	movb	%dl,%cl		/* Sector */
/* Compute head and track */
	xorw	%dx,%dx
	movw	heads,%bx
	divw	%bx		/* %dx:%ax / %bx */
	movw	%dx,%bx		/* Save the remainder to %bx */
	popw	%dx		/* Restore %dx*/
	movb	%bl,%dh		/* Head */
	movb	%al,%ch		/* Track */
	popw	%bx		/* Restore %bx */

	ret


/* Display error message at %si and then halt. */
error:
	call	putstr			/* Display error message */

/* Halt */
halt:
	hlt
	jmp	halt

/* Display a null-terminated string */
putstr:
putstr.load:
	lodsb			/* Load %al from %ds:(%si), then incl %si */
	testb	%al,%al		/* Stop at null */
	jnz	putstr.putc	/* Call the function to output %al */
	ret			/* Return if null is reached */
putstr.putc:
	call	putc		/* Output a character %al */
	jmp	putstr.load	/* Go to next character */
putc:
	pushw	%bx		/* Save %bx */
	movw	$0x7,%bx	/* %bh: Page number for text mode */
				/* %bl: Color code for graphics mode */
	movb	$0xe,%ah	/* BIOS: Put char in tty mode */
	int	$0x10		/* Call BIOS, print a character in %al */
	popw	%bx		/* Restore %bx */
	ret

/* Twiddle a bar */
twiddle:
	pushw	%ax
	pushw	%bx
	movb	twiddle_index,%al	/* Load index */
	movw	$twiddle_chars,%bx	/* Address table */
/* Next char */
	incb	%al
	andb	$3,%al
	movb	%al,twiddle_index	/* Save index for next call */
	xlatb				/* Get char (%al+%bx) -> %al */
	call    putc			/* Output it */
	movb	$8,%al			/* Backspace */
	call	putc			/* Output it */
	popw	%bx
	popw	%ax
	ret

/* Convert AL to hex char, saving the result to [EDI]. */
hex8:
	pushl	%eax
/* Do upper 4 */
	shrb	$0x4,%al
	call	hex8.1
	popl	%eax
hex8.1:
	andb	$0xf,%al	/* Get lower 4 */
	cmpb	$0xa,%al	/* Convert  : CF=1 if %al < $0xa */
	sbbb	$0x69,%al	/*  to hex  : %al<-%al - $0x69 - CF */
	das			/*  digit   : BCD */
	orb	$0x20,%al	/* To lower case */
	stosb			/* Save char to %di and inc %di */
	ret			/* (Recursive) */


/* Data section */
	.data

/* Saved boot drive information */
drive:
	.byte	0
sectors:
	.byte	0
cylinders:
	.word	0
heads:
	.byte	0

/* State and set of characters for twiddle function */
twiddle_index:
	.byte	0x0
twiddle_chars:
	.ascii  "|/-\\"

/* Messages */
msg_welcome:
	.asciz	"Welcome to AOS!\r\n\nLet's get it started.\r\n\n"
msg_readerror:
	.ascii  "Read Error: 0x"
hex_error:
	.asciz  "00\r\r"

/*
 * Local variables:
 * tab-width: 8
 * c-basic-offset: 8
 * End:
 * vim600: sw=8 ts=8 fdm=marker
 * vim<600: sw=8 ts=8
 */
