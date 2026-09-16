// The entry point, kept out of the module so that the tool's parts can be
// imported by a test without bringing a `main` with them.
//
// The arguments are read here rather than by `CommonOptionsParser`, and the
// compilation database is built directly. That parser reports failure as an
// `llvm::Error`, and handling one needs `toString` or `consumeError` -- names
// the module wrapper does not carry, and an `Error` that is neither handled
// nor consumed aborts. Reading a command line of this shape is thirty lines,
// and this way the tool asks the wrapper for nothing it has not got.
import demodulizer;
import libtooling;
import std;

namespace {

void say_how() {
  llvm::errs()
      << "demodulizer [--into=DIR] [--suffix=.hpp] [--spell NAME=<include>]\n"
         "            SOURCE... -- COMPILER ARGUMENT...\n"
         "\n"
         "Each SOURCE is a module interface unit. What follows `--` is the\n"
         "command it would be compiled with; the first word of it is the\n"
         "compiler's name and is not passed on.\n";
}

}  // namespace

int main(int argc, char** argv) {
  demod::how plan;
  std::vector<std::string> sources;
  std::vector<std::string> spelled;
  std::vector<std::string> compiled_with;
  bool past_the_dashes = false;

  for (int at = 1; at < argc; ++at) {
    const std::string one(argv[at]);
    if (past_the_dashes) {
      compiled_with.push_back(one);
    } else if (one == "--") {
      past_the_dashes = true;
    } else if (one.starts_with("--into=")) {
      plan.into = one.substr(std::string_view("--into=").size());
    } else if (one.starts_with("--suffix=")) {
      plan.suffix = one.substr(std::string_view("--suffix=").size());
    } else if (one.starts_with("--spell=")) {
      spelled.push_back(one.substr(std::string_view("--spell=").size()));
    } else if (one == "--spell" && at + 1 < argc) {
      spelled.emplace_back(argv[++at]);
    } else if (one == "--help" || one == "-h") {
      say_how();
      return 0;
    } else if (one.starts_with("-")) {
      llvm::errs() << "unknown option: " << one << "\n";
      say_how();
      return 2;
    } else {
      sources.push_back(one);
    }
  }

  if (sources.empty() || !past_the_dashes) {
    say_how();
    return 2;
  }
  plan.spelled_as = demod::spellings_from(spelled);

  // The first word after `--` is what the compiler is called, which the
  // database does not want: it supplies its own.
  if (!compiled_with.empty()) compiled_with.erase(compiled_with.begin());

  clang::tooling::FixedCompilationDatabase told(".", compiled_with);
  clang::tooling::ClangTool tool(told, sources);
  const int ran = tool.run(
      std::make_unique<demod::factory>(std::move(plan)).get());
  // A header that compiles is not yet a header that links, and the difference
  // only shows with a second translation unit. Said here instead.
  if (ran != 0) return 1;
  return demod::complaints() != 0 ? 2 : 0;
}
