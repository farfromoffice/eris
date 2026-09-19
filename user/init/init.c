/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 farfromoffice */

#include "../lib/eris.h"

/* The first program. It proves the four things ring 3 has to do: print through
   a call, learn who it is, wait, and read a file the kernel mounted. */

int main(void)
{
    print("init: hello from ring 3\n");

    print("init: pid is ");
    print_number((u64)getpid());
    print("\n");

    const u64 before = monotonic_ns();
    sleep_ms(20);
    const u64 slept = (monotonic_ns() - before) / 1000000;

    print("init: slept ");
    print_number(slept);
    print(" ms\n");

    int handle = open("/mnt/hello.txt");
    if (handle >= 0) {
        char buffer[64];
        i64 length = read(handle, buffer, sizeof(buffer) - 1);

        if (length > 0) {
            buffer[length] = '\0';
            print("init: /mnt/hello.txt says ");

            for (i64 i = 0; i < length; ++i) {
                if (buffer[i] == '\n')
                    buffer[i] = '\0';
            }

            print(buffer);
            print("\n");
        }

        close(handle);
    } else {
        print("init: no file to read, the disk is empty\n");
    }

    print("init: leaving with 42\n");
    return 42;
}
