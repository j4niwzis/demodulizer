// A module interface unit, turned back into a header.
//
// The authored source stays a module: it is the path everybody uses, and it
// should pay nothing for a fallback. What comes out of here is a build
// artifact -- generated, diffed in CI, attached to a release -- and never
// edited by hand.
//
// The text is copied verbatim. Nothing is printed back from the AST: a
// pretty-printer is not faithful on the templates this is meant for, and would
// quietly lose a `requires` clause or a default argument. The AST is asked for
// positions and for one thing it alone can answer -- which standard headers
// the file actually needs, now that `import std;` has stopped saying.
module;

export module demodulizer;
import libtooling;
import std;

namespace demod {

// What a module name becomes when it is a file. `scan.tre` -> `scan_tre.hpp`,
// which is the rule the sources already follow; a project that names them
// otherwise passes the mapping in.
export std::string header_for(std::string_view module_name,
                              std::string_view suffix = ".hpp") {
  std::string made(module_name);
  for (char& one : made) {
    if (one == '.') one = '_';
  }
  made += suffix;
  return made;
}

// What a path's last part is, what holds it, and what it is called without
// its extension. `llvm/Support/Path.h` has these and the module wrapper does
// not carry them; they are three lines each and no dependency at all.
std::string_view leaf_of(std::string_view path) {
  const std::size_t at = path.rfind('/');
  return at == std::string_view::npos ? path : path.substr(at + 1);
}

std::string_view holding_of(std::string_view path) {
  const std::size_t at = path.rfind('/');
  return at == std::string_view::npos ? std::string_view{} : path.substr(0, at);
}

std::string_view without_extension(std::string_view leaf) {
  const std::size_t at = leaf.rfind('.');
  return (at == std::string_view::npos || at == 0) ? leaf : leaf.substr(0, at);
}

// Every `#define` the file makes, so that every one of them can be taken back
// at the end of the header.
//
// A module does not hand its macros to whoever imports it, so nothing outside
// this file can be using one of these -- the undef at the end is safe by
// construction rather than by checking. As a header, though, the file would
// hand all of them over, and five hundred of them at that.
export class macros_defined : public clang::PPCallbacks {
 public:
  macros_defined(const clang::SourceManager& sm, std::vector<std::string>& into)
      : sm_(sm), into_(into) {}

  void MacroDefined(const clang::Token& name,
                    const clang::MacroDirective* md) override {
    const clang::MacroInfo* mi = md ? md->getMacroInfo() : nullptr;
    if (mi == nullptr || mi->isBuiltinMacro() || mi->isUsedForHeaderGuard()) {
      return;
    }
    const clang::SourceLocation at = mi->getDefinitionLoc();
    if (!at.isValid()) return;
    // Not `isInMainFile`: the compiler's own predefines sit in a buffer it
    // counts as the main file, and a header that undefined `__cplusplus`
    // would be a rather memorable bug.
    if (sm_.getFileID(sm_.getExpansionLoc(at)) != sm_.getMainFileID()) return;
    into_.push_back(name.getIdentifierInfo()->getName().str());
  }

 private:
  const clang::SourceManager& sm_;
  std::vector<std::string>& into_;
};

// The standard headers the file turns out to need.
//
// This is the whole reason the tool reads an AST rather than the text. With
// `import std;` the source says nothing about which parts of the standard
// library it uses, and a header has to say. Every declaration the file refers
// to is looked up, its definition's file is taken, and the file is named the
// way an include names it.
export class headers_used {
 public:
  headers_used(const clang::SourceManager& sm, clang::HeaderSearch& search)
      : sm_(sm), search_(search) {}

