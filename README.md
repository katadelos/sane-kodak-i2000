# sane-kodak-i2000

Standalone SANE backend for Kodak i2000 document scanners.

```sh
git submodule update --init
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

```sh
deb=kodak_i2000-04.14.00.00-1.amd64.deb
tar -xzf LinuxSoftware_i2000_v4.14.x86_64.deb.tar.gz -C build "./$deb"
ar -p "build/$deb" data.tar.gz | tar -xzOf - ./opt/kodak/kds_i2000/lib/devicemanager.so > build/devicemanager.so
build/kodak-i2000-fw extract build/devicemanager.so i2400-v4.14.kfw
```

## Install for Homebrew SANE

```sh
prefix="$(brew --prefix)"
conf="$prefix/etc/sane.d"
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$prefix" && cmake --build build
install -d "$prefix/lib/sane" "$conf/dll.d" "$prefix/share/sane/kodak_i2000"
install -m 755 build/libsane-kodak_i2000.1.so "$prefix/lib/sane/"
install -m 644 kodak_i2000 "$conf/dll.d/"
install -m 600 i2400-v4.14.kfw "$prefix/share/sane/kodak_i2000/"
test -f "$conf/kodak_i2000.conf" || cp backend/kodak_i2000.conf.in "$conf/kodak_i2000.conf"
grep -Eq '^[[:space:]]*firmware-source[[:space:]]' "$conf/kodak_i2000.conf" ||
  printf '\nfirmware-source %s\n' "$prefix/share/sane/kodak_i2000/i2400-v4.14.kfw" >> "$conf/kodak_i2000.conf"
scanimage -L
```
