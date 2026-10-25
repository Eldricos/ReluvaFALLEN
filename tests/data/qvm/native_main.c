/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 The Reluva contributors
 *
 * Runs test_vm.c natively and prints the results the QVM must match, as a
 * C++ table. The system calls do what the tests' handler does. */
#include <stdio.h>

int vmMain(int, int, int, int, int, int, int, int, int, int, int, int, int);

static int printed;
int trap_Print(const char *text) {
    int n = 0;
    while (text[n])
        n++;
    printed = n;
    return n * 3;
}
int trap_Add(int a, int b) { return a * 2 + b; }
int trap_Reenter(int command, int arg) { return vmMain(command, arg, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0); }
float trap_Halve(float f) { return f / 2.0f; }

int main(void) {
    static const int cases[][3] = {
        {0, 3, 4}, {0, -7, 9}, {1, 10, 0}, {1, 15, 0}, {2, 7, 0}, {2, -40, 0}, {3, 7, 0}, {3, 2000, 0}, {3, -9, 0},
        {4, 12345, 0}, {4, -98765, 0}, {4, 0x7fffffff, 0}, {5, 3, 0}, {5, 100, 0}, {5, 7, 0}, {5, -2, 0},
        {6, 0, 9}, {6, 1, 9}, {6, 2, 9}, {7, 42, -17}, {8, 5, 0}, {9, 12, 0}, {10, 3, 0}, {11, 0, 0},
    };
    int i;
    printf("struct QvmCase { int command, arg0, arg1, expected; };\n");
    printf("static const QvmCase kQvmCases[] = {\n");
    for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
        const int *c = cases[i];
        int r = vmMain(c[0], c[1], c[2], 6, 0, 0, 0, 0, 0, 0, 0, 0, 100);
        printf("    {%d, %d, %d, %d},\n", c[0], c[1], c[2], r);
    }
    printf("};\n");
    return 0;
}