  void saw(const clang::Decl* d) {
    if (d == nullptr) return;
    // A declaration that belongs to a module is named by that module's
    // import, and that import is answered where it stands -- under whatever
    // condition it stands under. Spelling its header out here as well would
    // put it in the file unconditionally, which is the whole of what this
    // was getting wrong. Only what `import std;` brings is named by headers.
    if (const clang::Module* from = d->getOwningModule()) {
      if (from->getTopLevelModuleName() != "std") return;
    }
    const clang::SourceLocation at = d->getLocation();
    if (!at.isValid()) return;
    const clang::FileID id = sm_.getFileID(sm_.getExpansionLoc(at));
    if (id == sm_.getMainFileID()) return;
    clang::OptionalFileEntryRef file = sm_.getFileEntryRefForID(id);
    if (!file) return;
    std::string named = name_of(*file);
    if (std::getenv("DEMOD_EXPLAIN") != nullptr) {
      llvm::errs() << "  file " << file->getName() << " -> "
                   << (named.empty() ? "(dropped)" : named) << "\n";
    }
    if (!named.empty()) found_.insert(std::move(named));
  }

  // What a user would have written.
  //
  // A declaration lives in the header that defines it, and in libc++ that is
  // one of the small ones behind the name: `find` is in `<__algorithm/find.h>`
  // and nobody includes that. The first component after the library root is
  // the public header it belongs to -- `__algorithm/…` is `<algorithm>`,
  // `__string/…` is `<string>` -- which is the library's own arrangement and
  // not a guess, though the handful of names that do not follow it are listed
  // rather than derived.
  static std::string public_name(std::string_view spelled) {
    static const std::map<std::string, std::string, std::less<>> named_apart = {
        {"__stddef_max_align_t.h", "cstddef"}, {"__stddef_size_t.h", "cstddef"},
        {"__stddef_ptrdiff_t.h", "cstddef"},   {"__stddef_null.h", "cstddef"},
        {"__stddef_nullptr_t.h", "cstddef"},   {"__stdarg_va_list.h", "cstdarg"},
    };
    // Support that no program includes on purpose: it arrives with whatever
    // else was asked for.
    static const std::set<std::string, std::less<>> kept_quiet = {
        "__config", "__configuration", "__assert", "__verbose_abort",
        "__availability", "__undef_macros", "__config_site", "__debug_utils",
        // The C library's own arrangement, reached through a C header that is
        // named in its own right.
        "bits", "sys", "asm", "asm-generic", "linux", "gnu"};
    static const std::map<std::string, std::string, std::less<>> renamed = {
        {"math", "cmath"}, {"locale_dir", "locale"}};
    std::string_view rest = spelled;
    // An include is never spelled as an absolute path. Where that is all the
    // header search could offer, the file is not reachable by name at all and
    // has to be recognised some other way.
    if (rest.starts_with("/")) return {};
    while (rest.starts_with("__cxx03/")) rest.remove_prefix(8);
    const std::size_t slash = rest.find('/');
    if (slash == std::string_view::npos) {
      auto told = named_apart.find(rest);
      if (told != named_apart.end()) return told->second;
      if (rest.starts_with("__")) return {};
      return std::string(rest);
    }
    std::string_view head = rest.substr(0, slash);
    if (kept_quiet.count(head) != 0) return {};
    // The sources the `std` module is assembled from, named after the public
    // header one for one. They are reachable by name here as well as by path,
    // so they have to be recognised in both places.
    if (head == "std" && rest.ends_with(".inc")) {
      std::string_view leaf = rest.substr(slash + 1);
      leaf.remove_suffix(4);
      return std::string(leaf);
    }
    // A forward declaration is where a type is named without being defined,
    // and something only declared -- a member never touched in this file --
    // refers to nothing else. So these cannot be skipped. The name is the
    // file's, not the directory's, and the few that do not match the header
    // they belong to are listed.
    if (head == "__fwd") {
      static const std::map<std::string, std::string, std::less<>> belongs_to = {
          {"pair", "utility"},       {"get", "utility"},
          {"subrange", "ranges"},    {"bit_reference", "bitset"},
          {"hash", "functional"},    {"byte", "cstddef"},
      };
      std::string_view leaf = rest.substr(slash + 1);
      if (leaf.ends_with(".h")) leaf.remove_suffix(2);
      auto instead = belongs_to.find(leaf);
      return instead != belongs_to.end() ? instead->second : std::string(leaf);
    }
    // Folding a path down to its first part is how this library is laid out
    // and nobody else's: `boost/pfr/core.hpp` is included by that name and by
    // no other. Only what announces itself as internal is folded.
    if (!head.starts_with("__")) return std::string(rest);
    head.remove_prefix(2);
    auto instead = renamed.find(head);
    return instead != renamed.end() ? instead->second : std::string(head);
  }

