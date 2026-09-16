// Something for the other fixture to import, so that `import a.b;` becoming
// `#include "a_b.hpp"` is exercised rather than assumed.
export module fixture.dep;
import std;

export namespace fixture {

struct held {
  std::string_view text;
  held() = default;
  held(const held&) = default;
  held& operator=(const held&) = default;
};

}  // namespace fixture
