#!/bin/sh
# Downloads one pinned Minecraft client jar from Mojang's own servers, as the official launcher does, checks its SHA-1,
# and unpacks only assets/minecraft/textures/ into a new, empty directory. See textures/README.md, "Minecraft's default
# textures", and plans/005-ahmed-bohemoth-initial-game-fixes-round-2-phase-09-default-textures.md.
#
# Mojang's textures are for use on this machine only. Nothing this writes may ever be committed: it lands under /tmp, and
# tools/import_minecraft_default.sh imports it into textures_local/, which git ignores.
#
# The jar is untrusted data. Nothing from it is ever run: the only command that opens it is unzip, which is told to take
# assets/minecraft/textures/ and nothing else, into a directory this script has just made empty.
#
# usage: tools/fetch_minecraft_default.sh [destination]   (default /tmp/mc_c_assets/minecraft_default)
set -eu

# The pin. Moving it means changing both lines here and pack.version and pack.client_sha1 in
# textures/import_minecraft_default.yaml, which the importer checks against the pin file written below.
MC_VERSION="26.3"
MC_CLIENT_SHA1="e877b6a07acd633fb3bb475002175cec036e7b87"
MC_MANIFEST_URL="https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"

destination="${1:-/tmp/mc_c_assets/minecraft_default}"
case "$destination" in
    /*) ;;
    *) echo "fetch_minecraft_default: the destination must be an absolute path" >&2; exit 64 ;;
esac
for tool in curl jq sha1sum unzip; do
    command -v "$tool" >/dev/null 2>&1 || { echo "fetch_minecraft_default: needs $tool" >&2; exit 69; }
done

# Downloads go to their own new directory, beside the destination and never inside the repository.
parent="$(dirname "$destination")"
mkdir -p "$parent"
download="$(mktemp -d "$parent/.download.XXXXXXXX")"
trap 'rm -rf "$download"' EXIT

fetch() { # url, file
    curl --fail --silent --show-error --location --proto '=https' --tlsv1.2 --max-time 600 -o "$2" "$1"
}

# 1. Mojang's list of versions names the version's own JSON and that file's SHA-1.
fetch "$MC_MANIFEST_URL" "$download/manifest.json"
version_url="$(jq -r --arg v "$MC_VERSION" '.versions[] | select(.id == $v) | .url' "$download/manifest.json")"
version_sha1="$(jq -r --arg v "$MC_VERSION" '.versions[] | select(.id == $v) | .sha1' "$download/manifest.json")"
case "$version_url" in
    https://piston-meta.mojang.com/*) ;;
    *) echo "fetch_minecraft_default: Mojang's list has no version $MC_VERSION on piston-meta" >&2; exit 1 ;;
esac

# 2. The version's JSON, checked against that SHA-1, names the client jar.
fetch "$version_url" "$download/version.json"
echo "$version_sha1  $download/version.json" | sha1sum --check --status ||
    { echo "fetch_minecraft_default: version $MC_VERSION's JSON does not match its SHA-1" >&2; exit 1; }
client_url="$(jq -r '.downloads.client.url' "$download/version.json")"
client_sha1="$(jq -r '.downloads.client.sha1' "$download/version.json")"
if [ "$client_sha1" != "$MC_CLIENT_SHA1" ]; then
    echo "fetch_minecraft_default: Mojang now lists client SHA-1 $client_sha1 for $MC_VERSION, not the pinned one" >&2
    exit 1
fi
case "$client_url" in
    https://piston-data.mojang.com/*) ;;
    *) echo "fetch_minecraft_default: the client jar is not on piston-data.mojang.com" >&2; exit 1 ;;
esac

# 3. The jar, checked against the pin.
fetch "$client_url" "$download/client.jar"
echo "$MC_CLIENT_SHA1  $download/client.jar" | sha1sum --check --status ||
    { echo "fetch_minecraft_default: the client jar does not match the pinned SHA-1" >&2; exit 1; }

# 4. Only the textures, into a new empty directory, then swapped in for the old one.
unpacked="$download/unpacked"
mkdir "$unpacked"
unzip -q -o "$download/client.jar" 'assets/minecraft/textures/*' -d "$unpacked"
# Anything but plain files and directories (a link, say) is refused: the importer would refuse it anyway.
if [ -n "$(find "$unpacked" ! -type f ! -type d -print -quit)" ]; then
    echo "fetch_minecraft_default: the jar's textures hold something that is not a file or a directory" >&2
    exit 1
fi
printf '%s %s\n' "$MC_VERSION" "$MC_CLIENT_SHA1" > "$unpacked/mc_c_pin.txt"
rm -rf "$destination"
mv "$unpacked" "$destination"
count="$(find "$destination/assets/minecraft/textures" -type f -name '*.png' | wc -l)"
echo "fetch_minecraft_default: Minecraft $MC_VERSION textures, $count PNG files, in $destination"
