#!/bin/sh
# Extracts ghcr.io/loveretro/tg5040-toolchain (linux/amd64) into a directory
# with the registry API, for hosts without Docker (orbs, plain CI runners).
#
#   engine/cedar-hwdec/scripts/fetch-toolchain.sh /tmp/tc-rootfs
#
# Then: TOOLCHAIN_ROOT=/tmp/tc-rootfs SYSROOT=/tmp/tc-rootfs/opt/.../libc \
#       engine/cedar-hwdec/scripts/build-spike.sh
#
# The image's gcc wrapper is a symlink to a missing /opt/x86_64-... prefix
# (it only works inside the container). The script creates that prefix as
# a copy of /opt/aarch64-nextui-linux-gnu so the compiler can resolve its
# sysroot, exactly as the CI container's /opt/x86_64-... directory does.

set -eu

DEST=${1:?usage: fetch-toolchain.sh DEST_DIR}
IMAGE=${IMAGE:-loveretro/tg5040-toolchain:latest}
REGISTRY=${REGISTRY:-ghcr.io}

mkdir -p "$DEST"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

repo=${IMAGE%%:*}
tag=${IMAGE#*:}
scope="repository:$repo:pull"
token=$(curl -fsSL "https://$REGISTRY/token?scope=$scope" | python3 -c \
        'import json,sys; print(json.load(sys.stdin)["token"])')

manifest=$(curl -fsSL -H "Authorization: Bearer $token" -H \
    'Accept: application/vnd.oci.image.index.v1+json' \
    "https://$REGISTRY/v2/$repo/manifests/$tag")
digest=$(printf '%s' "$manifest" | python3 -c '
import json, sys
index = json.load(sys.stdin)
for m in index.get("manifests", []):
    p = m.get("platform", {})
    if p.get("architecture") == "amd64" and p.get("os") == "linux":
        print(m["digest"])
        break
else:
    sys.exit("no linux/amd64 manifest")
')
layers=$(curl -fsSL -H "Authorization: Bearer $token" -H \
    'Accept: application/vnd.oci.image.manifest.v1+json' \
    "https://$REGISTRY/v2/$repo/manifests/$digest" |
    python3 -c 'import json,sys
for l in json.load(sys.stdin)["layers"]:
    print(l["digest"], l["size"])')

printf '%s\n' "$layers" | while read -r layer size; do
    echo "fetch-toolchain: $layer ($size bytes)"
    curl -fsSL -H "Authorization: Bearer $token" -o "$work/layer.tgz" \
        "https://$REGISTRY/v2/$repo/blobs/$layer"
    # Whiteouts from upper layers apply last-wins, as docker does.
    tar -xzf "$work/layer.tgz" -C "$DEST" --exclude='dev/*' 2>/dev/null || true
done

triple=aarch64-nextui-linux-gnu
root="$DEST/opt/$triple"
hostroot="$DEST/opt/x86_64-$triple"
chmod -R u+w "$DEST/opt"
if [ ! -d "$hostroot" ]; then
    mkdir -p "$hostroot"
    cp -a "$root/bin" "$root/lib" "$root/libexec" "$root/share" \
        "$hostroot/" 2>/dev/null || true
fi
if [ ! -e "$hostroot/bin/$triple-gcc" ]; then
    ln -sf "$triple-gcc-8.3.0" "$hostroot/bin/$triple-gcc"
fi
"$hostroot/bin/$triple-gcc" -print-sysroot
echo "fetch-toolchain: TOOLCHAIN_ROOT=$DEST SYSROOT=$root/libc"
