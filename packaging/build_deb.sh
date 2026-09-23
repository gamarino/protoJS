#!/usr/bin/env bash
# Build protoJS .deb package for the current machine (Debian/Ubuntu).
# Run from the protoJS project root. Requires: a built protojs binary, dpkg-deb.
# The generated .deb checks for the "protocore" package (protoCore .deb from CPack)
# in the range [2.0.0, 3.0.0) and for the libprotoCore.so.2 soname.

set -e
cd "$(dirname "$0")/.."
: "${VERSION:=0.1.0}"
: "${MAINTAINER:=Gustavo Marino <gamarino@gmail.com>}"
: "${PROTOJS_BINARY:=build_release/protojs}"
: "${PROTOCORE_SONAME:=libprotoCore.so.2}"
: "${OUTDIR:=build_release}"

if [ ! -f "$PROTOJS_BINARY" ]; then
    echo "ERROR: $PROTOJS_BINARY not found. Build protoJS first:" >&2
    echo "  cmake -B build_release -S . -DCMAKE_BUILD_TYPE=Release && cmake --build build_release" >&2
    echo "  (no -j: parallel protoJS builds hang some machines)" >&2
    exit 1
fi

# The package's preinst requires protoCore's soname; refuse to build a package
# whose binary was linked against a different one.
if ! objdump -p "$PROTOJS_BINARY" | grep -q "NEEDED *$PROTOCORE_SONAME"; then
    echo "ERROR: $PROTOJS_BINARY is not linked against $PROTOCORE_SONAME." >&2
    objdump -p "$PROTOJS_BINARY" | grep "NEEDED *libprotoCore" >&2 || true
    echo "Rebuild protoJS against protoCore 2.x." >&2
    exit 1
fi

# Staging and output live under $OUTDIR (a build directory), so this script no
# longer writes protoJS_staging/ and protoJS_<version>_amd64.deb into the
# repository root next to the committed artefacts of the same name.
STAGING="$OUTDIR/protoJS_staging"
rm -rf "$STAGING"
mkdir -p "$STAGING/DEBIAN" "$STAGING/usr/bin"
cp "$PROTOJS_BINARY" "$STAGING/usr/bin/protojs"
chmod 755 "$STAGING/usr/bin/protojs"
sed -e "s/\${VERSION}/$VERSION/g" -e "s/\${MAINTAINER}/$MAINTAINER/g" \
    packaging/templates/linux/control.template > "$STAGING/DEBIAN/control"
cp packaging/templates/linux/preinst.template "$STAGING/DEBIAN/preinst"
chmod 755 "$STAGING/DEBIAN/preinst"
dpkg-deb --build "$STAGING" "$OUTDIR/protoJS_${VERSION}_amd64.deb"
echo "Built $OUTDIR/protoJS_${VERSION}_amd64.deb."
echo "Install with: sudo dpkg -i $OUTDIR/protoJS_${VERSION}_amd64.deb"
echo "Ensure protoCore 2.x is installed first (package name: protocore)."
echo "Note: do not install this package and the CPack-built \"protojs\" package"
echo "at the same time; both claim /usr/bin/protojs."
