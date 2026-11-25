#!/bin/bash
# Restore the original mToken driver that install.sh backed up, remove the shim's files and restart emBridge.
# If there is no usable backup, it offers to remove the shim anyway (then reinstall the mToken driver package).
set -euo pipefail

TARGET="/Applications/CryptoIDATools.app/Contents/MacOS/libcryptoid_pkcs11.dylib"
BACKUP="$TARGET.bak"
SAFE_DIR="/Library/p11shim"
SERVICE="system/com.eMudhra.emBridge.deamon"

# Matches every p11shim version (the marker, or the log banner used by early builds).
is_shim() { [[ -f "$1" ]] && grep -aqE "p11shim-marker-v1|p11shim loaded" "$1"; }

if [[ -f "$BACKUP" ]] && ! is_shim "$BACKUP"; then
  # Restore atomically: copy to a new file, then rename over the shim.
  sudo cp -p "$BACKUP" "$TARGET.new"
  sudo mv -f "$TARGET.new" "$TARGET"
  echo "restored original driver (backup kept at $BACKUP; delete it with: sudo rm \"$BACKUP\")"
elif is_shim "$TARGET"; then
  if [[ -f "$BACKUP" ]]; then echo "The backup at $BACKUP is p11shim itself, not the original driver."
  else echo "No backup of the original driver was found."; fi
  read -r -p "Remove p11shim anyway? emBridge won't see mToken tokens until you reinstall the mToken driver package. [y/N] " ans
  [[ "$ans" == [yY]* ]] || { echo "Cancelled."; exit 0; }
  sudo rm -f "$TARGET"
  [[ -f "$BACKUP" ]] && sudo rm -f "$BACKUP"
  echo "removed p11shim"
else
  echo "p11shim is not installed at $TARGET. Nothing to restore."
fi

if [[ -d "$SAFE_DIR" ]]; then sudo rm -rf "$SAFE_DIR"; echo "removed $SAFE_DIR"; fi

if sudo launchctl kickstart -k "$SERVICE"; then
  echo "restarted emBridge"
else
  echo "WARNING: could not restart emBridge. Restart your Mac." >&2
fi
echo "Optional: delete the shim's log with:  sudo rm /Library/Logs/p11shim.log"
