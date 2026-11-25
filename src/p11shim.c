/*
 * p11shim — a pass-through PKCS#11 module for Longmai mToken CryptoID (FIPS) DSC tokens on macOS.
 *
 * Why this exists
 * ---------------
 * eMudhra emBridge (used by the MCA portal) signs with CKM_RSA_PKCS and passes a SHA-256
 * DigestInfo with the AlgorithmIdentifier NULL parameters omitted (49 bytes):
 *
 *     30 2f 30 0b 06 09 <sha256 oid> 04 20 <32-byte hash>
 *
 * The mToken CryptoID FIPS token rejects that with card status 0x6986 ("command not allowed"),
 * which emBridge reports as "Method C_Sign returned 27014" and the portal shows as
 * "Failed to extract sign data, please ensure you have changed the password".
 *
 * The token accepts the standard PKCS#1 v1.5 encoding with explicit NULL parameters (51 bytes):
 *
 *     30 31 30 0d 06 09 <sha256 oid> 05 00 04 20 <32-byte hash>
 *
 * This module forwards every PKCS#11 call unchanged to the real mToken driver, except one:
 * when C_Sign is called for a CKM_RSA_PKCS session and the input is a DigestInfo with the NULL
 * parameters missing, it inserts "05 00" before passing it on. The hash itself is never changed.
 * Works for any hash algorithm OID, not only SHA-256.
 *
 * Security
 * --------
 * - emBridge runs as root, so the real driver this module loads must not be replaceable by
 *   ordinary users. Before loading, the module checks that REAL_LIB and its folder are owned by
 *   root and not writable by group or others, and refuses to load otherwise (see the log).
 * - The PIN passes through C_Login unchanged, as with any PKCS#11 module. It is never logged,
 *   stored or inspected — not even its length.
 * - The log is written to LOG_PATH in a root-owned folder, opened with O_NOFOLLOW, and capped
 *   in size. It records function names, return codes, mechanisms and lengths, and for raw RSA
 *   signing only, the first bytes of the DigestInfo header. Build with -DSHIM_LOG=0 to disable.
 *
 * Build:  see build.sh  (universal x86_64 + arm64; emBridge runs as x86_64 under Rosetta)
 * License: MIT
 */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>
#include <pthread.h>

#ifndef REAL_LIB
#define REAL_LIB "/Library/p11shim/libcryptoida_pkcs11_f3.dylib"
#endif
#ifndef LOG_PATH
#define LOG_PATH "/Library/Logs/p11shim.log"
#endif
#ifndef SHIM_LOG
#define SHIM_LOG 1
#endif
#ifndef LOG_MAX_BYTES
#define LOG_MAX_BYTES (1024 * 1024)   /* when the log passes 1 MB it is truncated and restarted */
#endif
#ifndef SKIP_OWNER_CHECK              /* developers only: test the shim against an unprotected driver as a normal user */
#define SKIP_OWNER_CHECK 0
#endif

/* Always-present marker so install.sh/uninstall.sh can recognise this file even when logging is compiled out. */
__attribute__((used)) static const char p11shim_marker[] = "p11shim-marker-v1";

typedef unsigned long U;   /* CK_ULONG on macOS (LP64) */
#define NFUNC 68           /* number of function pointers in a PKCS#11 v2.x CK_FUNCTION_LIST */
#define CKR_GENERAL_ERROR 0x5UL
#define CKR_FUNCTION_NOT_SUPPORTED 0x54UL
#define CKM_RSA_PKCS 0x1UL

static const char *names[NFUNC] = {
 "C_Initialize","C_Finalize","C_GetInfo","C_GetFunctionList","C_GetSlotList","C_GetSlotInfo","C_GetTokenInfo",
 "C_GetMechanismList","C_GetMechanismInfo","C_InitToken","C_InitPIN","C_SetPIN","C_OpenSession","C_CloseSession",
 "C_CloseAllSessions","C_GetSessionInfo","C_GetOperationState","C_SetOperationState","C_Login","C_Logout",
 "C_CreateObject","C_CopyObject","C_DestroyObject","C_GetObjectSize","C_GetAttributeValue","C_SetAttributeValue",
 "C_FindObjectsInit","C_FindObjects","C_FindObjectsFinal","C_EncryptInit","C_Encrypt","C_EncryptUpdate",
 "C_EncryptFinal","C_DecryptInit","C_Decrypt","C_DecryptUpdate","C_DecryptFinal","C_DigestInit","C_Digest",
 "C_DigestUpdate","C_DigestKey","C_DigestFinal","C_SignInit","C_Sign","C_SignUpdate","C_SignFinal",
 "C_SignRecoverInit","C_SignRecover","C_VerifyInit","C_Verify","C_VerifyUpdate","C_VerifyFinal",
 "C_VerifyRecoverInit","C_VerifyRecover","C_DigestEncryptUpdate","C_DecryptDigestUpdate","C_SignEncryptUpdate",
 "C_DecryptVerifyUpdate","C_GenerateKey","C_GenerateKeyPair","C_WrapKey","C_UnwrapKey","C_DeriveKey",
 "C_SeedRandom","C_GenerateRandom","C_GetFunctionStatus","C_CancelFunction","C_WaitForSlotEvent"};