  [[nodiscard]] const std::set<std::string>& found() const { return found_; }

 private:
  // The shortest spelling that would find this file again. Asked of the
  // header search itself where it can answer, so that a file reached through
  // one of several search paths is named by the path it was reached through.
  std::string name_of(clang::FileEntryRef file) const {
    bool is_angled = false;
    const std::string spelled = search_.suggestPathToFileForDiagnostics(
        file, /*MainFile=*/llvm::StringRef(), &is_angled);
    // Asked for by `import std;`, a good deal of the standard library is
    // declared in the sources the `std` module is assembled from, and those
    // are on no include path -- there is nothing to shorten the name to. They
    // are named after the public header one for one (`std/algorithm.inc` is
    // `<algorithm>`), which is the arrangement that makes them, so the file's
    // own name answers where the search cannot.
    std::string named = spelled.empty() ? std::string() : public_name(spelled);
    // Where the search could not shorten the path -- which happens whenever
    // the flags the tool was given do not name the include directory the file
    // was found through -- the library root is still recognisable in the path
    // itself, and everything after it is the name.
    if (named.empty()) named = public_name(after_library_root(file.getName()));
    if (named.empty()) named = from_module_source(file.getName());
    if (named.empty()) return {};
    return "<" + named + ">";
  }

  static std::string after_library_root(llvm::StringRef path) {
    for (llvm::StringRef root : {"/c++/v1/", "/include/c++/"}) {
      const std::size_t at = path.find(root);
      if (at != llvm::StringRef::npos) {
        return path.substr(at + root.size()).str();
      }
    }
    return {};
  }

  static std::string from_module_source(llvm::StringRef path) {
    const std::string_view whole(path.data(), path.size());
    const std::string_view leaf = leaf_of(whole);
    if (!leaf.ends_with(".inc")) return {};
    if (leaf_of(holding_of(whole)) != "std") return {};
    return std::string(leaf.substr(0, leaf.size() - 4));
  }

  const clang::SourceManager& sm_;
  clang::HeaderSearch& search_;
  std::set<std::string> found_;
};

}  // namespace demod

namespace demod {

// An import in a branch the preprocessor did not take.
//
// There is no declaration for it -- the AST has nothing the compiler never
// read -- so the text is where it still is, and the text is where it has to
// be answered. A header may not say `import` under any condition, and what it
// should say instead follows from the name alone.
export template <class saying>
void say_what_was_skipped(std::string& body, const saying& include_for) {
  std::string made;
  std::size_t at = 0;
  while (at <= body.size()) {
    const std::size_t eol = body.find('\n', at);
    const std::size_t end = (eol == std::string::npos) ? body.size() : eol;
    std::string_view line(body.data() + at, end - at);
    std::string_view rest = line;
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) {
      rest.remove_prefix(1);
    }
    if (rest.starts_with("export ")) rest.remove_prefix(7);
    bool answered = false;
    if (rest.starts_with("import ") && rest.ends_with(";")) {
      std::string_view name = rest.substr(7, rest.size() - 8);
      while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
      const bool a_name = !name.empty() &&
                          name.find_first_not_of(
                              "abcdefghijklmnopqrstuvwxyz"
                              "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.") ==
                              std::string_view::npos;
      if (a_name) {
        made += include_for(std::string(name));
        answered = true;
      }
    }
    if (!answered) made.append(line);
    if (eol == std::string::npos) break;
    made += '\n';
    at = eol + 1;
  }
  body.swap(made);
}

