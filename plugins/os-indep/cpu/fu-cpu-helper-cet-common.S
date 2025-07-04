/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 * Copyright 2024 H.J. Lu <hjl.tools@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

	.text
	.globl	fu_cpu_helper_cet_testfn1
	.type	fu_cpu_helper_cet_testfn1, %function
fu_cpu_helper_cet_testfn1:
	.cfi_startproc
	movq	(%rsp), %rax
	addq	$8, %rsp
	jmp	*%rax
	.cfi_endproc
	.size	fu_cpu_helper_cet_testfn1, .-fu_cpu_helper_cet_testfn1
	.section	.note.GNU-stack,"",%progbits