/* Every PKCS#11 v2.x function takes at most 8 integer/pointer arguments, so one 8-argument
 * signature can forward any of them on both x86_64 and arm64. */
typedef U (*fn8)(U,U,U,U,U,U,U,U);
typedef struct { unsigned char major, minor; } V;
typedef struct { V version; void *f[NFUNC]; } FL;

static FL *real; static FL mine; static U load_rv = CKR_GENERAL_ERROR;
static pthread_once_t load_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t log_mu = PTHREAD_MUTEX_INITIALIZER, sess_mu = PTHREAD_MUTEX_INITIALIZER;

static void lg(const char *fmt, ...) {
#if SHIM_LOG
    pthread_mutex_lock(&log_mu);
    int fd = open(LOG_PATH, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd >= 0) {
        struct stat st;
        if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_nlink == 1) {
            if (st.st_size > LOG_MAX_BYTES) ftruncate(fd, 0);
            char line[512]; time_t t = time(0); struct tm tmv;
            int n = (int)strftime(line, sizeof line, "%Y-%m-%d %H:%M:%S ", localtime_r(&t, &tmv));
            va_list ap; va_start(ap, fmt); int m = vsnprintf(line + n, sizeof line - n - 1, fmt, ap); va_end(ap);
            if (m < 0) m = 0;
            n += (m < (int)(sizeof line - n - 1)) ? m : (int)(sizeof line - n - 2);
            line[n++] = '\n';
            write(fd, line, n);
        }
        close(fd);
    }
    pthread_mutex_unlock(&log_mu);
#else
    (void)fmt;
#endif
}

static void detail(int i, U a, U b, U c, U d, U e, U rv) {
    switch (i) {
    case 12: lg("   OpenSession slot=%lu flags=0x%lx -> session=%lu", a, b, rv ? 0 : (e ? *(U*)e : 0)); break;
    case 13: lg("   CloseSession session=%lu", a); break;
    case 18: lg("   Login session=%lu userType=%lu", a, b); break;   /* PIN not logged, not even its length */
    case 19: lg("   Logout session=%lu", a); break;
    case 27: if (rv == 0 && d) lg("   FindObjects found=%lu", *(U*)d); break;
    case 42: lg("   SignInit session=%lu mechanism=0x%lx", a, b ? *(U*)b : 0); break;
    case 43: lg("   Sign session=%lu dataLen=%lu sigBuf=%s", a, c, d ? "yes" : "null(size query)"); break;
    }
}

/* Remember which mechanism each open session's C_SignInit used. */
#define MAXSESS 64
static U sess_id[MAXSESS], sess_mech[MAXSESS]; static int sess_used[MAXSESS];
static void setmech(U s, U m) {
    pthread_mutex_lock(&sess_mu);
    int slot = -1;
    for (int k = 0; k < MAXSESS; k++) if (sess_used[k] && sess_id[k] == s) { slot = k; break; }
    if (slot < 0) for (int k = 0; k < MAXSESS; k++) if (!sess_used[k]) { slot = k; break; }
    if (slot >= 0) { sess_used[slot] = 1; sess_id[slot] = s; sess_mech[slot] = m; }
    pthread_mutex_unlock(&sess_mu);
    /* Table full (sessions leaked without C_CloseSession): this session isn't tracked, so its input is
     * forwarded unchanged. Entries are also cleared on C_CloseAllSessions and C_Finalize. */
    if (slot < 0) lg("WARNING: session table full, DigestInfo fix skipped for session %lu", s);
}
static U getmech(U s) {
    U m = 0; pthread_mutex_lock(&sess_mu);
    for (int k = 0; k < MAXSESS; k++) if (sess_used[k] && sess_id[k] == s) { m = sess_mech[k]; break; }
    pthread_mutex_unlock(&sess_mu); return m;
}
static void forget(U s, int all) {
    pthread_mutex_lock(&sess_mu);
    for (int k = 0; k < MAXSESS; k++) if (all || (sess_used[k] && sess_id[k] == s)) sess_used[k] = 0;
    pthread_mutex_unlock(&sess_mu);
}