// What the file says and where it says it.
// Where an `import a.b.c;` really ends. The declaration's own range stops at
// the first identifier -- the rest of the name is kept apart from it -- and
// what has to go is the whole statement, semicolon and all.
clang::SourceLocation through_semicolon(clang::SourceLocation from,
                                        const clang::SourceManager& sm,
                                        const clang::ASTContext& ctx) {
  std::optional<clang::Token> next =
      clang::Lexer::findNextToken(from, sm, ctx.getLangOpts());
  while (next && !next->is(clang::tok::semi)) {
    next = clang::Lexer::findNextToken(next->getLocation(), sm,
                                       ctx.getLangOpts());
  }
  return next ? next->getLocation() : from;
}

// What would go wrong at a link rather than at a compile.
//
// A module may define a function at namespace scope once and be done: the
// definition is attached to the module and nobody else has it. A header hands
// the same definition to every translation unit that includes it, and the
// linker then has two. One translation unit never shows it -- the header
// compiles, a program built from it runs -- and the second one does.
//
// Templates, `constexpr` and `consteval` functions, and members defined inside
// their class are all inline already and are left alone. What is counted is a
// plain definition with external linkage, which is the only thing that
// collides.
int complained = 0;

export int complaints() { return complained; }

export struct seen {
  std::vector<clang::SourceRange> exports;   // the `export` keyword, alone
  std::vector<clang::SourceRange> imports;   // `import x;`, whole
  std::vector<std::string> imported_names;   // and what each of them named
};

export class walk : public clang::RecursiveASTVisitor<walk> {
 public:
  walk(clang::ASTContext& ctx, seen& into, headers_used& used)
      : sm_(ctx.getSourceManager()), into_(into), used_(used) {}

  // Only what this file writes; a declaration reached through an import is
  // somebody else's text and is left alone.
  [[nodiscard]] bool here(clang::SourceLocation at) const {
    return at.isValid() && sm_.isInMainFile(sm_.getExpansionLoc(at));
  }

  // The whole of the standard library is in this translation unit -- that is
  // what `import std;` means -- and walking into it would record everything it
  // refers to as something this file needs. Only what this file writes is
  // walked, which is also most of the running time saved.
  bool TraverseDecl(clang::Decl* d) {
    if (d == nullptr) return true;
    if (!llvm::isa<clang::TranslationUnitDecl>(d) && !here(d->getBeginLoc()) &&
        !here(d->getLocation())) {
      return true;
    }
    return clang::RecursiveASTVisitor<walk>::TraverseDecl(d);
  }

  bool VisitFunctionDecl(clang::FunctionDecl* d) {
    if (!here(d->getLocation()) || !d->isThisDeclarationADefinition()) {
      return true;
    }
    if (d->getTemplatedKind() != clang::FunctionDecl::TK_NonTemplate) {
      return true;
    }
    if (d->isInlined() || d->isConstexpr()) return true;
    // Asked for by `= default` or refused by `= delete`: neither is a
    // definition anybody links to twice, and clang does not mark them inline
    // even though they are.
    if (d->isDefaulted() || d->isDeleted()) return true;
    // Defined where it was declared, inside its class, which is inline by
    // rule. Only a member defined outside its class needs the keyword.
    if (const auto* member = llvm::dyn_cast<clang::CXXMethodDecl>(d)) {
      if (member->getLexicalDeclContext() == member->getDeclContext()) {
        return true;
      }
    }
    if (d->getLinkageInternal() == clang::Linkage::Internal) return true;
    say_it_would_collide(d, "a function defined at namespace scope and not "
                            "inline");
    return true;
  }

  bool VisitVarDecl(clang::VarDecl* d) {
    if (!here(d->getLocation()) || !d->isFileVarDecl()) return true;
    if (d->isThisDeclarationADefinition() != clang::VarDecl::Definition) {
      return true;
    }
    if (d->isInline() || d->isConstexpr()) return true;
    if (d->getLinkageInternal() == clang::Linkage::Internal) return true;
    if (d->getType().isConstQualified()) return true;
    say_it_would_collide(d, "a variable defined at namespace scope and not "
                            "inline");
    return true;
  }

