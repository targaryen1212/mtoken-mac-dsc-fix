/*
 * p11check — read-only diagnostic for a PKCS#11 driver. No PIN needed, changes nothing.
 *
 * Shows whether the driver loads, how many token slots it sees, the token's status flags
 * (e.g. whether the PIN must be changed), and how many public objects are visible.
 *
 * Usage:  ./p11check /path/to/driver.dylib
 * Test the way emBridge sees it (Intel mode):  arch -x86_64 ./p11check /path/to/driver.dylib
 */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef unsigned long U;
typedef struct { unsigned char maj, min; } V;
typedef struct { unsigned char label[32], man[32], model[16], serial[16];
                 U flags, a, b, c, d, maxpin, minpin, e, f, g, h; V hw, fw; unsigned char t[16]; } TI;
typedef struct { U type; void *p; U len; } A;

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s /path/to/pkcs11.dylib\n", argv[0]); return 2; }
    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { printf("FAIL  could not load driver: %s\n", dlerror()); return 1; }
    printf("OK    driver loaded\n");

    U (*init)(void *) = dlsym(h, "C_Initialize");
    U (*gsl)(unsigned char, U *, U *) = dlsym(h, "C_GetSlotList");
    U (*gti)(U, TI *) = dlsym(h, "C_GetTokenInfo");
    U (*os)(U, U, void *, void *, U *) = dlsym(h, "C_OpenSession");
    U (*fi)(U, A *, U) = dlsym(h, "C_FindObjectsInit");
    U (*fo)(U, U *, U, U *) = dlsym(h, "C_FindObjects");
    U (*ff)(U) = dlsym(h, "C_FindObjectsFinal");
    if (!init || !gsl) { printf("FAIL  not a PKCS#11 driver\n"); return 1; }

    U irv = init(0);
    printf("      C_Initialize rv=0x%lx\n", irv);
    if (irv && irv != 0x191 /* CKR_CRYPTOKI_ALREADY_INITIALIZED */) {
        printf("FAIL  driver failed to initialise. If this is p11shim, check /Library/Logs/p11shim.log\n"); return 1;
    }
    U slots[8], n = 8; U srv = gsl(1, slots, &n);
    if (srv) { printf("FAIL  C_GetSlotList rv=0x%lx\n", srv); return 1; }
    printf("      C_GetSlotList rv=0x0, tokens present: %lu\n", n);
    if (!n) { printf("FAIL  driver sees no token. Is it plugged in, and is this the right driver for it?\n"); return 1; }
    printf("OK    token found\n");

    TI t; memset(&t, 0, sizeof t);
    if (gti && gti(slots[0], &t) == 0) {
        printf("      label: %.32s   (may contain your name: remove it before sharing this output)\n", t.label);
        printf("      model: %.16s\n", t.model);
        struct { U b; const char *n; } F[] = {
            {0x4, "LOGIN_REQUIRED"}, {0x8, "USER_PIN_INITIALIZED"}, {0x400, "TOKEN_INITIALIZED"},
            {0x10000, "USER_PIN_COUNT_LOW"}, {0x20000, "USER_PIN_FINAL_TRY"}, {0x40000, "USER_PIN_LOCKED"},
            {0x80000, "USER_PIN_TO_BE_CHANGED"}, {0x800000, "SO_PIN_TO_BE_CHANGED"}};
        printf("      flags: 0x%lx\n", t.flags);
        for (int k = 0; k < 8; k++) if (t.flags & F[k].b) printf("        %s\n", F[k].n);
        if (t.flags & 0x40000) printf("WARN  user PIN is LOCKED — unlock it with your token's admin tool\n");
        if (t.flags & 0x80000) printf("WARN  user PIN must be changed before signing\n");
    }

    U ses;
    if (os && fi && fo && ff && os(slots[0], 4, 0, 0, &ses) == 0) {
        const char *cls[] = {"data", "certificates", "public keys", "private keys (need PIN)"};
        for (U c = 0; c < 4; c++) {
            A a = {0, &c, sizeof c}; U o[32], k = 0;
            fi(ses, &a, 1); fo(ses, o, 32, &k); ff(ses);
            printf("      visible without PIN: %lu %s\n", k, cls[c]);
        }
    }
    return 0;
}
