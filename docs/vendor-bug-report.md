# Bug report: mToken CryptoID FIPS tokens can't sign through emBridge on macOS

Ready to send to eMudhra (emBridge) and Longmai (mToken driver). Add your own version numbers if they differ.

---

**Subject:** emBridge for macOS: mToken CryptoID FIPS token not detected, and C_Sign fails with 27014 (0x6986)

**Environment**
- macOS 27, Apple Silicon. emBridge runs as x86_64 under Rosetta.
- emBridge for macOS 3.2.x
- Token: Longmai mToken CryptoID FIPS (USB product `mTokenCryptoFIPS`)
- Longmai macOS driver installed: `/usr/local/lib/libcryptoida_pkcs11_f3.dylib` (universal x86_64/arm64)
- Portal: MCA V3, Associate DSC

### Issue 1 (emBridge): wrong mToken driver path, so no token is found

For the provider "mToken CryptoID CSP", emBridge loads
`/Applications/CryptoIDATools.app/Contents/MacOS/libcryptoid_pkcs11.dylib`.
On this system that file is an x86_64-only build from 2019 that doesn't detect FIPS tokens.
`C_GetSlotList(tokenPresent=TRUE)` returns 0 slots, so emBridge logs:

```
Token mToken CryptoID CSP => 0 slots found
```

The current Longmai driver, `/usr/local/lib/libcryptoida_pkcs11_f3.dylib`, detects the same token (1 slot) in both x86_64 and arm64.

**Request:** check `/usr/local/lib/libcryptoida_pkcs11_f3.dylib` (and any other current mToken driver paths) before the legacy CryptoIDATools path.

### Issue 2 (emBridge, and the token is strict): DigestInfo without NULL parameters is rejected

With the working driver, emBridge logs in successfully (`C_Login` returns CKR_OK), then calls:

```
C_SignInit(mechanism = CKM_RSA_PKCS)
C_Sign(dataLen = 49)
```

The 49-byte input is a SHA-256 DigestInfo whose AlgorithmIdentifier **has no NULL parameters**:

```
30 2f 30 0b 06 09 60 86 48 01 65 03 04 02 01 04 20 <32-byte hash>
```

The token rejects it: `C_Sign` returns vendor code `0x6986` (27014). emBridge logs
`PKCSSign -> Exception => Method C_Sign returned 27014`, and the portal shows
"Failed to extract sign data, please ensure you have changed the password". That message is misleading, because the PIN had already been changed and `C_Login` had succeeded.

Using the same session, key and driver, the standard 51-byte encoding **succeeds**, as does `CKM_SHA256_RSA_PKCS`:

```
30 31 30 0d 06 09 60 86 48 01 65 03 04 02 01 05 00 04 20 <32-byte hash>
```

RFC 8017 section 9.2, note 1, gives the DigestInfo encodings for EMSA-PKCS1-v1_5 with NULL parameters.

**Request to eMudhra:** build the DigestInfo with explicit NULL parameters (the standard RFC 8017 encoding), or use `CKM_SHA256_RSA_PKCS`, for mToken tokens.
**Request to Longmai:** consider accepting DigestInfo with absent parameters, as many other tokens do.

**Also:** when `C_Sign` fails after a successful `C_Login`, the portal shouldn't tell the user to change their password.

### Issue 3 (emBridge, minor): service restarts mid-request

The emBridge daemon sometimes restarts (`starting service at port 26769`) 15–30 seconds into a ListToken request, before it replies. Scanning all supported token brands one after another takes about 25–30 seconds. After a restart, the open portal page stops sending requests until the browser is fully restarted.

### How to reproduce Issue 2 without emBridge

A test program that logs in and signs the 49-byte and 51-byte forms with the same key is at
https://github.com/targaryen1212/mtoken-mac-dsc-fix/blob/main/tools/signtest.c. On an affected token, the 51-byte form returns CKR_OK and the 49-byte form returns 0x6986.

### Workaround

A pass-through PKCS#11 module that adds the missing NULL parameters before calling the real driver fixes signing completely. MCA DSC registration then succeeds. Source: https://github.com/targaryen1212/mtoken-mac-dsc-fix