  bool VisitExportDecl(clang::ExportDecl* d) {
    if (here(d->getExportLoc())) {
      into_.exports.push_back(clang::SourceRange(d->getExportLoc()));
    }
    return true;
  }

  bool VisitImportDecl(clang::ImportDecl* d) {
    if (!here(d->getLocation())) return true;
    clang::Module* m = d->getImportedModule();
    if (m == nullptr) return true;
    into_.imports.push_back(d->getSourceRange());
    into_.imported_names.push_back(m->getTopLevelModuleName() == "std"
                                       ? std::string("std")
                                       : m->getFullModuleName());
    return true;
  }

  // Everything the file refers to, so that the headers it needs can be named.
  bool VisitDeclRefExpr(clang::DeclRefExpr* e) {
    return here(e->getBeginLoc()) ? note(e->getDecl()) : true;
  }
  bool VisitMemberExpr(clang::MemberExpr* e) {
    return here(e->getBeginLoc()) ? note(e->getMemberDecl()) : true;
  }
  bool VisitCXXConstructExpr(clang::CXXConstructExpr* e) {
    return here(e->getBeginLoc()) ? note(e->getConstructor()) : true;
  }
  // In a template that has not been instantiated a dependent call is not a
  // reference to anything yet -- `std::copy_n(first, n, out)` is a lookup
  // still to be resolved -- so the candidates it found are what name the
  // header. Without this the header misses exactly the declarations that only
  // a template uses, which is most of them here.
  bool VisitUnresolvedLookupExpr(clang::UnresolvedLookupExpr* e) {
    return VisitOverloadExpr(e);
  }
  bool VisitUnresolvedMemberExpr(clang::UnresolvedMemberExpr* e) {
    return VisitOverloadExpr(e);
  }
  bool VisitOverloadExpr(clang::OverloadExpr* e) {
    if (!here(e->getBeginLoc())) return true;
    for (clang::NamedDecl* one : e->decls()) note(one);
    return true;
  }
  bool VisitCallExpr(clang::CallExpr* e) {
    return here(e->getBeginLoc()) ? note(e->getCalleeDecl()) : true;
  }

  bool VisitTypeLoc(clang::TypeLoc t) {
    if (!here(t.getBeginLoc())) return true;
    const clang::Type* ty = t.getTypePtr();
    if (ty == nullptr) return true;
    if (const clang::TagDecl* tag = ty->getAsTagDecl()) note(tag);
    if (const auto* spec = ty->getAs<clang::TemplateSpecializationType>()) {
      note(spec->getTemplateName().getAsTemplateDecl());
    }
    return true;
  }

 private:
  void say_it_would_collide(const clang::NamedDecl* d, const char* what) {
    ++complained;
    llvm::errs() << sm_.getFilename(d->getLocation()) << ":"
                 << sm_.getSpellingLineNumber(d->getLocation()) << ": "
                 << d->getQualifiedNameAsString() << " is " << what
                 << ", so two translation units that include the header would"
                    " each have one\n";
  }

  // An instantiation is recorded where it was instantiated, which is here --
  // `std::vector<std::string_view>` as a member is written down as belonging
  // to this file, and asking where it came from would answer this file. The
  // template it came from is what has a header.
  static const clang::Decl* where_it_came_from(const clang::Decl* d) {
    if (const auto* one = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(d)) {
      return one->getSpecializedTemplate();
    }
    if (const auto* one = llvm::dyn_cast<clang::VarTemplateSpecializationDecl>(d)) {
      return one->getSpecializedTemplate();
    }
    if (const auto* one = llvm::dyn_cast<clang::FunctionDecl>(d)) {
      if (clang::FunctionTemplateDecl* from = one->getPrimaryTemplate()) {
        return from;
      }
    }
    return d;
  }

