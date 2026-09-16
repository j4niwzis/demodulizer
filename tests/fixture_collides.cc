// What must not pass. Both of these are one definition per translation unit
// that includes the header, and the linker gets one from each.
export module fixture.collides;
import std;

export namespace fixture {

int collides(int x) { return x + 1; }
int counter = 0;

// None of these collide, and none of them may be complained about.
inline int fine(int x) { return x; }
constexpr int also_fine(int x) { return x * 2; }
consteval int fine_too(int x) { return x + 2; }
template <class type> type a_template(type x) { return x; }
constexpr int kept = 3;
const int also_kept = 4;

struct thing {
  thing() = default;
  ~thing() = default;
  int in_class() { return 1; }
};

}  // namespace fixture

// Not exported, because a name with internal linkage cannot be -- and still
// in the file, so the check has to see it and leave it alone.
namespace fixture {
static int hidden = 5;
}  // namespace fixture
