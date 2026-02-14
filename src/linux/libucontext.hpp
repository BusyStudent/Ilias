// Using https://github.com/kaniini/libucontext
/*
 * Copyright (c) 2020 Ariadne Conill <ariadne@dereferenced.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * This software is provided 'as is' and without any warranty, express or
 * implied.  In no event shall the authors be liable for any damages arising
 * from the use of this software.
 */

#pragma once

#include <ilias/defines.hpp>
#include <cstring>
#include <cstdarg>
#include <cstdint>
#include <ucontext.h>

ILIAS_NS_BEGIN

// Pollyfill of the ucontext on some platform (like android)
namespace sys {

// On android, the system header provide it
using ::ucontext_t;

#if  !defined(__ANDROID__)
using ::makecontext;
using ::swapcontext;
using ::getcontext;
using ::setcontext;
#elif defined(__aarch64__)

// https://github.com/kaniini/libucontext/tree/master/arch/aarch64
/*
 * REG_SZ                = 8
 * MCONTEXT_GREGS        = 184
 * REG_OFFSET(n)         = 184 + n * 8
 *
 *   REG_OFFSET(0)  = 184    REG_OFFSET(16) = 312
 *   REG_OFFSET(2)  = 200    REG_OFFSET(18) = 328
 *   REG_OFFSET(4)  = 216    REG_OFFSET(20) = 344
 *   REG_OFFSET(6)  = 232    REG_OFFSET(22) = 360
 *   REG_OFFSET(8)  = 248    REG_OFFSET(24) = 376
 *   REG_OFFSET(10) = 264    REG_OFFSET(26) = 392
 *   REG_OFFSET(12) = 280    REG_OFFSET(28) = 408
 *   REG_OFFSET(14) = 296    REG_OFFSET(30) = 424
 *
 * SP_OFFSET              = 432
 * PC_OFFSET              = 440
 * PSTATE_OFFSET          = 448
 * FPSIMD_CONTEXT_OFFSET  = 464
 */

inline namespace {

extern "C" // For swapcontext call this
[[using gnu: naked, visibility("hidden")]]
inline auto _ilias_asm_setcontext(ucontext_t *uc) noexcept -> int {
	asm(
		// x0 = ucp

		/* restore GPRs */
		"ldp    x18, x19, [x0, #328]\n\t"   // REG_OFFSET(18)
		"ldp    x20, x21, [x0, #344]\n\t"   // REG_OFFSET(20)
		"ldp    x22, x23, [x0, #360]\n\t"   // REG_OFFSET(22)
		"ldp    x24, x25, [x0, #376]\n\t"   // REG_OFFSET(24)
		"ldp    x26, x27, [x0, #392]\n\t"   // REG_OFFSET(26)
		"ldp    x28, x29, [x0, #408]\n\t"   // REG_OFFSET(28)
		"ldr    x30,      [x0, #424]\n\t"   // REG_OFFSET(30)

		/* save current stack pointer */
		"ldr    x2, [x0, #432]\n\t"          // SP_OFFSET
		"mov    sp, x2\n\t"

		"add    x2, x0, #464\n\t"            // FPSIMD_CONTEXT_OFFSET
		"ldp    q8, q9,   [x2, #144]\n\t"
		"ldp    q10, q11, [x2, #176]\n\t"
		"ldp    q12, q13, [x2, #208]\n\t"
		"ldp    q14, q15, [x2, #240]\n\t"

		/* save current program counter in link register */
		"ldr    x16, [x0, #440]\n\t"         // PC_OFFSET

		/* restore args */
		"ldp    x2, x3,   [x0, #200]\n\t"   // REG_OFFSET(2)
		"ldp    x4, x5,   [x0, #216]\n\t"   // REG_OFFSET(4)
		"ldp    x6, x7,   [x0, #232]\n\t"   // REG_OFFSET(6)
		"ldp    x0, x1,   [x0, #184]\n\t"   // REG_OFFSET(0)

		/* jump to new PC */
		"br     x16\n"
	);
}

inline auto setcontext(ucontext_t *uctxt) noexcept -> int {
	return _ilias_asm_setcontext(uctxt);
}

[[using gnu: visibility("hidden")]]
inline auto trampoline() noexcept -> void {
	ucontext_t *link = nullptr;

	// FETCH_LINKPTR(uc_link);
	asm("mov	%0, x19" : "=r" ((link)));

	if (!link) {
		::exit(0);
	}
	setcontext(link);
}

[[using gnu: naked, visibility("hidden")]]
inline auto getcontext(ucontext_t *uc) noexcept -> int {
	asm(
		"str    xzr, [x0, #184]\n\t"        // REG_OFFSET(0)

		/* save x2 and x3 for reuse */
		"stp    x2, x3,   [x0, #200]\n\t"   // REG_OFFSET(2)

		/* save current program counter in link register */
		"str    x30, [x0, #440]\n\t"         // PC_OFFSET

		/* save current stack pointer */
		"mov    x2, sp\n\t"
		"str    x2, [x0, #432]\n\t"          // SP_OFFSET

		/* save pstate */
		"str    xzr, [x0, #448]\n\t"         // PSTATE_OFFSET

		"add    x2, x0, #464\n\t"            // FPSIMD_CONTEXT_OFFSET
		"stp    q8, q9,   [x2, #144]\n\t"
		"stp    q10, q11, [x2, #176]\n\t"
		"stp    q12, q13, [x2, #208]\n\t"
		"stp    q14, q15, [x2, #240]\n\t"

		/* save GPRs and return value 0 */
		"mov    x2, x0\n\t"
		"mov    x0, #0\n\t"

		"stp    x0, x1,   [x2, #184]\n\t"   // REG_OFFSET(0)  — x0=0
		"stp    x4, x5,   [x2, #216]\n\t"   // REG_OFFSET(4)
		"stp    x6, x7,   [x2, #232]\n\t"   // REG_OFFSET(6)
		"stp    x8, x9,   [x2, #248]\n\t"   // REG_OFFSET(8)
		"stp    x10, x11, [x2, #264]\n\t"   // REG_OFFSET(10)
		"stp    x12, x13, [x2, #280]\n\t"   // REG_OFFSET(12)
		"stp    x14, x15, [x2, #296]\n\t"   // REG_OFFSET(14)
		"stp    x16, x17, [x2, #312]\n\t"   // REG_OFFSET(16)
		"stp    x18, x19, [x2, #328]\n\t"   // REG_OFFSET(18)
		"stp    x20, x21, [x2, #344]\n\t"   // REG_OFFSET(20)
		"stp    x22, x23, [x2, #360]\n\t"   // REG_OFFSET(22)
		"stp    x24, x25, [x2, #376]\n\t"   // REG_OFFSET(24)
		"stp    x26, x27, [x2, #392]\n\t"   // REG_OFFSET(26)
		"stp    x28, x29, [x2, #408]\n\t"   // REG_OFFSET(28)
		"str    x30,      [x2, #424]\n\t"   // REG_OFFSET(30)

		"ret\n"
	);
}

[[using gnu: visibility("hidden")]]
inline auto makecontext(ucontext_t *ucp, void (*func)(), int argc) noexcept -> void {
	ILIAS_ASSERT(argc == 0);
    if (argc != 0) {
        __builtin_unreachable();
    }

	// arch/aarch64/makecontext.c
	unsigned long *sp;
	// unsigned long *regp;
	// int i;

	sp = (unsigned long *) ((uintptr_t) ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size);
	sp -= argc < 8 ? 0 : argc - 8;
	sp = (unsigned long *) (((uintptr_t) sp & -16L));

	ucp->uc_mcontext.sp = (uintptr_t) sp;
	ucp->uc_mcontext.pc = (uintptr_t) func;
	ucp->uc_mcontext.regs[19] = (uintptr_t) ucp->uc_link;
	ucp->uc_mcontext.regs[30] = (uintptr_t) &trampoline;

	// regp = (void *) &(ucp->uc_mcontext.regs[0]);
}

[[using gnu: naked, visibility("hidden")]]
inline auto swapcontext(ucontext_t *oucp, ucontext_t *ucp) noexcept -> int {
	asm(
		// x0 = oucp
		// x1 = ucp

		"str    xzr, [x0, #184]\n\t"        // REG_OFFSET(0)

		/* save GPRs */
		"stp    x2, x3,   [x0, #200]\n\t"   // REG_OFFSET(2)
		"stp    x4, x5,   [x0, #216]\n\t"   // REG_OFFSET(4)
		"stp    x6, x7,   [x0, #232]\n\t"   // REG_OFFSET(6)
		"stp    x8, x9,   [x0, #248]\n\t"   // REG_OFFSET(8)
		"stp    x10, x11, [x0, #264]\n\t"   // REG_OFFSET(10)
		"stp    x12, x13, [x0, #280]\n\t"   // REG_OFFSET(12)
		"stp    x14, x15, [x0, #296]\n\t"   // REG_OFFSET(14)
		"stp    x16, x17, [x0, #312]\n\t"   // REG_OFFSET(16)
		"stp    x18, x19, [x0, #328]\n\t"   // REG_OFFSET(18)
		"stp    x20, x21, [x0, #344]\n\t"   // REG_OFFSET(20)
		"stp    x22, x23, [x0, #360]\n\t"   // REG_OFFSET(22)
		"stp    x24, x25, [x0, #376]\n\t"   // REG_OFFSET(24)
		"stp    x26, x27, [x0, #392]\n\t"   // REG_OFFSET(26)
		"stp    x28, x29, [x0, #408]\n\t"   // REG_OFFSET(28)
		"str    x30,      [x0, #424]\n\t"   // REG_OFFSET(30)

		/* save current program counter in link register */
		"str    x30, [x0, #440]\n\t"         // PC_OFFSET

		/* save current stack pointer */
		"mov    x2, sp\n\t"
		"str    x2, [x0, #432]\n\t"          // SP_OFFSET

		/* save pstate */
		"str    xzr, [x0, #448]\n\t"         // PSTATE_OFFSET

		"add    x2, x0, #464\n\t"            // FPSIMD_CONTEXT_OFFSET
		"stp    q8, q9,   [x2, #144]\n\t"
		"stp    q10, q11, [x2, #176]\n\t"
		"stp    q12, q13, [x2, #208]\n\t"
		"stp    q14, q15, [x2, #240]\n\t"

		/* context to swap to is in x1 so... we move to x0 and call setcontext */
		/* store our link register in x28 */
		"mov    x28, x30\n\t"

		/* move x1 to x0 and call setcontext */
		"mov    x0, x1\n\t"
		"bl     _ilias_asm_setcontext\n\t"

		/* hmm, we came back here try to return */
		"mov    x30, x28\n\t"
		"ret\n"
	);
}

} // namespace

#else
	#error "ucontext doesn't support on current platform"
#endif // defined(__ANDROID__)

} // namespace sys

ILIAS_NS_END
