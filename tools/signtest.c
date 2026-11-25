/*
 * signtest — checks whether your token can sign, with dummy data. Nothing is stored or sent anywhere.
 *
 * Asks for your token PIN (input hidden, never printed or saved), logs in, and signs a fixed
 * dummy hash three ways:
 *   1. CKM_SHA256_RSA_PKCS                         (token hashes the data itself)
 *   2. CKM_RSA_PKCS, standard 51-byte DigestInfo   (with NULL parameters)
 *   3. CKM_RSA_PKCS, 49-byte DigestInfo            (NULL omitted — what emBridge sends)
 *
 * If 1 and 2 pass but 3 fails with rv=0x6986 (or a vendor error), you have the problem p11shim fixes.
 *
 * Usage:  ./signtest /path/to/driver.dylib
 * Careful: each wrong PIN uses up one of the token's limited attempts.
 */
#define __STDC_WANT_LIB_EXT1__ 1   /* for memset_s */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <readpassphrase.h>

typedef unsigned long U;
typedef struct { U type; void *p; U len; } A;
typedef struct { U mech; void *p; U len; } M;
#define F(name, ret, ...) ret (*name)(__VA_ARGS__) = dlsym(h, #name)

static const char *verdict(U rv) { return rv == 0 ? "OK" : "FAILED"; }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s /path/to/pkcs11.dylib\n", argv[0]); return 2; }
    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { printf("could not load driver: %s\n", dlerror()); return 1; }
    F(C_Initialize, U, void *); F(C_GetSlotList, U, unsigned char, U *, U *);
    F(C_OpenSession, U, U, U, void *, void *, U *); F(C_Login, U, U, U, unsigned char *, U);
    F(C_FindObjectsInit, U, U, A *, U); F(C_FindObjects, U, U, U *, U, U *); F(C_FindObjectsFinal, U, U);
    F(C_SignInit, U, U, M *, U); F(C_Sign, U, U, unsigned char *, U, unsigned char *, U *);
    F(C_Logout, U, U); F(C_CloseSession, U, U);
    if (!C_Initialize || !C_GetSlotList || !C_OpenSession || !C_Login || !C_FindObjectsInit || !C_FindObjects ||
        !C_FindObjectsFinal || !C_SignInit || !C_Sign || !C_Logout || !C_CloseSession) {
        printf("This file is not a complete PKCS#11 driver.\n"); return 1;
    }

    C_Initialize(0);
    U slots[4], n = 4; C_GetSlotList(1, slots, &n);
    if (!n) { printf("No token found.\n"); return 1; }
    U ses; U orv = C_OpenSession(slots[0], 4 /* read-only session */, 0, 0, &ses);
    if (orv) { printf("Could not open a session: rv=0x%lx. Is another app (CryptoIDUserTools, the portal) using the token?\n", orv); return 1; }

    char pin[64];
    if (!readpassphrase("Token PIN (hidden): ", pin, sizeof pin, RPP_REQUIRE_TTY)) return 1;
    U rv = C_Login(ses, 1, (unsigned char *)pin, strlen(pin));
    memset_s(pin, sizeof pin, 0, sizeof pin);   /* not optimised away, unlike memset */
    printf("Login: rv=0x%lx %s\n", rv, rv == 0 ? "OK" : rv == 0xa0 ? "WRONG PIN" : rv == 0xa4 ? "PIN LOCKED" : "FAILED");
    if (rv != 0 && rv != 0x100) return 1;

    /* Find a private key that is allowed to sign (CKA_CLASS = CKO_PRIVATE_KEY, CKA_SIGN = TRUE). */
    U cls = 3; unsigned char yes = 1;
    A tmpl[2] = {{0x0, &cls, sizeof cls}, {0x108, &yes, 1}};
    C_FindObjectsInit(ses, tmpl, 2); U keys[8], k = 0; C_FindObjects(ses, keys, 8, &k); C_FindObjectsFinal(ses);
    printf("Signing keys: %lu\n", k);
    if (!k) { printf("No private key with signing permission found on this token.\n"); return 1; }

    static const unsigned char oid[] = {0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01};
    unsigned char hash[32]; memset(hash, 0xAB, sizeof hash);
    unsigned char di51[51] = {0x30,0x31,0x30,0x0d}; memcpy(di51 + 4, oid, 11); di51[15] = 0x05; di51[16] = 0x00;
    di51[17] = 0x04; di51[18] = 0x20; memcpy(di51 + 19, hash, 32);
    unsigned char di49[49] = {0x30,0x2f,0x30,0x0b}; memcpy(di49 + 4, oid, 11);
    di49[15] = 0x04; di49[16] = 0x20; memcpy(di49 + 17, hash, 32);
    unsigned char data[16] = "dummy test data", sig[1024]; U sl;

    M m1 = {0x40, 0, 0};
    rv = C_SignInit(ses, &m1, keys[0]); if (!rv) { sl = sizeof sig; rv = C_Sign(ses, data, sizeof data, sig, &sl); }
    printf("1. SHA256_RSA_PKCS:               rv=0x%lx %s\n", rv, verdict(rv));
    M m2 = {0x1, 0, 0};
    rv = C_SignInit(ses, &m2, keys[0]); if (!rv) { sl = sizeof sig; rv = C_Sign(ses, di51, sizeof di51, sig, &sl); }
    printf("2. RSA_PKCS, 51-byte DigestInfo:  rv=0x%lx %s\n", rv, verdict(rv));
    rv = C_SignInit(ses, &m2, keys[0]); if (!rv) { sl = sizeof sig; rv = C_Sign(ses, di49, sizeof di49, sig, &sl); }
    printf("3. RSA_PKCS, 49-byte DigestInfo:  rv=0x%lx %s\n", rv, verdict(rv));

    C_Logout(ses); C_CloseSession(ses);
    return 0;
}
