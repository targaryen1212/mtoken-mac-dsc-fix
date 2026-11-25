#!/bin/bash
# Build p11shim and the diagnostic tools. Needs the Xcode Command Line Tools (run: xcode-select --install).
#   ./build.sh        build everything into ./build
#   ./build.sh test   also build and run the unit tests (no token or sudo needed)
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

if ! command -v cc >/dev/null; then
  echo "No C compiler found. Install the Xcode Command Line Tools:  xcode-select --install" >&2
  exit 1
fi

# Universal binary: emBridge runs as x86_64 (under Rosetta on Apple Silicon); other apps may be arm64.
cc -arch x86_64 -arch arm64 -dynamiclib -O2 -Wall -Wno-unused-parameter -Wno-unused-result \
   -o build/p11shim.dylib src/p11shim.c
if ! codesign -s - -f build/p11shim.dylib 2>build/codesign.err; then
  echo "WARNING: ad-hoc code signing failed:" >&2; cat build/codesign.err >&2
fi
rm -f build/codesign.err
echo "built build/p11shim.dylib"

cc -arch x86_64 -arch arm64 -O2 -o build/p11check tools/p11check.c
cc -arch x86_64 -arch arm64 -O2 -o build/signtest tools/signtest.c
echo "built build/p11check and build/signtest"

if [[ "${1:-}" == "test" ]]; then
  cc -O2 -w -o build/fixtest tools/fixtest.c
  ./build/fixtest
fi