  bool note(const clang::Decl* d) {
    if (d == nullptr) return true;
    d = where_it_came_from(d);
    if (std::getenv("DEMOD_EXPLAIN") != nullptr) {
      if (const auto* named = llvm::dyn_cast<clang::NamedDecl>(d)) {
        llvm::errs() << "  saw " << named->getQualifiedNameAsString() << "\n";
      }
    }
    used_.saw(d->getCanonicalDecl());
    return true;
  }

  const clang::SourceManager& sm_;
  seen& into_;
  headers_used& used_;
};

}  // namespace demod

namespace demod {

// Take a line out of the text, from the start of the line the offset is on
// through the newline that ends it. Used for the module declaration, which is
// a statement no header may carry.
std::size_t drop_line_at(std::string& text, std::size_t at) {
  const std::size_t from = text.rfind('\n', at);
  const std::size_t begin = (from == std::string::npos) ? 0 : from + 1;
  std::size_t end = text.find('\n', at);
  end = (end == std::string::npos) ? text.size() : end + 1;
  text.erase(begin, end - begin);
  return begin;
}

// `export module x;`, `module x;` and a bare `module;` are the three things a
// header cannot say. There is exactly one module declaration in a unit, so
// this is a search and not a loop over matches.
export void drop_module_declaration(std::string& text) {
  for (std::string_view spelling : {"export module ", "module "}) {
    const std::size_t at = text.find(spelling);
    if (at == std::string::npos) continue;
    const bool at_line_start = (at == 0) || text[at - 1] == '\n';
    if (!at_line_start) continue;
    drop_line_at(text, at);
    return;
  }
  const std::size_t fragment = text.find("module;");
  if (fragment != std::string::npos &&
      (fragment == 0 || text[fragment - 1] == '\n')) {
    drop_line_at(text, fragment);
  }
}

export struct how {
  std::string into = ".";                          // where headers are written
  std::string suffix = ".hpp";                     // what they are called
  std::map<std::string, std::string> spelled_as;   // module name -> include
};

// One module interface unit in, one header out.
export class to_a_header : public clang::ASTConsumer {
 public:
  to_a_header(clang::CompilerInstance& ci, how plan,
              std::vector<std::string>& macros)
      : ci_(ci), plan_(std::move(plan)), macros_(macros) {}

  void HandleTranslationUnit(clang::ASTContext& ctx) override {
    clang::SourceManager& sm = ctx.getSourceManager();
    seen said;
    headers_used used(sm, ci_.getPreprocessor().getHeaderSearchInfo());
    walk(ctx, said, used).TraverseDecl(ctx.getTranslationUnitDecl());

    // What an import becomes, said where the import was.
    //
    // Not gathered at the top, which is what this did first and what made it
    // wrong: an import inside a `#if` the preprocessor skipped is not in the
    // AST at all, so it stayed in the text as an `import` -- a thing no
    // header may contain -- while the include it should have become was put
    // at the top unconditionally, where the other branch would read it. Put
    // in its place, the condition around it is kept without this having to
    // understand it.
    const auto include_for = [&](const std::string& module_name) {
      if (module_name == "std") {
        std::string made;
        for (const std::string& one : used.found()) {
          made += "#include " + one + "\n";
        }
        if (!made.empty()) made.pop_back();
        return made;
      }
      auto told = plan_.spelled_as.find(module_name);
      if (told != plan_.spelled_as.end()) return "#include " + told->second;
      return "#include \"" + header_for(module_name, plan_.suffix) + "\"";
    };

    // What comes out, as offsets into the file it came from.
    //
    // clang has a rewriter for this and the module wrapper does not carry it,
    // which costs nothing here: all that happens is that a few runs of
    // characters leave one buffer, and an offset with a length says that as
    // plainly as a source range does.
    std::vector<std::tuple<unsigned, unsigned, std::string>> taken_out;
    const auto token_length = [&](clang::SourceLocation at) {
      return clang::Lexer::MeasureTokenLength(at, sm, ctx.getLangOpts());
    };
    // The `export` keyword goes and the whitespace after it stays: what is
    // left is `namespace scan {`, which is what a header says.
    for (const clang::SourceRange& one : said.exports) {
      taken_out.emplace_back(sm.getFileOffset(one.getBegin()),
                             token_length(one.getBegin()), std::string());
    }
    // An import goes entirely, semicolon and all; what it asked for is said
    // again at the top, because a header says what it needs before it needs
    // it.
    for (std::size_t at = 0; at < said.imports.size(); ++at) {
      const clang::SourceRange& one = said.imports[at];
      const clang::SourceLocation semi =
          through_semicolon(one.getEnd(), sm, ctx);
      const unsigned from = sm.getFileOffset(one.getBegin());
      const unsigned upto = sm.getFileOffset(semi) + token_length(semi);
      if (upto > from) {
        taken_out.emplace_back(from, upto - from,
                               include_for(said.imported_names[at]));
      }
    }

    std::string body = sm.getBufferData(sm.getMainFileID()).str();
    // Back to front, so that taking one run out does not move the next.
    std::ranges::sort(taken_out, std::ranges::greater{},
                      [](const auto& one) { return std::get<0>(one); });
    for (const auto& [at, length, with] : taken_out) {
      if (at + length > body.size()) continue;
      body.erase(at, length);
      if (!with.empty()) body.insert(at, with);
    }
    drop_module_declaration(body);
    // And an import the preprocessor never reached, which is in no AST: the
    // one thing a header may not carry, left behind by a condition that was
    // false. The name is all that is needed to say what it should have been.
    say_what_was_skipped(body, include_for);

    std::string made = "// Generated from the module interface unit of the "
                       "same name. Do not edit.\n#pragma once\n";
    made += body;
    if (!macros_.empty()) {
      made += "\n// A module keeps its macros to itself and a header does not, "
              "so they are\n// taken back here rather than handed to whoever "
              "includes this.\n";
      for (const std::string& one : macros_) made += "#undef " + one + "\n";
    }
    write_out(sm, made);
  }

