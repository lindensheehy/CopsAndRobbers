#include "core.hpp"
#include <cassert>
#include <iostream>
namespace sy {
volatile std::sig_atomic_t interrupted = 0;
}
int main() {
    for (int n = 1; n <= 12; ++n)
        for (int k = 1; k <= 4; ++k) {
            sy::Index index(n, k);
            for (uint64_t id = 0; id < index.count(); ++id) {
                auto c = index.unrank(id);
                assert(std::is_sorted(c.begin(), c.begin() + k));
                assert(index.rank(c) == id);
                if (id + 1 < index.count())
                    assert(c < index.unrank(id + 1));
            }
        }
    sy::Index large(199, 4);
    assert(large.count() == 67331650);
    sy::Cops start{0, 3, 66, 184};
    assert(large.rank(start) == 69617);
    assert(large.unrank(69617) == start);
    for (uint64_t id = 0; id < large.count(); id += 7919)
        assert(large.rank(large.unrank(id)) == id);
    std::cout << "PASS: multiset rank roundtrips and recorded Scotland Yard start\n";
}
