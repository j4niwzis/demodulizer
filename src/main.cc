// The entry point, kept out of the module so that the tool's parts can be
// imported by a test without bringing a `main` with them.
import demodulizer;
import libtooling;
import std;

static llvm::cl::OptionCategory category("demodulizer");
static llvm::cl::opt<std::string> into(
    "into", llvm::cl::desc("where the headers are written"),
    llvm::cl::init("."), llvm::cl::cat(category));
static llvm::cl::opt<std::string> suffix(
    "suffix", llvm::cl::desc("what they are called"), llvm::cl::init(".hpp"),
    llvm::cl::cat(category));
static llvm::cl::list<std::string> spelled(
    "spell", llvm::cl::desc("module=<include>, for modules not ours"),
    llvm::cl::cat(category));

int main(int argc, const char** argv) {
  auto parsed = clang::tooling::CommonOptionsParser::create(argc, argv, category);
  if (!parsed) {
    llvm::errs() << llvm::toString(parsed.takeError()) << "\n";
    return 1;
  }
  clang::tooling::ClangTool tool(parsed->getCompilations(),
                                 parsed->getSourcePathList());
  demod::how plan{.into = into, .suffix = suffix,
                  .spelled_as = demod::spellings_from(spelled)};
  const int ran = tool.run(std::make_unique<demod::factory>(std::move(plan)).get());
  // A header that compiles is not yet a header that links, and the difference
  // only shows with a second translation unit. Said here instead.
  return ran != 0 ? ran : (demod::complaints() != 0 ? 2 : 0);
}
