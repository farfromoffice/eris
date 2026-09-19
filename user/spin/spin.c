/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 farfromoffice */

#include "../lib/eris.h"

/* Runs long enough to be interrupted many times over. If the kernel resumes a
   program with a register out of place, the sum comes back wrong. */

int main(void)
{
    const u64 rounds = 40000000;
    u64 sum = 0;

    print("spin: counting\n");

    for (u64 i = 1; i <= rounds; ++i)
        sum += i % 7;

    /* The residues repeat every seven numbers and forty million is a whole
       number of those cycles plus five, which works out to this. */
    const u64 expected = 120000000;

    print("spin: sum is ");
    print_number(sum);
    print(", expected ");
    print_number(expected);
    print("\n");

    if (sum != expected) {
        print("spin: the numbers do not match\n");
        return 1;
    }

    print("spin: survived the ticks\n");
    return 0;
}
