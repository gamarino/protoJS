#!/usr/bin/env bash
# Build protoJS .deb package for the current machine (Debian/Ubuntu).
# Run from the protoJS project root. Requires: build/protojs binary, dpkg-deb.
# The generated .deb checks for the "protocore" package (protoCore .deb from CPack).

set -e
cd "$(dirname "$0")/.."
: "${VERSION:=0.1.0}"
: "${MAINTAINER:=protoJS <protojs@example.com>}"

if [ ! -f build/protojs ]; then
    echo "ERROR: build/protojs not found. Build protoJS first: cmake -B build -S . && cmake --build build --target protojs" >&2
    exit 1
fi

mkdir -p protoJS_staging/DEBIAN protoJS_staging/usr/bin
cp build/protojs protoJS_staging/usr/bin/protojs
chmod 755 protoJS_staging/usr/bin/protojs
sed -e "s/\${VERSION}/$VERSION/g" -e "s/\${MAINTAINER}/$MAINTAINER/g" \
    packaging/templates/linux/control.template > protoJS_staging/DEBIAN/control
cp packaging/templates/linux/preinst.template protoJS_staging/DEBIAN/preinst
chmod 755 protoJS_staging/DEBIAN/preinst
dpkg-deb --build protoJS_staging "protoJS_${VERSION}_amd64.deb"
echo "Built protoJS_${VERSION}_amd64.deb. Install with: sudo dpkg -i protoJS_${VERSION}_amd64.deb"
echo "Ensure protoCore is installed first (package name: protocore)."
