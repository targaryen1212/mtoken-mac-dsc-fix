/* fixtest — unit test for the DigestInfo rewrite in p11shim. No token needed.  Run: ./build.sh test */
#include "../src/p11shim.c"

static int fails;
static void check(const char *what, int ok) { printf("%s  %s\n", ok ? "PASS" : "FAIL", what); if (!ok) fails++; }

int main(void) {
    static const unsigned char oid256[] = {0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01};
    unsigned char in[49] = {0x30,0x2f,0x30,0x0b}; memcpy(in + 4, oid256, 11); in[15] = 0x04; in[16] = 0x20;
    memset(in + 17, 0xAB, 32);
    unsigned char want[51] = {0x30,0x31,0x30,0x0d}; memcpy(want + 4, oid256, 11); want[15] = 0x05; want[16] = 0x00;
    want[17] = 0x04; want[18] = 0x20; memset(want + 19, 0xAB, 32);
    unsigned char out[256];

    U n = fix_digestinfo(in, sizeof in, out, sizeof out);
    check("49-byte SHA-256 DigestInfo is rewritten to 51 bytes", n == 51);
    check("rewritten bytes equal the standard encoding", n == 51 && memcmp(out, want, 51) == 0);
    check("standard 51-byte DigestInfo is left unchanged", fix_digestinfo(want, sizeof want, out, sizeof out) == 0);

    static const unsigned char oid1[] = {0x06,0x05,0x2b,0x0e,0x03,0x02,0x1a};   /* SHA-1 */
    unsigned char s1[33] = {0x30,0x1f,0x30,0x07}; memcpy(s1 + 4, oid1, 7); s1[11] = 0x04; s1[12] = 0x14;
    memset(s1 + 13, 0xCD, 20);
    n = fix_digestinfo(s1, sizeof s1, out, sizeof out);
    check("SHA-1 DigestInfo without NULL is also fixed (33 -> 35)", n == 35 && out[11] == 0x05 && out[12] == 0x00);

    unsigned char raw[32]; memset(raw, 0x30, sizeof raw);
    check("raw 32-byte hash (not DigestInfo) is left unchanged", fix_digestinfo(raw, sizeof raw, out, sizeof out) == 0);
    unsigned char junk[49]; memcpy(junk, in, 49); junk[1] = 0x2e;
    check("malformed length is left unchanged", fix_digestinfo(junk, sizeof junk, out, sizeof out) == 0);

    printf(fails ? "\n%d test(s) FAILED\n" : "\nAll tests passed\n", fails);
    return fails != 0;
}
