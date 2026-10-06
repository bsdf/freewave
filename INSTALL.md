# INSTALL

## DEBIAN AND UBUNTU
packages for Debian 13, Debian unstable, Ubuntu 24.04 and Ubuntu 26.04 (amd64)
are built on the openSUSE Build Service. add the repository key:

```
sudo curl -fsSL -o /usr/share/keyrings/home_bsdf_freewave.asc \
  https://download.opensuse.org/repositories/home:/bsdf:/freewave/Debian_13/Release.key
```

then create `/etc/apt/sources.list.d/freewave.sources`, replacing `Debian_13`
with `Debian_Unstable`, `xUbuntu_24.04` or `xUbuntu_26.04` as needed:

```
Types: deb
URIs: https://download.opensuse.org/repositories/home:/bsdf:/freewave/Debian_13/
Suites: /
Components:
Architectures: amd64
Signed-By: /usr/share/keyrings/home_bsdf_freewave.asc
```

and install:

```
sudo apt update
sudo apt install freewave
```

## FLATPAK

```
cmake -B build .
cmake --build build --target flatpak
flatpak run org.xeyes.Freewave
```

requires `flatpak-builder` and the KDE SDK:

```
flatpak install flathub org.kde.Platform//6.10 org.kde.Sdk//6.10
```

## FROM SOURCE
requirements depend on the backend.

### MPD backend (default)
 * C++23 (GCC 14+)
 * Qt 6.4 or newer
 * spdlog (fetched automatically)
 * fmt (bundled with spdlog)
 * libmpdclient2

### Subsonic/OpenSubsonic backend (`-DENABLE_SUBSONIC=ON`)
 * all of the above except libmpdclient2
 * GStreamer 1.0 (`gstreamer1.0-plugins-base`, `gstreamer1.0-plugins-good`)

### building

```
cmake -B build .
cmake --build build
```

tests, sanitizer builds and the style guardrail are described in
[HACKING.md](HACKING.md).
