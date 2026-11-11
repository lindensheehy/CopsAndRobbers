#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <fstream>
#include <functional>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace sy {
constexpr uint32_t INF = 1000000000;
using Cops = std::array<uint8_t, 4>;
struct Mask {
    std::array<uint64_t, 4> w{};
    void set(int v) { w[v / 64] |= uint64_t(1) << (v % 64); }
    bool has(int v) const { return (w[v / 64] >> (v % 64)) & 1; }
    void erase(int v) { w[v / 64] &= ~(uint64_t(1) << (v % 64)); }
    bool empty() const { return !(w[0] | w[1] | w[2] | w[3]); }
    bool operator==(const Mask &b) const { return w == b.w; }
    template <class F> void each(F f) const {
        // Portable C++17: scanning four words without platform intrinsics.
        for (int i = 0; i < 4; ++i) {
            uint64_t bits = w[i];
            int offset = 0;
            while (bits) {
                while (!(bits & 1)) {
                    bits >>= 1;
                    ++offset;
                }
                f(i * 64 + offset);
                bits >>= 1;
                ++offset;
            }
        }
    }
};
inline uint64_t mix(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
struct Graph {
    int n = 0, degree = 0;
    std::vector<std::vector<uint8_t>> adj;
    std::vector<Mask> neighbors;
    std::string canonical;
    explicit Graph(const std::string &path) {
        std::ifstream in(path);
        if (!in)
            throw std::runtime_error("Cannot open graph: " + path);
        std::vector<std::string> rows;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            if (line[0] == '-')
                break;
            if (line.find_first_not_of("01") != std::string::npos)
                throw std::runtime_error("Graph must be a square 0/1 adjacency matrix");
            rows.push_back(line);
        }
        n = int(rows.size());
        if (n < 1 || n > 255)
            throw std::runtime_error("Supported vertex count: 1..255");
        for (const auto &r : rows)
            if (int(r.size()) != n)
                throw std::runtime_error("Graph is not square");
        adj.resize(n);
        neighbors.resize(n);
        for (int u = 0; u < n; ++u) {
            canonical += rows[u] + "\n";
            for (int v = 0; v < n; ++v) {
                if (rows[u][v] != rows[v][u])
                    throw std::runtime_error("Graph must be undirected");
                if (rows[u][v] == '1') {
                    adj[u].push_back(uint8_t(v));
                    neighbors[u].set(v);
                }
            }
            if (adj[u].empty())
                throw std::runtime_error(
                    "Isolated vertices unsupported; define no-move rules first");
            degree = std::max(degree, int(adj[u].size()));
        }
    }
    Mask expand(const Mask &b) const {
        Mask result;
        b.each([&](int v) {
            for (int i = 0; i < 4; ++i)
                result.w[i] |= neighbors[v].w[i];
        });
        return result;
    }
};
// V5 ranks nondecreasing multiset configurations lexicographically.
struct Index {
    int n, k;
    uint64_t choose[260][5]{};
    Index(int n_, int k_) : n(n_), k(k_) {
        if (k < 1 || k > 4)
            throw std::runtime_error("Supported cop count: 1..4");
        for (int a = 0; a < 260; ++a) {
            choose[a][0] = 1;
            for (int b = 1; b <= 4; ++b)
                choose[a][b] = b > a ? 0 : (b == a ? 1 : choose[a - 1][b - 1] + choose[a - 1][b]);
        }
    }
    uint64_t count() const { return choose[n + k - 1][k]; }
    uint64_t rank(const Cops &c) const {
        uint64_t result = 0;
        int prev = -1, m = n + k - 1;
        for (int i = 0; i < k; ++i) {
            int b = int(c[i]) + i;
            result += choose[m - prev - 1][k - i] - choose[m - b][k - i];
            prev = b;
        }
        return result;
    }
    Cops unrank(uint64_t id) const {
        if (id >= count())
            throw std::runtime_error("Configuration ID out of range");
        Cops c{};
        int lo = 0;
        for (int i = 0; i < k; ++i) {
            bool found = false;
            for (int v = lo; v < n; ++v) {
                int left = k - i - 1;
                uint64_t block = choose[n - v + left - 1][left];
                if (id < block) {
                    c[i] = uint8_t(v);
                    lo = v;
                    found = true;
                    break;
                }
                id -= block;
            }
            if (!found)
                throw std::runtime_error("Invalid combinatorial rank");
        }
        return c;
    }
};
inline Mask removeCops(Mask b, const Cops &c, int k) {
    for (int i = 0; i < k; ++i)
        b.erase(c[i]);
    return b;
}
inline void sortCops(Cops &c, int k) {
    for (int i = 1; i < k; ++i)
        for (int j = i; j > 0 && c[j] < c[j - 1]; --j)
            std::swap(c[j], c[j - 1]);
}
inline std::vector<Cops> moves(const Graph &g, const Index &ix, const Cops &c) {
    uint64_t products = 1;
    for (int i = 0; i < ix.k; ++i)
        products *= g.adj[c[i]].size();
    if (products > 1000000)
        throw std::runtime_error("Joint move generation exceeds 1,000,000 products at one node");
    std::vector<Cops> result;
    Cops next{};
    std::function<void(int)> visit = [&](int i) {
        if (i == ix.k) {
            Cops sorted = next;
            sortCops(sorted, ix.k);
            result.push_back(sorted);
            return;
        }
        for (uint8_t v : g.adj[c[i]]) {
            next[i] = v;
            visit(i + 1);
        }
    };
    visit(0);
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
struct State {
    uint64_t c;
    Mask b;
    uint16_t round;
    bool operator==(const State &s) const { return c == s.c && round == s.round && b == s.b; }
};
struct Hash {
    size_t operator()(const State &s) const {
        uint64_t h = mix(s.c) ^ mix(s.round);
        for (uint64_t w : s.b.w)
            h = mix(h ^ mix(w));
        return size_t(h);
    }
};
struct Stats {
    std::atomic<uint64_t> nodes{0}, moves{0}, oracleReads{0}, pruned{0}, hits{0};
};
struct Interrupted {};
extern volatile std::sig_atomic_t interrupted;
struct Control {
    Stats &stats;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    double seconds = 0;
    uint64_t maxNodes = 0;
    std::atomic<bool> abort{false};
    void poll() {
        if (abort.load(std::memory_order_relaxed) || interrupted)
            throw Interrupted{};
        if (seconds > 0 &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >=
                seconds)
            throw Interrupted{};
    }
    void enter() {
        poll();
        uint64_t n = stats.nodes.fetch_add(1, std::memory_order_relaxed);
        if (maxNodes && n >= maxNodes)
            throw Interrupted{};
    }
};
// The reader implements this small interface; tests can run with nullptr.
struct Values {
    virtual ~Values() = default;
    virtual uint32_t robberLower(uint64_t c, int r) const = 0;
};
struct Entry {
    bool win;
    Cops action;
};
enum class OracleMode { Off, Order, Bound };
struct Search {
    const Graph &g;
    const Index &ix;
    const Values *oracle;
    OracleMode mode;
    int horizon;
    std::vector<bool> reveal;
    Control &control;
    size_t memoLimit;
    std::unordered_map<State, Entry, Hash> memo;
    Cops rootMove{};
    bool rootHasMove = false;
    Search(const Graph &g_, const Index &ix_, const Values *o, OracleMode mode_, int h,
           const std::vector<int> &reveals, Control &ctl, size_t limit)
        : g(g_), ix(ix_), oracle(o), mode(mode_), horizon(h), reveal(h + 1, false), control(ctl),
          memoLimit(limit) {
        for (int r : reveals)
            if (r <= h)
                reveal[r] = true;
    }
    void remember(const State &key, bool value, const Cops &action = {}) {
        if (!memoLimit)
            return;
        if (memo.size() >= memoLimit)
            memo.clear(); // bounded, performance-only eviction
        memo.emplace(key, Entry{value, action});
    }
    bool solve(const Cops &c, Mask b, int round = 1) {
        control.enter();
        b = removeCops(b, c, ix.k);
        if (b.empty())
            return true;
        if (round > horizon)
            return false;
        State key{ix.rank(c), b, uint16_t(round)};
        auto old = memo.find(key);
        if (old != memo.end()) {
            ++control.stats.hits;
            return old->second.win;
        }
        struct Candidate {
            Cops c;
            uint64_t id;
            uint32_t score;
        };
        std::vector<Candidate> candidates;
        const uint32_t remaining = uint32_t(2 * (horizon - round + 1));
        // Joint moves are generated only for this node, never for all 67M configurations.
        auto legal = moves(g, ix, c);
        for (const Cops &dest : legal) {
            control.poll();
            ++control.stats.moves;
            Mask mid = removeCops(b, dest, ix.k);
            if (mid.empty()) {
                remember(key, true, dest);
                if (round == 1) {
                    rootMove = dest;
                    rootHasMove = true;
                }
                return true;
            }
            uint64_t id = ix.rank(dest);
            uint32_t worst = 0;
            if (oracle && mode != OracleMode::Off) {
                mid.each([&](int r) {
                    ++control.stats.oracleReads;
                    worst = std::max(worst, oracle->robberLower(id, r));
                });
                // Even a fully informed continuation cannot finish after this action.
                if (mode == OracleMode::Bound && worst > remaining - 1) {
                    ++control.stats.pruned;
                    continue;
                }
            }
            candidates.push_back({dest, id, worst});
        }
        if (oracle && mode != OracleMode::Off)
            std::stable_sort(
                candidates.begin(), candidates.end(),
                [](const Candidate &a, const Candidate &b) { return a.score < b.score; });
        for (const auto &a : candidates) {
            control.poll();
            Mask mid = removeCops(b, a.c, ix.k);
            Mask after = removeCops(g.expand(mid), a.c, ix.k);
            bool win = after.empty();
            if (!win && round < horizon) {
                if (reveal[round]) {
                    // A reveal is observable: subsequent cop actions may depend on it.
                    win = true;
                    after.each([&](int r) {
                        if (win) {
                            Mask singleton;
                            singleton.set(r);
                            win = solve(a.c, singleton, round + 1);
                        }
                    });
                } else {
                    // Hidden: ONE policy must handle the whole surviving cloud.
                    win = solve(a.c, after, round + 1);
                }
            }
            if (win) {
                remember(key, true, a.c);
                if (round == 1) {
                    rootMove = a.c;
                    rootHasMove = true;
                }
                return true;
            }
        }
        remember(key, false);
        return false;
    }
};
} // namespace sy
