### protoJS Release Procedures

This document describes how to build the distribution packages for protoJS.

#### 1. Linux DEB (amd64)

**Staging Layout:**
```
protoJS_staging/
├── DEBIAN/
│   ├── control
│   └── preinst
└── usr/
    └── bin/
        └── protojs
```

**Build Commands:**
```bash
# 1. Prepare staging
mkdir -p protoJS_staging/DEBIAN
mkdir -p protoJS_staging/usr/bin
cp build/protojs protoJS_staging/usr/bin/

# 2. Copy templates and set permissions
cp packaging/templates/linux/control.template protoJS_staging/DEBIAN/control
cp packaging/templates/linux/preinst.template protoJS_staging/DEBIAN/preinst
chmod 755 protoJS_staging/DEBIAN/preinst

# 3. Build package
dpkg-deb --build protoJS_staging protoJS_0.1.0_amd64.deb
```

**Verification:**
```bash
# Install
sudo dpkg -i protoJS_0.1.0_amd64.deb
# Verify
dpkg -l protoJS
protojs --version
# Uninstall
sudo apt remove protoJS
```

---

#### 2. Linux RPM (x86_64)

**Build Commands:**
```bash
# 1. Setup rpmbuild tree
mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

# 2. Create source tarball
tar -czf ~/rpmbuild/SOURCES/protoJS-0.1.0.tar.gz protojs

# 3. Build using the spec file
rpmbuild -ba packaging/templates/linux/protoJS.spec.template --define "version 0.1.0" --define "release 1"
```

**Verification:**
```bash
# Install
sudo rpm -ivh ~/rpmbuild/RPMS/x86_64/protoJS-0.1.0-1.x86_64.rpm
# Verify
rpm -q protoJS
# Uninstall
sudo rpm -e protoJS
```

---

#### 3. Windows MSI (x64)

**Toolchain:** WiX Toolset v3.11+
**Justification:** WiX is the industry standard for creating professional MSI installers. It allows for declarative definition of components, registry searches (for dependencies), and proper uninstallation/PATH management.

**Build Commands:**
```powershell
# 1. Compile
candle.exe -arch x64 packaging/templates/windows/protoJS.wxs.template -o protoJS.wixobj
# 2. Link
light.exe protoJS.wixobj -o protoJS-0.1.0.msi
```

**Silent Install/Uninstall:**
```powershell
# Install
msiexec /i protoJS-0.1.0.msi /quiet /qn /norestart
# Uninstall
msiexec /x {PRODUCT-GUID} /quiet /qn /norestart
```

---

#### 4. macOS PKG (universal2)

**Build Commands:**
```bash
# 1. Create component PKG
pkgbuild --root ./staging \
         --identifier com.protoJS.pkg \
         --version 0.1.0 \
         --install-location /usr/local/bin \
         --scripts packaging/templates/macos \
         protoJS-core.pkg

# 2. Create product installer
productbuild --package protoJS-core.pkg \
             --identifier com.protoJS.installer \
             protoJS-0.1.0.pkg
```

**Signing & Notarization:**
```bash
# Sign
productsign --sign "Developer ID Installer: Your Team (ID)" protoJS-0.1.0.pkg protoJS-0.1.0-signed.pkg

# Notarize
xcrun altool --notarize-app --primary-bundle-id "com.protoJS.installer" --username "apple-id" --password "app-specific-password" --file protoJS-0.1.0-signed.pkg

# Staple
xcrun stapler staple protoJS-0.1.0-signed.pkg
```

**Verification:**
```bash
# Verify architecture
lipo -info /usr/local/bin/protojs
# Verify dependency resolution
otool -L /usr/local/bin/protojs
```