/*
 * If `in` is a DER DigestInfo whose AlgorithmIdentifier has no parameters, i.e.
 *     SEQ { SEQ { OID }, OCTET STRING hash }
 * write the standard form
 *     SEQ { SEQ { OID, NULL }, OCTET STRING hash }
 * to `out` and return its length. Return 0 (no change) for anything else.
 * Only short-form DER lengths are handled, which covers every hash up to SHA-512.
 */
static U fix_digestinfo(const unsigned char *in, U len, unsigned char *out, U outsz) {
    if (len < 12 || len > 127 || in[0] != 0x30 || in[2] != 0x30 || in[4] != 0x06) return 0;
    U oidlen = in[5]; U p = 6 + oidlen;
    if (in[3] != 2 + oidlen) return 0;                 /* inner SEQUENCE must contain only the OID */
    if (p + 2 > len || in[p] != 0x04) return 0;        /* followed directly by the OCTET STRING */
    U hlen = in[p + 1];
    if (p + 2 + hlen != len || in[1] != len - 2) return 0;
    U nl = len + 2; if (nl > outsz || nl > 127) return 0;
    out[0] = 0x30; out[1] = (unsigned char)(nl - 2);
    out[2] = 0x30; out[3] = (unsigned char)(in[3] + 2);
    memcpy(out + 4, in + 4, 2 + oidlen);               /* OID tag, length, value */
    out[6 + oidlen] = 0x05; out[7 + oidlen] = 0x00;    /* NULL parameters */
    memcpy(out + 8 + oidlen, in + p, 2 + hlen);        /* OCTET STRING with the unchanged hash */
    return nl;
}

static U w43(U a,U b,U c,U d,U e,U f,U g,U h) {   /* C_Sign(session, data, dataLen, sig, sigLen) */
    (void)f; (void)g; (void)h;
    unsigned char buf[256]; U nl = 0; U mech = getmech(a);
    if (mech == CKM_RSA_PKCS && b) {
        /* emBridge's raw-RSA input is a DigestInfo: a fixed algorithm header followed by a hash.
         * Log the first 6 bytes only when they look like one (start with SEQUENCE ... SEQUENCE). */
        if (c >= 6 && ((unsigned char*)b)[0] == 0x30 && ((unsigned char*)b)[2] == 0x30) lg("   Sign DigestInfo header %02x %02x %02x %02x %02x %02x ... len=%lu",
            ((unsigned char*)b)[0], ((unsigned char*)b)[1], ((unsigned char*)b)[2], ((unsigned char*)b)[3],
            ((unsigned char*)b)[4], ((unsigned char*)b)[5], c);
        nl = fix_digestinfo((unsigned char*)b, c, buf, sizeof buf);
    }
    if (nl) lg("   FIX: DigestInfo missing NULL params, rewrote %lu -> %lu bytes", c, nl);
    U rv = ((U(*)(U,U,U,U,U))real->f[43])(a, nl ? (U)buf : b, nl ? nl : c, d, e);
    lg("C_Sign rv=0x%lx%s", rv, rv ? "  <-- ERROR" : "");
    if (nl) lg("   Sign session=%lu dataLen=%lu (sent to token: %lu) sigBuf=%s", a, c, nl, d ? "yes" : "null(size query)");
    else detail(43, a, b, c, d, e, rv);
    return rv;
}

U C_GetFunctionList(FL **pp);
static U w3(U a,U b,U c,U d,U e,U f,U g,U h) {    /* funcs->C_GetFunctionList returns the shim's table */
    (void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h; return C_GetFunctionList((FL **)a);
}

#define W(i) static U w##i(U a,U b,U c,U d,U e,U f,U g,U h){ \
    if (i == 42 && b) setmech(a, *(U*)b); \
    U rv = ((fn8)real->f[i])(a,b,c,d,e,f,g,h); \
    if (i == 13 && rv == 0) forget(a, 0); \
    if ((i == 14 || i == 1) && rv == 0) forget(0, 1); \
    if ((i != 4 && i != 5 && i != 6 && i != 15 && i != 24) || rv) lg("%s rv=0x%lx%s", names[i], rv, rv ? "  <-- ERROR" : ""); \
    detail(i,a,b,c,d,e,rv); return rv; }
