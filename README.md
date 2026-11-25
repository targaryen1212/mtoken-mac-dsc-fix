# Using an mToken CryptoID DSC on a Mac: Adobe Acrobat and the MCA portal (emBridge)

This guide is for people in India with a **Longmai mToken CryptoID (FIPS)** USB DSC token on a **Mac**, where:

- **Adobe Acrobat** doesn't list the certificate, or says **"Could not load the PKCS#11 module"**, or
- the **MCA portal** (Associate DSC / Register DSC through **eMudhra emBridge**) shows an **empty Token dropdown**, or fails with
  **"Failed to extract sign data, please ensure you have changed the password"**, even after you've changed the PIN.

It explains why each problem happens and how to fix it. It also includes a small open-source **fix driver** (`p11shim`) for the MCA signing error, which has no other known fix on macOS.

> **Unofficial workaround.** Not affiliated with or endorsed by eMudhra, Longmai, Adobe or MCA. It changes no certificate, key or hash, and the signature is still made by your token. But it does sit in your signing chain, and your PIN passes through it on the way to the token, so read the code before you install it (about 250 lines of C). Use at your own risk.

> **Using an AI coding agent?** Paste the ready-made prompt in [`docs/ai-agent-prompt.md`](docs/ai-agent-prompt.md) into Claude Code, Cursor, Codex or similar. The agent will diagnose your Mac and walk you through only the fixes you need, and it's told never to ask for your PIN or type your password.

---

## Contents

