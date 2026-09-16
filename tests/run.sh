#!/bin/sh
# What the tool has to get right, checked rather than asserted.
#
# Each of these was wrong once: the include list counted the whole standard
# library, `<algorithm>` went missing because only a template used it, and a
# definition that would collide at a link went unmentioned because one
# translation unit never shows it.
set -eu

CXX=${CXX:-clang++-22}
TOOL=${TOOL:-./demodulizer}
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

say() { printf '%s\n' "$*"; }
died() { printf 'FAILED: %s\n' "$*" >&2; exit 1; }

# ---- the `std` module, which `import std;` needs before anything else ----
root=$(dirname "$(dirname "$(command -v "$CXX")")")
for one in ${STD_CPPM:-} \ "$root/share/libc++/v1/std.cppm" \
           "$root/lib/llvm-22/share/libc++/v1/std.cppm" \
           /usr/lib/llvm-22/share/libc++/v1/std.cppm; do
  [ -f "$one" ] && stdcppm=$one && break
done
[ -n "${stdcppm:-}" ] || died "std.cppm not found; libc++ modules are needed"
say "std module: $stdcppm"
$CXX -std=c++23 -stdlib=libc++ -Wno-reserved-module-identifier \
  --precompile -x c++-module -o "$work/std.pcm" "$stdcppm"

mods="-fmodule-file=std=$work/std.pcm"
$CXX -std=c++23 -stdlib=libc++ $mods --precompile -x c++-module \
  -o "$work/fixture.dep.pcm" "$here/fixture_dep.cc"
mods="$mods -fmodule-file=fixture.dep=$work/fixture.dep.pcm"

# ---- the units that must convert ----
mkdir -p "$work/hdr"
"$TOOL" --into="$work/hdr" "$here/fixture_dep.cc" "$here/fixture_ok.cc" -- \
  "$CXX" -std=c++23 -stdlib=libc++ -x c++-module $mods

for want in fixture_dep.hpp fixture_ok.hpp; do
  [ -f "$work/hdr/$want" ] || died "$want was not written"
done

# The import became an include, named by the same rule.
grep -q '#include "fixture_dep.hpp"' "$work/hdr/fixture_ok.hpp" \
  || died "the import of fixture.dep did not become an include"

# Only a template uses `std::copy_n`, and the header still has to say so.
grep -q '#include <algorithm>' "$work/hdr/fixture_ok.hpp" \
  || died "<algorithm> is missing, which only a template asked for"
grep -q '#include <vector>' "$work/hdr/fixture_ok.hpp" \
  || died "<vector> is missing"

# The whole standard library is in the translation unit and must not be in the
# header. Nine or ten is right for this fixture; forty means the walk went
# into what it imported.
count=$(grep -c '^#include <' "$work/hdr/fixture_ok.hpp")
say "includes in fixture_ok.hpp: $count"
[ "$count" -lt 20 ] || died "$count includes: the walk went into the import"

# And the same for an import the preprocessor never read: it has no
# declaration to be found by, and leaving it is a header that says `import`.
awk '/^#if FIXTURE_THE_OTHER_WAY$/{f=1;next} f&&/^#endif$/{exit} f{print}' \
  "$work/hdr/fixture_ok.hpp" | grep -q '#include "fixture_dep.hpp"' \
  || died "the import in the branch that was not taken was not answered"

# A module keeps its macros; a header hands them over unless they are taken
# back.
grep -q '^#undef FIXTURE_WIDTH' "$work/hdr/fixture_ok.hpp" \
  || died "the macro was not taken back"

# Nothing of the module grammar may survive.
for forbidden in '^export ' '^module;' '^import ' '^export module'; do
  ! grep -q "$forbidden" "$work/hdr/fixture_ok.hpp" \
    || died "the header still says $forbidden"
done

# ---- and the headers have to compile, and link twice ----
cat > "$work/one.cc" <<'EOF'
#include "fixture_ok.hpp"
int how_many(const char* text) {
  return static_cast<int>(fixture::taken_apart(text).pieces.size());
}
EOF
cat > "$work/two.cc" <<'EOF'
#include "fixture_ok.hpp"
#include <cstdio>
int how_many(const char* text);
int main() {
  std::printf("%d\n", how_many("a,b,c"));
  return 0;
}
EOF
$CXX -std=c++23 -stdlib=libc++ -I"$work/hdr" "$work/one.cc" "$work/two.cc" \
  -o "$work/both"
got=$("$work/both")
[ "$got" = "3" ] || died "the program built from the headers said '$got', not 3"
say "two translation units linked, and the program is right"

# ---- and what must not pass ----
set +e
out=$("$TOOL" --into="$work/hdr" "$here/fixture_collides.cc" -- \
  "$CXX" -std=c++23 -stdlib=libc++ -x c++-module $mods 2>&1)
code=$?
set -e
[ "$code" -eq 2 ] || died "a colliding unit was accepted (exit $code)"
for name in collides counter; do
  printf '%s' "$out" | grep -q "fixture::$name" \
    || died "$name was not named as a collision"
done
for name in fine also_fine fine_too a_template kept also_kept hidden \
            thing in_class; do
  ! printf '%s' "$out" | grep -q "fixture::$name" \
    || died "$name was named as a collision and is not one"
done
say "the colliding unit was refused, and nothing else was"

say "all of it passed"
