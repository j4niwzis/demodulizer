demodulizer turns a C++20 module interface unit back into a header.

The authored source stays a module. That is the path everybody uses, and it
should pay nothing for a fallback: no `SCAN_EXPORT` on every declaration, no
`#ifdef` at the top of every file. What comes out of here is a build artifact
-- generated, compiled in CI, attached to a release -- and never edited by
hand. The duplication is a property of a derived file, not a cost.

    demodulizer --into=include --spell 'boost.pfr=<boost/pfr.hpp>' \
      src/*.cc -- clang++ -std=c++23 -x c++-module -fmodule-file=std=std.pcm

What it does to each unit:

  * removes the `export` keyword, and the module declaration
  * turns `import a.b;` into `#include "a_b.hpp"`, named by the same rule
    unless `--spell` says otherwise
  * puts `#undef` for every macro the file defined at the end of the header
  * replaces `import std;` with the standard headers the file actually needs

The text is copied verbatim. Nothing is printed back out of the AST: a
pretty-printer is not faithful on heavy templates and would quietly lose a
`requires` clause or a default argument. The AST is asked for positions --
where `export` is, where an `import` ends -- and for the one thing only it can
answer.

That one thing is the include list, and it is the reason this is a libtooling
program rather than a script. `import std;` says nothing about which parts of
the standard library a file uses, and a header has to say. Every declaration
the file refers to is looked up, the file that declares it is found, and that
file is named the way an include names it. Three things make it more than a
lookup:

  * only references made *from* the file are counted. The whole standard
    library is in the translation unit -- that is what the import means -- and
    walking into it would record everything it refers to as a dependency.
  * a dependent call in an uninstantiated template is not a reference to
    anything yet, so the candidates the lookup found are what name the header.
    Without this a header misses exactly what only templates use.
  * libc++ declares things in the small headers behind the name -- `find` is in
    `<__algorithm/find.h>` -- and with `import std;` many declarations come
    from the sources the module is assembled from, which are on no include
    path at all. Both are folded to the public header, by the library's own
    arrangement rather than by guessing.

It also says what would go wrong at a link rather than at a compile, and
exits non-zero when it finds any. A module may define a function at namespace
scope once and be done -- the definition is attached to the module and nobody
else has it. A header hands the same definition to every translation unit that
includes it, and the linker then has two. One translation unit never shows it:
the header compiles, a program built from it runs, and the second translation
unit is where it breaks. Templates, `constexpr` and `consteval` functions,
members defined inside their class, and anything `= default` or `= delete` are
inline already and are left alone -- which took two passes to get right, since
clang does not mark a defaulted member inline even though it is, and the first
version complained about twenty-seven of them.

`DEMOD_EXPLAIN=1` prints what was seen and what each file was named.

The macros matter more than they look. A module does not hand its macros to
whoever imports it, so nothing outside a unit can be using one -- which makes
the `#undef` at the end safe by construction rather than by checking. As a
header the same file would hand all of them over, and one unit of the library
this was written for defines five hundred and forty-two.

The check that matters is not whether the tables here are complete: it is
whether the generated headers compile. On the first full run over that
library, they did not -- and the error was in the library rather than here. A
default template argument was given twice, once at a forward declaration and
once at the definition, which is ill-formed and which a module interface unit
accepts. Nothing but a header build would have found it.


## Licence

This tool is licensed under the GNU Affero General Public License, version 3
or later. The file it writes is not.

What comes out is the file that went in, copied through: the `export` keyword
taken off, the module declaration dropped, imports turned into includes, and a
banner, a `#pragma once` and a list of `#include` and `#undef` lines added.
None of that carries anything of this program's own into the result -- a
generator that rewrites its input is not an author of it -- so the headers are
covered by whatever covers the sources they were made from, and nothing here
reaches into them. A GPL library stays a GPL library, section 13 and all the
rest of this licence included.