1. [Tested setup](#1-tested-setup)
2. [Check that your Mac sees the token](#2-check-that-your-mac-sees-the-token)
3. [Fix Adobe Acrobat](#3-fix-adobe-acrobat)
4. [Fix the MCA portal / emBridge](#4-fix-the-mca-portal--embridge)
5. [Undo everything](#5-undo-everything)
6. [Troubleshooting](#6-troubleshooting)
7. [Technical details (why this works)](#7-technical-details-why-this-works)
8. [GST and Income Tax portals](#8-gst-and-income-tax-portals)
9. [Security notes](#9-security-notes)

---

## 1. Tested setup

| Item | Version |
|---|---|
| Mac | Apple Silicon (M-series) |
| macOS | 27 |
| Token | Longmai **mToken CryptoID FIPS** (USB name `mTokenCryptoFIPS`, USB vendor ID 19533) |
| Token driver | mToken CryptoID macOS driver, which installs `/usr/local/lib/libcryptoida_pkcs11*.dylib` and `CryptoIDUserTools` / `CryptoIDATools` apps |
| Adobe Acrobat | Acrobat DC 26.x (sandboxed) |
| eMudhra emBridge | 3.2.x for macOS (runs as an Intel app under Rosetta) |

Other mToken models, or Intel Macs, will probably behave the same, but they're untested. ePass2003, HyperSecu, ProxKey and WatchData tokens have **different** problems. This guide doesn't cover them.

### Before you start

- Install the **mToken CryptoID macOS driver** from your DSC provider.
- Install **Rosetta 2** if you're on Apple Silicon: `softwareupdate --install-rosetta --agree-to-license`
- Install the **Xcode Command Line Tools** (needed to build the fix): `xcode-select --install`
- Plug the token **directly** into the Mac if you can. Some USB hubs cause problems.

### Get the code and build the tools

All commands in this guide are run in Terminal from inside the repo folder:

```bash
git clone https://github.com/targaryen1212/mtoken-mac-dsc-fix.git
```
```bash
cd mtoken-mac-dsc-fix
```
```bash
./build.sh test
```

(Or download the ZIP from GitHub and `cd` into the unzipped folder.) The last command builds everything into `build/` and runs the unit tests, which should all pass. It needs no token and no password.

---

## 2. Check that your Mac sees the token

In Terminal:

```bash
ioreg -p IOUSB -w0 | grep -i mtoken
```

You should see a line containing `mTokenCryptoFIPS`. If you don't, the problem is the cable, the port or the hub, not the software.

Then check that the driver can talk to it, using the read-only check tool from this repo (no PIN, changes nothing):

```bash
./build/p11check /usr/local/lib/libcryptoida_pkcs11_f3.dylib
```

You want `OK token found`. In our testing, the mToken package installs **three** driver files, and only `libcryptoida_pkcs11_f3.dylib` saw the FIPS token:

| File | Result |
|---|---|
| `libcryptoida_pkcs11.dylib` | wrapper that loads the `_f2` / `_f3` files; fails inside Acrobat's sandbox |
| `libcryptoida_pkcs11_f2.dylib` | 0 tokens |
| `libcryptoida_pkcs11_f3.dylib` | **1 token, works** |
| `/Applications/CryptoIDATools.app/Contents/MacOS/libcryptoid_pkcs11.dylib` (old, 2019, Intel-only) | 0 tokens |

If `p11check` warns that the PIN is locked or must be changed, sort that out first with **CryptoIDUserTools** (see [Troubleshooting](#6-troubleshooting)).

---

## 3. Fix Adobe Acrobat

### Why it fails

Acrobat on macOS runs in a **sandbox**. When you attach `/usr/local/lib/libcryptoida_pkcs11.dylib`, Acrobat can read the file you picked, but that file is only a wrapper. It then tries to load `/usr/local/lib/libcryptoida_pkcs11_f3.dylib`, which the sandbox blocks. Result: **"Could not load the PKCS#11 module."** Attaching `_f3` from `/usr/local/lib` directly fails for the same reason.

In our testing, Acrobat's sandbox did allow reading files **inside any `.app` bundle**. Its entitlements include a rule permitting reads of paths matching `*.app/*`. So the fix is to put a copy of the working driver inside a folder whose name ends in `.app`.

### Steps

1. Copy the driver into a `.app` folder in your home directory. No `sudo` needed.

   ```bash
   mkdir -p ~/Applications/DSCToken.app/Contents/Frameworks
   ```
   ```bash
   cp /usr/local/lib/libcryptoida_pkcs11_f3.dylib ~/Applications/DSCToken.app/Contents/Frameworks/
   ```

2. In Acrobat, go to **Acrobat → Settings → Signatures → Identities & Trusted Certificates → More…**
3. Select **Digital IDs → PKCS#11 Modules and Tokens** and click **Attach Module**.
4. In the Library Path box, type the full path. Replace `YOUR_USERNAME` with your Mac user name, which `whoami` in Terminal shows:

   ```
   /Users/YOUR_USERNAME/Applications/DSCToken.app/Contents/Frameworks/libcryptoida_pkcs11_f3.dylib
   ```

5. The module appears as **"Longmai PKCS11# Library"**. Expand it and select the token (**mToken CryptoID**).
6. The certificate list stays **empty until you log in**: Acrobat only lists IDs whose private key it can see. Click the first toolbar button. It asks for your **token PIN**, and after that your certificate appears. If a **"Add Digital ID"** wizard opens after the PIN, click **Cancel**. Your certificate is already on the token.
7. Select your certificate, click the **pencil icon → Use for Signing**.
8. To sign: open a PDF and go to **Tools → Use a certificate → Digitally Sign**.

**Don't delete `~/Applications/DSCToken.app`.** Acrobat loads the driver from there.

---

## 4. Fix the MCA portal / emBridge

The MCA portal signs through **eMudhra emBridge**, a local service at `https://localhost.emudhra.com:26769`. There are **three separate problems** stacked on top of each other, so you may have to fix all three.

### Problem A: the Token dropdown is empty

**Why.** On macOS, emBridge has a hard-coded list of driver locations, one per token brand. For mToken it loads:

```
/Applications/CryptoIDATools.app/Contents/MacOS/libcryptoid_pkcs11.dylib
```

That file is a **2019, Intel-only** driver that doesn't recognise the newer FIPS tokens, so emBridge logs `mToken CryptoID CSP => 0 slots found`.

You can confirm this in emBridge's log. It's readable only by root and the admin group, so use `sudo`:

```bash
sudo grep -a "mToken CryptoID CSP =>" /Library/eMudhra/emBridge/*.log | tail -5
```

> Note: emBridge doesn't use the macOS Keychain or CryptoTokenKit for mToken. The token never appears in `security list-smartcards`, and that's normal.

**Fix.** Put a working driver at that path. Step 2 below installs `p11shim` there, which also fixes Problem C, so most people only need that. (If you only want to fix Problem A, you could copy `libcryptoida_pkcs11_f3.dylib` over that file after backing it up. It contains an Intel slice, so emBridge can use it. But on our test token, signing still failed until Problem C was fixed too.)

### Problem B: "please ensure you have changed the password"

**Why.** New DSC tokens come with a **default PIN**, and the token refuses to sign until you change it.

**Fix.** Open **CryptoIDUserTools** (Blue or Purple, whichever shows your token), choose **Change PIN**, set a new PIN, then **quit CryptoIDUserTools completely (⌘Q)**. While it's open it can keep the token busy, and emBridge then sees 0 tokens.

### Problem C: the same error *after* changing the PIN

This is the one with no other known fix.

**Why.** emBridge logs in successfully, then asks the token to sign using raw RSA (`CKM_RSA_PKCS`) with a **49-byte** SHA-256 "DigestInfo" that **leaves out the NULL algorithm parameters**:

```
emBridge sends:   30 2f 30 0b 06 09 <SHA-256 OID> 04 20 <32-byte hash>          (49 bytes)
standard form:    30 31 30 0d 06 09 <SHA-256 OID> 05 00 04 20 <32-byte hash>    (51 bytes)
```

The mToken CryptoID FIPS token **rejects the 49-byte form** with card status `0x6986` ("command not allowed"). emBridge logs that as:

```
PKCSSign -> Exception => Method C_Sign returned 27014
```

27014 is 0x6986 in decimal. The portal then shows the misleading "please ensure you have changed the password" message.

You can prove this on your own token with `signtest` (see [Troubleshooting](#6-troubleshooting)). Test 2 (51 bytes) passes and test 3 (49 bytes) fails.

**Fix.** `p11shim` is a tiny PKCS#11 driver that emBridge loads instead. It passes **every call unchanged** to the real `libcryptoida_pkcs11_f3.dylib`, with one exception: when a raw-RSA signing request contains a DigestInfo missing its NULL parameters, it inserts the two missing bytes (`05 00`) before passing it on. The hash isn't touched, and the signature is still made by your token with your key.

### Steps

1. **Build and test**, if you haven't already ([Get the code](#get-the-code-and-build-the-tools)). All tests should pass.

2. **Read `install.sh`**, then run it from the repo folder. It first checks the mToken driver's code signature, then shows exactly what it will do and waits for you to type `y`. After that it asks for your Mac password:

   ```bash
   ./install.sh
   ```

   It copies the real mToken driver to `/Library/p11shim/`, a root-owned folder, so nothing else on your Mac can replace it. It then backs up the original emBridge driver to `libcryptoid_pkcs11.dylib.bak`, installs the shim and restarts emBridge.

3. **Quit your browser completely (⌘Q)** and reopen it. Each time emBridge restarts, the open portal page loses its connection to it, and the dropdown stays empty until you reload.

4. Log in to MCA, go to **Associate DSC**, fill in your details and open the popup.

5. **Wait 30–60 seconds** before opening the Token dropdown. emBridge checks a dozen token brands one after another, which is slow.

6. Choose **mToken CryptoID**, your certificate and your **new** PIN, then click **Register**.

7. If MCA says *"You have already registered your DSC… Do you want to update your DSC?"*, it means an older DSC is registered against your DIN/PAN. MCA allows only one DSC per DIN/PAN at a time, so **Update** replaces the old one, and the old token stops working for MCA filings.

### Check that the fix worked

```bash
grep -E "FIX|C_Sign rv|REFUSED" /Library/Logs/p11shim.log | tail -4
```

A successful signature looks like this:

```
   FIX: DigestInfo missing NULL params, rewrote 49 -> 51 bytes
C_Sign rv=0x0
```

### Lock down /usr/local/lib (recommended)

Some token installers make `/usr/local/lib` **writable by every user and program** on the Mac. Check yours:

```bash
ls -ld /usr/local/lib
```

If you see `drwxrwxrwx`, or an owner other than `root`, that's a security hole whether or not you use this fix. emBridge runs as root and loads other token drivers (ePass, HyperSecu and others) from that folder, so any program could replace one of them and gain full control of your Mac.

First check what's in the folder:

```bash
ls -la /usr/local/lib
```

If it holds only token driver files (`.dylib`), as on most Apple Silicon Macs, where Homebrew lives in `/opt/homebrew` instead, make it root-owned and read-only for everyone else:

```bash
sudo chown -R root:wheel /usr/local/lib
```
```bash
sudo chmod -R go-w /usr/local/lib
```

If you use an **Intel Mac with Homebrew in `/usr/local`**, don't run these. They would break Homebrew. Ask in an issue instead.

`p11shim` itself refuses to load a real driver that isn't root-owned and protected. That's why `install.sh` copies it to `/Library/p11shim/` instead of using it from `/usr/local/lib`.

---

## 5. Undo everything

- **MCA fix:** `./uninstall.sh` restores the original driver from the `.bak` file, removes `/Library/p11shim/` and restarts emBridge. It refuses to restore if the backup turns out to be the shim itself.
- **Acrobat:** in Acrobat's PKCS#11 settings, select the module and click **Detach Module**, then `rm -r ~/Applications/DSCToken.app`.
- **Log file:** `sudo rm /Library/Logs/p11shim.log`

---

## 6. Troubleshooting

### Read-only token check (no PIN)

```bash
./build/p11check /usr/local/lib/libcryptoida_pkcs11_f3.dylib
```

To test the way emBridge sees the token (in Intel mode):

```bash
arch -x86_64 ./build/p11check /usr/local/lib/libcryptoida_pkcs11_f3.dylib
```

### Signing test (asks for PIN)

```bash
./build/signtest /usr/local/lib/libcryptoida_pkcs11_f3.dylib
```

This signs a dummy hash three ways. The PIN is read with input hidden, and it's never printed or saved. **Each wrong PIN uses up one of the token's limited attempts.** Close the MCA popup first so the token isn't busy.

| Result | Meaning |
|---|---|
| Login `WRONG PIN` | wrong PIN. Stop before the token locks. |
| Login `PIN LOCKED` | unlock it with CryptoIDUserTools (needs the admin/SO PIN from your provider) |
| 1 and 2 OK, 3 FAILED | the emBridge format problem. Install `p11shim`. |
| all three FAILED | token or key problem. Contact your DSC provider. |

### emBridge's log

```bash
sudo sh -c 'grep -a -E "mToken|slots found|C_Sign|starting service" "$(ls -t /Library/eMudhra/emBridge/[0-9]*.log | head -1)" | tail -20'
```

| What you see | Meaning and what to do |
|---|---|
| `mToken CryptoID CSP => 0 slots found` | wrong or old driver at the CryptoIDATools path (Problem A), **or** CryptoIDUserTools is open. Quit it. |
| `1 slots found`, but dropdown empty | the answer reached the page too late, or emBridge restarted. Quit the browser, reopen, wait 30–60 s. |
| `starting service` right after a request | emBridge restarted mid-request. Quit and reopen the browser, log in again. |
| no new lines when you open the popup | the page isn't talking to emBridge. Quit and reopen the browser, log in again. |
| `C_Sign returned 27014` | Problem B (change the PIN) or Problem C (install `p11shim`) |

### The shim's log

```bash
tail -30 /Library/Logs/p11shim.log
```

It lists every PKCS#11 call emBridge makes, with return codes. It never records your PIN, not even its length. It's capped at 1 MB and restarts when full.

| What you see | Meaning and what to do |
|---|---|
| `REFUSED: ... is not owned by root` / `is writable by group or others` | the real driver copy isn't protected. Run `./install.sh` again. |
| `REFUSED: cannot stat ...` | the protected copy in `/Library/p11shim/` is missing. Run `./install.sh` again. |
| `dlopen of real driver failed` | the protected copy is damaged or for the wrong architecture. Reinstall the mToken driver package, then run `./install.sh` again. |
| `WARNING: session table full` | emBridge left many sessions open. Restart emBridge (`sudo launchctl kickstart -k system/com.eMudhra.emBridge.deamon`). |
| no log file at all | emBridge hasn't loaded the shim yet. Open the portal popup, or check that `install.sh` finished. |

### After updates

Reinstalling the mToken driver package can overwrite the shim, and so might a future emBridge update. If MCA signing breaks again, run `./install.sh` again. It never overwrites an existing backup, and it never backs up the shim as if it were the original. After reinstalling the mToken package, also check `/usr/local/lib` permissions again ([Lock down /usr/local/lib](#lock-down-usrlocallib-recommended)).

---

## 7. Technical details (why this works)

- emBridge is a .NET app built for **x86_64**. On Apple Silicon it runs under Rosetta, so any driver it loads **must contain an x86_64 slice**. `libcryptoida_pkcs11_f3.dylib` and `p11shim.dylib` are both universal.
- emBridge loads drivers through `Pkcs11Interop`, which calls `C_GetFunctionList`. `p11shim` gets the real driver's function table, copies it and replaces each pointer with a wrapper that logs and forwards. Only `C_Sign` behaves differently, and only for `CKM_RSA_PKCS` input that is a DigestInfo without NULL parameters.
- The rewrite is the same DER structure with explicit `NULL` parameters. RFC 8017 (PKCS #1 v2.2), section 9.2, note 1, lists the DigestInfo encodings for PKCS#1 v1.5 signatures, and all of them include the NULL parameters. So the rewritten form is the **standard** one, and the MCA portal accepted the resulting signature.
- The rewrite works for any hash OID (SHA-1, SHA-256, SHA-384, SHA-512). Input that is already standard, or isn't a DigestInfo at all, passes through unchanged (see `tools/fixtest.c`).
- The emBridge daemon runs as root (`launchd` label `com.eMudhra.emBridge.deamon`), so installing the shim needs `sudo`. Because the daemon is root, the shim checks before `dlopen` that the real driver and its folder are root-owned, aren't symlinks and aren't writable by group or others.
- Files are installed atomically, by copying to `*.new` and then renaming. A driver file that a running process has loaded is never overwritten in place.

---

## 8. GST and Income Tax portals

- **Income Tax** uses emBridge too, so the same fix should apply. That's untested.
- **GST** uses a different eMudhra utility, **emSigner**. It may hit similar problems, but this fix hasn't been tested with it. Pull requests welcome.

---

## 9. Security notes

- **Review before installing.** Anything in a signing chain should be read, not trusted blindly. The shim is in `src/p11shim.c`, about 250 lines. Build it yourself from source; don't install a binary someone sends you.
- **Your PIN passes through the shim** on its way to the token, as it would through any PKCS#11 driver. The shim forwards it unchanged and never logs, stores or inspects it, not even its length. It makes **no network connections**.
- **The log** (`/Library/Logs/p11shim.log`) is in a root-owned folder, readable by users of this Mac, and opened in a way that refuses symlinks. It records function names, return codes, mechanisms and lengths. For raw-RSA signing only, and only when the input looks like a DigestInfo, it also records the first 6 bytes of the request: a fixed algorithm header, not your document. To turn logging off, build with `-DSHIM_LOG=0` (edit the `cc` line in `build.sh`).
- **The installer checks the vendor driver's code signature** (Developer ID, team `4M7RX53G2N` on the tested driver) before copying it, and checks it again on the protected copy.
- **The vendor driver may write its own log.** It contains the strings `/var/tmp/` and `mk_log.log`, so it may write a log there as root. That's outside this fix's control and wasn't confirmed. If it matters to you, check for `/var/tmp/mk_log.log`.
- **`p11check` prints your token's label**, which can include the certificate holder's name. Remove it before pasting output into a public issue.
- **The real driver is loaded from a root-only location** (`/Library/p11shim/`). The shim refuses to load it if anyone other than root could have replaced it.
- **Lock down `/usr/local/lib`** if your token installer left it world-writable ([how](#lock-down-usrlocallib-recommended)).
- **Never share your DSC PIN** with anyone, including support people and AI assistants. Anyone with your token and PIN can sign legally binding documents as you.

---

## Reporting upstream

The proper fix belongs with the vendors. See [`docs/vendor-bug-report.md`](docs/vendor-bug-report.md) for a ready-to-send report for eMudhra and Longmai.

## License

MIT. See [LICENSE](LICENSE).