 private:
  void write_out(const clang::SourceManager& sm, const std::string& made) const {
    clang::OptionalFileEntryRef from =
        sm.getFileEntryRefForID(sm.getMainFileID());
    if (!from) return;
    const llvm::StringRef path = from->getName();
    const std::string_view whole(path.data(), path.size());
    const std::string stem(without_extension(leaf_of(whole)));
    std::string where = plan_.into + "/" + stem + plan_.suffix;
    std::error_code failed;
    llvm::raw_fd_ostream to(where, failed);
    if (failed) {
      llvm::errs() << "cannot write " << where << ": " << failed.message()
                   << "\n";
      return;
    }
    to << made;
    llvm::errs() << "wrote " << where << "\n";
  }

  clang::CompilerInstance& ci_;
  how plan_;
  std::vector<std::string>& macros_;
};

}  // namespace demod

namespace demod {

export class action : public clang::ASTFrontendAction {
 public:
  explicit action(how plan) : plan_(std::move(plan)) {}

  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
      clang::CompilerInstance& ci, llvm::StringRef) override {
    // The macros have to be watched as the file is read; by the time there is
    // an AST the directives are gone.
    ci.getPreprocessor().addPPCallbacks(
        std::make_unique<macros_defined>(ci.getSourceManager(), macros_));
    return std::make_unique<to_a_header>(ci, plan_, macros_);
  }

 private:
  how plan_;
  std::vector<std::string> macros_;
};

export class factory : public clang::tooling::FrontendActionFactory {
 public:
  explicit factory(how plan) : plan_(std::move(plan)) {}
  std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<action>(plan_);
  }

 private:
  how plan_;
};

// `name=<spelling>`, repeatable: how a module that is not one of ours is
// named by an include. `boost.pfr=<boost/pfr.hpp>` is the one this project
// needs.
export std::map<std::string, std::string> spellings_from(
    const std::vector<std::string>& told) {
  std::map<std::string, std::string> made;
  for (const std::string& one : told) {
    const std::size_t at = one.find('=');
    if (at == std::string::npos || at == 0) continue;
    made.emplace(one.substr(0, at), one.substr(at + 1));
  }
  return made;
}

}  // namespace demod