W(0)W(1)W(2)W(4)W(5)W(6)W(7)W(8)W(9)W(10)W(11)W(12)W(13)W(14)W(15)W(16)W(17)W(18)W(19)
W(20)W(21)W(22)W(23)W(24)W(25)W(26)W(27)W(28)W(29)W(30)W(31)W(32)W(33)W(34)W(35)W(36)W(37)W(38)W(39)
W(40)W(41)W(42)W(44)W(45)W(46)W(47)W(48)W(49)W(50)W(51)W(52)W(53)W(54)W(55)W(56)W(57)W(58)W(59)
W(60)W(61)W(62)W(63)W(64)W(65)W(66)W(67)
static void *wr[NFUNC] = {w0,w1,w2,w3,w4,w5,w6,w7,w8,w9,w10,w11,w12,w13,w14,w15,w16,w17,w18,w19,
 w20,w21,w22,w23,w24,w25,w26,w27,w28,w29,w30,w31,w32,w33,w34,w35,w36,w37,w38,w39,
 w40,w41,w42,w43,w44,w45,w46,w47,w48,w49,w50,w51,w52,w53,w54,w55,w56,w57,w58,w59,
 w60,w61,w62,w63,w64,w65,w66,w67};

/* The real driver and its folder must be root-owned and not writable by group/others,
 * otherwise anyone could replace it and run code inside the root emBridge daemon.
 * Only the file and its immediate folder are checked (not ACLs or parent folders). That is enough for
 * the default /Library/p11shim, since /Library is root:wheel 755 on macOS. If you change REAL_LIB,
 * make sure every parent folder is root-owned too. */
static int path_is_safe(const char *path, int must_be_dir) {
    struct stat st;
    if (lstat(path, &st) != 0) { lg("REFUSED: cannot stat %s", path); return 0; }
    if (S_ISLNK(st.st_mode)) { lg("REFUSED: %s is a symlink", path); return 0; }
    if (must_be_dir ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode)) { lg("REFUSED: %s has the wrong file type", path); return 0; }
    if (st.st_uid != 0) { lg("REFUSED: %s is not owned by root", path); return 0; }
    if (st.st_mode & (S_IWGRP | S_IWOTH)) { lg("REFUSED: %s is writable by group or others", path); return 0; }
    return 1;
}

static void do_load(void) {
    if (!SKIP_OWNER_CHECK) {
        char dir[1024]; strncpy(dir, REAL_LIB, sizeof dir - 1); dir[sizeof dir - 1] = 0;
        if (!path_is_safe(dirname(dir), 1) || !path_is_safe(REAL_LIB, 0)) {
            lg("Not loading the real driver. Run install.sh again to copy it to a safe location.");
            load_rv = CKR_GENERAL_ERROR; return;
        }
    }
    void *h = dlopen(REAL_LIB, RTLD_NOW | RTLD_LOCAL);
    if (!h) { lg("dlopen of real driver failed: %s", dlerror()); load_rv = CKR_GENERAL_ERROR; return; }
    U (*gfl)(FL **) = dlsym(h, "C_GetFunctionList");
    if (!gfl) { lg("real driver has no C_GetFunctionList"); load_rv = CKR_GENERAL_ERROR; return; }
    FL *r = 0; U rv = gfl(&r);
    if (rv || !r) { load_rv = rv ? rv : CKR_GENERAL_ERROR; return; }
    mine.version = r->version;
    for (int i = 0; i < NFUNC; i++) mine.f[i] = r->f[i] ? wr[i] : 0;
    mine.f[3] = wr[3];
    real = r;                    /* published last; pthread_once guarantees visibility to callers */
    load_rv = 0;
    lg("=== p11shim loaded, real driver %s ===", REAL_LIB);
}
static U load(void) { pthread_once(&load_once, do_load); return load_rv; }

U C_GetFunctionList(FL **pp) {
    if (!pp) return 0x7 /* CKR_ARGUMENTS_BAD */;
    U rv = load(); if (rv) return rv; *pp = &mine; return 0;
}

/* Direct exports, for callers that dlsym individual functions instead of using C_GetFunctionList. */
#define D(name, i) U name(U a,U b,U c,U d,U e,U f,U g,U h){ U rv = load(); if (rv) return rv; \
    if (!mine.f[i]) return CKR_FUNCTION_NOT_SUPPORTED; return ((fn8)mine.f[i])(a,b,c,d,e,f,g,h); }
D(C_Initialize,0) D(C_Finalize,1) D(C_GetInfo,2) D(C_GetSlotList,4) D(C_GetSlotInfo,5) D(C_GetTokenInfo,6)
D(C_GetMechanismList,7) D(C_GetMechanismInfo,8) D(C_SetPIN,11) D(C_OpenSession,12) D(C_CloseSession,13)
D(C_CloseAllSessions,14) D(C_GetSessionInfo,15) D(C_Login,18) D(C_Logout,19) D(C_GetAttributeValue,24)
D(C_FindObjectsInit,26) D(C_FindObjects,27) D(C_FindObjectsFinal,28) D(C_SignInit,42) D(C_Sign,43)
D(C_SignUpdate,44) D(C_SignFinal,45) D(C_DigestInit,37) D(C_Digest,38) D(C_GenerateRandom,64) D(C_WaitForSlotEvent,67)
