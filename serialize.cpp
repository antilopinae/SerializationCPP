#include "serializable.hpp"

using namespace Core;

// TODO
// template <uint64_t Size> class IndexSmallString : public
// enable_serialization_from_this<char[Size]> {}; class IndexString : public
// enable_serialization_from_this<std::string> {};

class IndexInt32 : public enable_serialization_from_this<int32_t> {};
class IndexInt64 : public enable_serialization_from_this<int64_t> {};

class IndexBool : public enable_serialization_from_this<bool> {};

template <uint64_t Size> class IndexBools : public enable_serialization_from_this<std::array<bool, Size>> {};
template <typename Enum> class IndexEnum : public enable_serialization_from_this<Enum> {};


enum Example { Example1 = 0, Example2 = 1 };
static_assert(Serializable<IndexEnum<Example>>);


class SomeDifferSerializeObject : public enable_serialization_from_this<IndexInt64, IndexInt32, IndexBools<256>> {};


int main() {
  IndexBool index{};
  auto* p = index.serialize();
  index.deserialize(p);

  IndexBools<4> index2{};

  std::array b = {true, false, true, false};
  index2.set<0>(b);
  index2.serialize_all();

  auto* p2 = index2.serialize();
  index2.deserialize(p2);

  auto u2 = index2.get<0>();
  for (const auto& c : u2) std::cout << c << " ";

  return 0;
}
