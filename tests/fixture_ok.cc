// A module that a header can be made of, written to exercise what was wrong
// the first three times.
export module fixture.ok;
import std;
import fixture.dep;

// Defined here and used here, and taken back at the end of the header: a
// module keeps its macros and a header hands them over.
#define FIXTURE_WIDTH 8

export namespace fixture {

// `std::copy_n` inside a template that nobody instantiates. The call is
// dependent, so it refers to nothing yet -- the candidates the lookup found
// are the only thing that can name `<algorithm>`.
template <class type, std::size_t count>
constexpr void take(const type (&from)[count], type* into) {
  std::copy_n(from, count, into);
}

// An aggregate, a view, a vector: three more headers to be named.
struct answer {
  std::vector<std::string_view> pieces;
  held first;
  answer() = default;
  answer(answer&&) = default;
  answer& operator=(answer&&) = default;
};

inline answer taken_apart(std::string_view text) {
  answer made;
  made.pieces.reserve(FIXTURE_WIDTH);
  for (auto part : std::views::split(text, ',')) {
    made.pieces.emplace_back(part.data(), part.size());
  }
  if (!made.pieces.empty()) made.first.text = made.pieces.front();
  return made;
}

}  // namespace fixture
