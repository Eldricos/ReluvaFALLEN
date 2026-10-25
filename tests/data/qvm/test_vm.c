/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 The Reluva contributors
 *
 * A program for testing Reluva's QVM interpreter against what the Quake III
 * compiler generates. build.sh compiles it twice: natively, for the
 * expected results, and to a QVM (with q3lcc and q3asm), which the tests
 * then run. vmMain must be the first function. */

int trap_Print(const char *text);          /* system call 0 */
int trap_Add(int a, int b);                /* system call 1 */
int trap_Reenter(int command, int arg);    /* system call 2: calls vmMain(command, arg) */
float trap_Halve(float f);                 /* system call 3 */

static int fib(int n);
static int structs(int seed);
static int floats(int x);
static int integers(int x);
static int switches(int x);
static void append(const char *s);
static void appendInt(int v);
static int (*ops[3])(int);
static char buffer[128];
static int table[64];

int vmMain(int command, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7, int arg8,
           int arg9, int arg10, int arg11) {
    int i, sum;
    switch (command) {
    case 0: return arg0 + arg1 * arg2;
    case 1: return fib(arg0);
    case 2: return structs(arg0);
    case 3: return floats(arg0);
    case 4: return integers(arg0);
    case 5: return switches(arg0);
    case 6: return ops[arg0 % 3](arg1);
    case 7:
        buffer[0] = 0;
        append("value ");
        appendInt(arg0);
        append(" and ");
        appendInt(arg1);
        return trap_Print(buffer);
    case 8:
        for (i = 0; i < 64; i++)
            table[i] = trap_Add(i, arg0);
        sum = 0;
        for (i = 0; i < 64; i++)
            sum += table[i];
        return sum;
    case 9: return trap_Reenter(1, arg0) + 1;
    case 10: return arg11 - arg0;
    }
    return -12345;
}

typedef struct {
    int id;
    float weight;
    char name[12];
    short small;
    unsigned char flags;
} item_t;

static item_t items[4];

static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

static int length(const char *s) {
    int n = 0;
    while (s[n])
        n++;
    return n;
}

/* Appends a number to buffer, like a tiny sprintf. */
static void appendInt(int v) {
    char digits[16];
    int n = 0, at = length(buffer);
    unsigned u;
    if (v < 0) {
        buffer[at++] = '-';
        u = (unsigned)(-v);
    } else {
        u = (unsigned)v;
    }
    do {
        digits[n++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    while (n)
        buffer[at++] = digits[--n];
    buffer[at] = 0;
}

static void append(const char *s) {
    int at = length(buffer), i = 0;
    while (s[i])
        buffer[at++] = s[i++];
    buffer[at] = 0;
}

static int square(int x) { return x * x; }
static int negate(int x) { return -x; }
static int twice(int x) { return x + x; }
static int (*ops[3])(int) = {square, negate, twice};

static int structs(int seed) {
    item_t a, b;
    int i, sum = 0;
    for (i = 0; i < 4; i++) {
        items[i].id = seed + i;
        items[i].weight = (float)i * 0.5f;
        items[i].small = (short)(-1000 * i);
        items[i].flags = (unsigned char)(250 + i);
        items[i].name[0] = (char)('a' + i);
        items[i].name[1] = 0;
    }
    a = items[2]; /* a structure copy: BLOCK_COPY */
    b = a;
    b.id++;
    for (i = 0; i < 4; i++)
        sum += items[i].id + items[i].small + items[i].flags + items[i].name[0];
    return sum + b.id + (int)(b.weight * 10.0f) + a.small;
}

static int floats(int x) {
    float f = (float)x, g = 0.0f;
    int i;
    for (i = 1; i <= 10; i++)
        g += f / (float)i;
    if (g > 100.0f)
        g = g - 100.0f;
    else if (g < -5.5f)
        g = -g;
    return (int)(g * 1000.0f) + (int)trap_Halve(f * 3.0f);
}

static int integers(int x) {
    unsigned u = (unsigned)x;
    int r = 0;
    r += x / 7 + x % 7;
    r += (int)(u / 3u) + (int)(u % 5u);
    r += (x << 3) ^ (x >> 2);
    r += (int)(u >> 28) | (x & 0xff);
    r += ~x;
    r += (signed char)x + (short)x;
    r += (x < 0) + (u > 100u) * 2 + (x >= -3) * 4;
    return r;
}

static int switches(int x) {
    /* Dense cases compile to a jump table. */
    switch (x) {
    case 0: return 11;
    case 1: return 22;
    case 2: return 33;
    case 3: return 44;
    case 4: return 55;
    case 5: return 66;
    case 6: return 77;
    case 100: return 1000;
    default: return -1;
    }
}

