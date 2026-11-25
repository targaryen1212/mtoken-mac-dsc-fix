#!/bin/bash
# Install p11shim as the mToken driver that emBridge loads.
# Read this script before running it. It asks for your Mac password (sudo).
#
# What it does:
#   1. copies the real mToken driver to /Library/p11shim/ (root-owned, so nothing else can replace it)
#   2. backs up the driver emBridge currently loads (only the genuine original, never the shim)
#   3. installs the shim in its place, atomically
#   4. restarts emBridge
set -euo pipefail
cd "$(dirname "$0")"

VENDOR_DRIVER="/usr/local/lib/libcryptoida_pkcs11_f3.dylib"
SAFE_DIR="/Library/p11shim"
SAFE_DRIVER="$SAFE_DIR/libcryptoida_pkcs11_f3.dylib"
TARGET="/Applications/CryptoIDATools.app/Contents/MacOS/libcryptoid_pkcs11.dylib"
BACKUP="$TARGET.bak"
SERVICE="system/com.eMudhra.emBridge.deamon"   # sic: eMudhra's own spelling
SHIM="build/p11shim.dylib"

[[ -f "$SHIM" ]] || { echo "Run ./build.sh first." >&2; exit 1; }
[[ -f "$VENDOR_DRIVER" ]] || { echo "mToken driver not found at $VENDOR_DRIVER. Install the mToken CryptoID macOS driver first." >&2; exit 1; }
[[ -d /Library/eMudhra/emBridge ]] || { echo "emBridge is not installed (/Library/eMudhra/emBridge missing)." >&2; exit 1; }
[[ -d "$(dirname "$TARGET")" ]] || { echo "$(dirname "$TARGET") not found. This fix expects CryptoIDATools.app from the mToken driver package." >&2; exit 1; }

# Matches every p11shim version (the marker, or the log banner used by early builds).
is_shim() { [[ -f "$1" ]] && grep -aqE "p11shim-marker-v1|p11shim loaded" "$1"; }

# The vendor driver is Developer ID signed by Longmai. The team ID below is what the tested driver carries;
# check yours with: codesign -dv /usr/local/lib/libcryptoida_pkcs11_f3.dylib
EXPECTED_TEAM="4M7RX53G2N"
team_of() { codesign -dv "$1" 2>&1 | sed -n 's/^TeamIdentifier=//p'; }

# Warn up front if the folder the vendor driver lives in could have been tampered with.
if [[ -d /usr/local/lib ]] && { [[ "$(stat -f %u /usr/local/lib)" != 0 ]] || (( 8#$(stat -f %Lp /usr/local/lib) & 8#022 )); }; then
  echo "WARNING: /usr/local/lib is not root-owned, or is writable by others, so its files could have been"
  echo "replaced. The driver's code signature is checked below before it is trusted. Afterwards, see"
  echo "'Lock down /usr/local/lib' in the README."
  echo
fi

if ! codesign --verify --strict "$VENDOR_DRIVER" 2>/dev/null; then
  echo "ERROR: $VENDOR_DRIVER has a missing or broken code signature. Reinstall the mToken driver package." >&2
  exit 1
fi
VENDOR_TEAM="$(team_of "$VENDOR_DRIVER")"
if [[ "$VENDOR_TEAM" != "$EXPECTED_TEAM" ]]; then
  echo "WARNING: the driver is signed by team '$VENDOR_TEAM', not the expected '$EXPECTED_TEAM'."
  echo "That's fine only if your DSC provider shipped a newer driver from a different signer."
  read -r -p "Type 'trust' to continue anyway: " t
  [[ "$t" == "trust" ]] || { echo "Cancelled."; exit 1; }
fi
echo "Vendor driver signature OK (team $VENDOR_TEAM)."
echo

echo "This will:"
echo "  1. copy      $VENDOR_DRIVER"
echo "       to      $SAFE_DRIVER   (root-owned)"
if [[ -f "$BACKUP" ]]; then
  echo "  2. keep the existing backup $BACKUP"
elif is_shim "$TARGET"; then
  echo "  2. skip the backup (the current file is already p11shim, not the original)"
else
  echo "  2. back up   $TARGET  ->  $BACKUP"
fi
echo "  3. install   $SHIM  ->  $TARGET"
echo "  4. restart   the emBridge service"
read -r -p "Continue? [y/N] " ans
[[ "$ans" == [yY]* ]] || { echo "Cancelled."; exit 0; }

# 1. Real driver to a root-owned location.
sudo mkdir -p "$SAFE_DIR"
sudo chown root:wheel "$SAFE_DIR"
sudo chmod 755 "$SAFE_DIR"
sudo cp "$VENDOR_DRIVER" "$SAFE_DRIVER.new"
sudo chown root:wheel "$SAFE_DRIVER.new"
sudo chmod 644 "$SAFE_DRIVER.new"
# Re-verify the root-owned copy itself, so nothing can swap the file between the check above and the copy.
if ! codesign --verify --strict "$SAFE_DRIVER.new" 2>/dev/null || [[ "$(team_of "$SAFE_DRIVER.new")" != "$VENDOR_TEAM" ]]; then
  sudo rm -f "$SAFE_DRIVER.new"
  echo "ERROR: the copied driver failed signature verification. Nothing was installed." >&2
  exit 1
fi
sudo mv -f "$SAFE_DRIVER.new" "$SAFE_DRIVER"
echo "copied real driver to $SAFE_DRIVER"

# 2. Back up the genuine original only.
if [[ ! -f "$BACKUP" && -f "$TARGET" ]] && ! is_shim "$TARGET"; then
  sudo cp -p "$TARGET" "$BACKUP"
  echo "backed up original driver"
fi

# 3. Install atomically: write a new file, then rename over the old one (never overwrite a loaded file in place).
sudo cp "$SHIM" "$TARGET.new"
sudo chown root:admin "$TARGET.new"
sudo chmod 755 "$TARGET.new"
sudo mv -f "$TARGET.new" "$TARGET"
echo "installed p11shim"

# 4. Restart emBridge.
if sudo launchctl kickstart -k "$SERVICE"; then
  echo "restarted emBridge"
else
  echo "WARNING: could not restart emBridge. Restart your Mac before retrying." >&2
fi

echo
echo "Done. Now QUIT your browser completely (Cmd+Q), reopen it, log in to the portal again and retry."
echo "Signing log: $( [[ -f /Library/Logs/p11shim.log ]] && echo /Library/Logs/p11shim.log || echo '/Library/Logs/p11shim.log (created on first use)')"
