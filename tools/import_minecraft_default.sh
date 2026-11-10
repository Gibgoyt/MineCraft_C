#!/bin/sh
# Makes textures_local/: a fresh copy of textures/, with every texture that textures/import_minecraft_default.yaml takes
# from Minecraft's default pack replaced by Mojang's art. See textures/README.md, "Minecraft's default textures".
#
# textures_local/ is git-ignored and must never be committed: the repository is public and the art is Mojang's. The
# next configure sees it and builds the game from it; delete it to go back to Pixel Perfection.
#
# usage: tools/import_minecraft_default.sh [texture_pack tool] [unpacked pack]
#   defaults: build/debug/bin/tools/texture_pack   /tmp/mc_c_assets/minecraft_default (tools/fetch_minecraft_default.sh)
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
tool="${1:-$root/build/debug/bin/tools/texture_pack}"
pack="${2:-/tmp/mc_c_assets/minecraft_default}"
local_tree="$root/textures_local"

if [ ! -x "$tool" ]; then
    echo "import_minecraft_default: no $tool; build it with" >&2
    echo "    tools/test-or-build.sh cmake --build --preset debug --target mc_tool_texture_pack" >&2
    exit 69
fi
# Refuse to write anything the repository would publish.
if ! git -C "$root" check-ignore -q "$local_tree/"; then
    echo "import_minecraft_default: textures_local/ is not ignored by .gitignore; refusing" >&2
    exit 1
fi
if [ -n "$(git -C "$root" ls-files textures_local)" ]; then
    echo "import_minecraft_default: something under textures_local/ is tracked; remove it from the index first" >&2
    exit 1
fi

# A fresh copy each time, so nothing stale survives a change to the map.
staging="$root/textures_local.new"
rm -rf "$staging"
cp -a "$root/textures" "$staging"
"$tool" import-default --pack "$pack" --map "$root/textures/import_minecraft_default.yaml" --textures "$staging"
# vector.yaml's light thresholds were set on Pixel Perfection's pixels. Where Minecraft's art is darker, the copy's row is
# lowered here, so that the same pixels still shine; texture_vector refuses a row no pixel passes.
#   glow_lichen: Minecraft's brightest specks have a luma of 172, under the 180 set for Pixel Perfection.
sed -i 's/^\(  - { texture: blocks\/glow_lichen, emissive: 7, min_luma: \)180 }$/\1170 }/' "$staging/vector.yaml"
grep -q 'blocks/glow_lichen, emissive: 7, min_luma: 170 }' "$staging/vector.yaml" ||
    { echo "import_minecraft_default: vector.yaml's glow_lichen row has changed; update this script" >&2; exit 1; }
rm -rf "$local_tree"
mv "$staging" "$local_tree"
echo "import_minecraft_default: textures_local/ is ready; configure again to build with it"
