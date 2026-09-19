/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 farfromoffice */

#include "../lib/eris.h"

/* Exists to be killed. A program that reaches past what it owns must take the
   fault alone, and the kernel has to carry on afterwards. */

int main(void)
{
    print("crash: about to write where nothing is mapped\n");

    volatile u64* nowhere = (volatile u64*)0x4000000000ULL;
    *nowhere = 1;

    print("crash: still here, which means the kernel let it through\n");
    return 0;
}
