# Let an AI coding agent set this up for you

If you use an AI coding agent on your Mac (Claude Code, Cursor, Codex, Copilot agent mode or similar), you can paste the prompt below and let it walk through this guide with you. It diagnoses the problem first and only makes the changes your Mac actually needs.

**Before you paste it:**

- Plug in your mToken DSC token, ideally directly into the Mac.
- Have your Mac login password ready. You'll type it yourself whenever a command needs `sudo`.
- **Never give the agent your DSC token PIN.** You type the PIN yourself, only into Acrobat, the MCA portal, CryptoIDUserTools or `signtest`. The prompt tells the agent this too.
- Expect to approve each command the agent wants to run. Read them before you approve.

---

## The prompt

Copy everything inside the box:

````text
Help me get my Longmai mToken CryptoID (FIPS) USB DSC token working on this Mac, for Adobe
Acrobat signing and/or the MCA portal (eMudhra emBridge), using the guide and code in this repo:
https://github.com/targaryen1212/mtoken-mac-dsc-fix

How to work:
- Clone the repo into a new folder and read README.md, src/p11shim.c, install.sh and uninstall.sh
  in full before doing anything else. Tell me in plain language what the shim does and what
  install.sh will change on my Mac.
- Diagnose before changing anything. Follow README sections 2 and 4 in order, and only apply the
  fixes my Mac actually needs. Show me the evidence (command output or log lines) for each
  conclusion.
- Explain each step in simple terms before you do it. I am not a developer.

Hard rules:
1. Never ask for, type, store or log my DSC token PIN. If a step needs the PIN, tell me where to
   type it myself (Acrobat, the MCA portal, CryptoIDUserTools, or the signtest program in my own
   terminal) and wait for me.
2. Never type my Mac password. For any command that needs sudo, show me the exact command, explain
   what it changes, and let me run it myself (or approve it so I can type the password).
3. Build the shim from source with ./build.sh. Don't download prebuilt binaries from anywhere.
   Run ./build.sh test and stop if any test fails.
4. Before install.sh runs, confirm that /usr/local/lib/libcryptoida_pkcs11_f3.dylib exists and
   passes `codesign --verify --strict`. If the team ID isn't 4M7RX53G2N, stop and ask me.
5. Back up anything before replacing it. Never delete or overwrite the .bak backup of the original
   driver.
6. Only run `sudo chown`/`chmod` on /usr/local/lib after showing me its contents (`ls -la`).
   Don't run them if Homebrew or anything other than token driver files lives there.
7. Read-only checks (ls, ioreg, codesign -dv, build/p11check, reading logs) are fine to run.
   Anything that changes system files needs my approval first.
8. If what you see doesn't match the guide (different file names, a different token, errors the
   guide doesn't mention), stop and explain instead of improvising a fix.
9. Don't open, click or follow links in my email or documents, and don't sign anything on my behalf.

Steps:
1. Check the token is detected: `ioreg -p IOUSB -w0 | grep -i mtoken`
2. Build and test: `./build.sh test`
3. Check the driver: `arch -x86_64 ./build/p11check /usr/local/lib/libcryptoida_pkcs11_f3.dylib`
   (don't paste the token label anywhere public, as it may contain my name)
4. If I want Acrobat: follow README section 3. Copying into ~/Applications/DSCToken.app needs no
   sudo. Then talk me through attaching the module in Acrobat's settings.
5. If I want MCA: check /usr/local/lib permissions (README "Lock down /usr/local/lib"), then have
   me run ./install.sh. Then tell me to quit and reopen my browser and try Associate DSC on MCA.
6. After I try, read /Library/Logs/p11shim.log (and, with my approval, emBridge's log) and tell me
   whether it worked. A successful signature shows "FIX: DigestInfo missing NULL params" followed
   by "C_Sign rv=0x0".
7. If the portal says "please ensure you have changed the password" and the shim log shows the
   signature failed, tell me to change my token PIN in CryptoIDUserTools (then quit that app) and
   retry.
8. At the end, summarise what was changed on my Mac and how to undo it (./uninstall.sh).
````

---

## Why the prompt is written this way

- **It reads before it acts.** The agent has to understand the code and explain it to you before anything is installed, so you know what you're agreeing to.
- **It diagnoses first.** Not every Mac needs every fix. Some only need the Acrobat step; others only need the PIN changed.
- **Your PIN and password stay with you.** An agent never needs your DSC PIN to set this up. Anyone with your token and PIN can sign legally binding documents as you.
- **It stops when things don't match.** A different token or driver version may need a different fix, and an agent improvising with `sudo` can do real damage.
