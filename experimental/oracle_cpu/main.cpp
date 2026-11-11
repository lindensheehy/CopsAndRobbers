#include "oracle.hpp"
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
namespace sy {
volatile std::sig_atomic_t interrupted = 0;
}
using namespace sy;
static void signalHandler(int) { interrupted = 1; }
static uint64_t number(const std::string &s) {
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Expected unsigned integer: " + s);
    size_t used = 0;
    auto n = std::stoull(s, &used);
    if (used != s.size())
        throw std::runtime_error("Invalid integer");
    return n;
}
static std::vector<int> list(const std::string &s) {
    std::vector<int> v;
    if (s == "none")
        return v;
    std::istringstream in(s);
    std::string t;
    while (std::getline(in, t, ',')) {
        auto x = number(t);
        if (x > 65535)
            throw std::runtime_error("List item too large");
        v.push_back(int(x));
    }
    if (v.empty() || s.back() == ',')
        throw std::runtime_error("Empty list item");
    return v;
}
static void printCops(const Cops &c, int k) {
    std::cout << '[';
    for (int i = 0; i < k; ++i) {
        if (i)
            std::cout << ',';
        std::cout << int(c[i]);
    }
    std::cout << ']';
}
static uint64_t fingerprint(const std::string &s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}
static void help() {
    std::cout << R"(CPU finite-horizon robber-cloud tree solver (zero-based vertex IDs)
Usage: scotlandyard_oracle_cpu.exe MATRIX [options]
 --cops K                 1..4 (default 4)
 --start a,b,c,d           fixed cop start; defaults to oracle best start
 --oracle FILE            completed V5 v3 / encoding 2 full-visibility oracle
 --oracle-graph MATRIX    original matrix used by that oracle (required with oracle)
 --oracle-mode bound|order|off  default bound with oracle, off without
 --rounds H               capture deadline in cop+robber rounds (default 24)
 --reveals 3,8,13,18,24    reveal AFTER these robber moves; 'none' supported
 --robber all|ID          initially visible (default all, separate root per ID)
 --hidden-start all|LIST  alternatively one initially hidden cloud
 --threads N              parallel initial roots (default 1)
 --memo-entries N         per-thread cache limit (default 100000; 0 disables)
 --max-seconds N          search budget (default 60; 0 unlimited)
 --max-nodes N            shared search node budget (default 0 unlimited)
 --checkpoint FILE        atomic completed-root checkpoint
 --resume                 resume completed roots from that checkpoint
 --preflight              validate inputs/header only
 --query R                query full-visibility cop-turn lower bound at --start
 --help
No tickets or added pass moves. Matrix self-edges are honored.
WIN/LOSS refer to this fixed start and deadline, not the graph's cop number.
Exit 0: definitive result, 2: UNKNOWN (budget/interrupt), 1: invalid input/error.
Checkpoints resume BETWEEN initial roots; unfinished DFS paths restart.
)";
}
int main(int argc, char **argv) {
    try {
        if (argc < 2 || std::string(argv[1]) == "--help") {
            help();
            return argc < 2 ? 1 : 0;
        }
        std::string graphPath = argv[1], startText, oraclePath, original, modeText, robber = "all",
                    hidden, checkpoint;
        int k = 4, horizon = 24, threads = 1, query = -1;
        uint64_t memo = 100000, maxNodes = 0;
        double seconds = 60;
        std::vector<int> reveals{3, 8, 13, 18, 24};
        bool resume = false, preflight = false, robberSet = false;
        for (int i = 2; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--help") {
                help();
                return 0;
            }
            if (a == "--resume") {
                resume = true;
                continue;
            }
            if (a == "--preflight") {
                preflight = true;
                continue;
            }
            if (i + 1 >= argc)
                throw std::runtime_error("Missing value: " + a);
            std::string v = argv[++i];
            auto bounded = [&](uint64_t max) {
                auto x = number(v);
                if (x > max)
                    throw std::runtime_error("Option out of range: " + a);
                return int(x);
            };
            if (a == "--cops")
                k = bounded(4);
            else if (a == "--rounds")
                horizon = bounded(256);
            else if (a == "--threads")
                threads = bounded(256);
            else if (a == "--start")
                startText = v;
            else if (a == "--oracle")
                oraclePath = v;
            else if (a == "--oracle-graph")
                original = v;
            else if (a == "--oracle-mode")
                modeText = v;
            else if (a == "--reveals")
                reveals = list(v);
            else if (a == "--robber") {
                robber = v;
                robberSet = true;
            } else if (a == "--hidden-start")
                hidden = v;
            else if (a == "--memo-entries")
                memo = number(v);
            else if (a == "--max-nodes")
                maxNodes = number(v);
            else if (a == "--max-seconds")
                seconds = double(number(v));
            else if (a == "--checkpoint")
                checkpoint = v;
            else if (a == "--query")
                query = bounded(254);
            else
                throw std::runtime_error("Unknown option: " + a);
        }
        if (horizon < 1 || threads < 1 || seconds > 1e12)
            throw std::runtime_error("Invalid horizon, threads or time limit");
        for (int r : reveals)
            if (r < 1 || r > 256)
                throw std::runtime_error("Reveal rounds must be 1..256");
        if (!hidden.empty() && robberSet)
            throw std::runtime_error("Choose --robber OR --hidden-start");
        Graph g(graphPath);
        Index ix(g.n, k);
        std::unique_ptr<Oracle> oracle;
        if (!oraclePath.empty())
            oracle = std::make_unique<Oracle>(oraclePath, g, ix, original);
        if (modeText.empty())
            modeText = oracle ? "bound" : "off";
        OracleMode mode = modeText == "bound"   ? OracleMode::Bound
                          : modeText == "order" ? OracleMode::Order
                                                : OracleMode::Off;
        if (modeText != "bound" && modeText != "order" && modeText != "off")
            throw std::runtime_error("Invalid oracle mode");
        if (mode != OracleMode::Off && !oracle)
            throw std::runtime_error("Oracle mode requires --oracle");
        Cops start{};
        if (startText.empty()) {
            if (!oracle || oracle->best == UINT64_MAX)
                throw std::runtime_error("Supply --start");
            start = ix.unrank(oracle->best);
        } else {
            auto a = list(startText);
            if (int(a.size()) != k)
                throw std::runtime_error("Start length must equal cop count");
            for (int i = 0; i < k; ++i) {
                if (a[i] >= g.n)
                    throw std::runtime_error("Cop vertex out of range");
                start[i] = uint8_t(a[i]);
            }
            sortCops(start, k);
        }
        if (oracle)
            std::cerr
                << "Using completed V5 oracle read-only. V5 has no graph hash: --oracle-graph must "
                   "be its authentic original matrix. No added passes or tickets.\n";
        if (preflight) {
            std::cout << "{\"status\":\"PREFLIGHT_OK\",\"vertices\":" << g.n
                      << ",\"config_count\":" << ix.count()
                      << ",\"oracle_bytes\":" << (oracle ? oracle->map.size : 0) << ",\"start\":";
            printCops(start, k);
            std::cout << "}\n";
            return 0;
        }
        if (query >= 0) {
            if (!oracle || query >= g.n)
                throw std::runtime_error("Query needs oracle and valid robber vertex");
            uint32_t best = INF;
            bool exact = true;
            Cops action{};
            bool has = false;
            if (std::find(start.begin(), start.begin() + k, query) != start.begin() + k)
                best = 0;
            else
                for (auto c : moves(g, ix, start)) {
                    auto id = ix.rank(c);
                    auto v = oracle->robberLower(id, query);
                    if (v < INF)
                        ++v;
                    if (v < best) {
                        best = v;
                        action = c;
                        has = true;
                        exact = !oracle->saturated(id, query);
                    } else if (v == best && v < INF && !oracle->saturated(id, query)) {
                        action = c;
                        exact = true;
                    }
                }
            std::cout << "{\"winning\":" << (best < INF ? "true" : "false")
                      << ",\"exact\":" << (exact ? "true" : "false") << ",\"ply_lower_bound\":";
            if (best == INF)
                std::cout << "null";
            else
                std::cout << best;
            std::cout << ",\"recommended_cops\":";
            if (has)
                printCops(action, k);
            else
                std::cout << "null";
            std::cout << "}\n";
            return 0;
        }
        std::vector<Mask> roots;
        auto vertices = [&](const std::string &s) {
            std::vector<int> a;
            if (s == "all") {
                for (int r = 0; r < g.n; ++r)
                    a.push_back(r);
            } else
                a = list(s);
            if (a.empty())
                throw std::runtime_error("Initial robber set must not be empty");
            for (int r : a)
                if (r >= g.n)
                    throw std::runtime_error("Robber vertex out of range");
            return a;
        };
        if (hidden.empty()) {
            for (int r : vertices(robber)) {
                Mask b;
                b.set(r);
                roots.push_back(b);
            }
        } else {
            Mask b;
            for (int r : vertices(hidden))
                b.set(r);
            roots.push_back(b);
        }
        struct Result {
            int value = -1;
            bool has = false;
            Cops action{};
        };
        std::vector<Result> results(roots.size());
        std::ostringstream key;
        key << "SY_CPU_V1\n"
            << g.canonical << k << ' ' << ix.rank(start) << ' ' << horizon << ' ' << modeText
            << '\n';
        for (int r : reveals)
            key << r << ',';
        for (auto b : roots) {
            key << '\n';
            for (auto w : b.w)
                key << w << ',';
        }
        if (oracle) {
            key << std::filesystem::absolute(oraclePath).string() << oracle->map.size
                << std::filesystem::last_write_time(oraclePath).time_since_epoch().count();
            for (int i = 0; i < 64; ++i)
                key << int(oracle->map.data[i]) << ',';
        }
        auto identity = fingerprint(key.str());
        if (resume) {
            if (checkpoint.empty())
                throw std::runtime_error("--resume requires --checkpoint");
            std::ifstream in(checkpoint);
            std::string magic;
            uint64_t old = 0, count = 0;
            in >> magic >> old >> count;
            if (magic != "SY_ROOT_CHECKPOINT_V1" || old != identity || count != roots.size())
                throw std::runtime_error("Checkpoint does not match inputs");
            for (auto &r : results) {
                int has = 0;
                in >> r.value >> has;
                if (r.value < -1 || r.value > 1 || has < 0 || has > 1)
                    throw std::runtime_error("Invalid checkpoint result");
                r.has = has;
                for (auto &c : r.action) {
                    int v = -1;
                    in >> v;
                    if (v < 0 || v >= g.n)
                        throw std::runtime_error("Invalid checkpoint action");
                    c = uint8_t(v);
                }
            }
            if (!in)
                throw std::runtime_error("Truncated checkpoint");
        } else if (!checkpoint.empty() && std::filesystem::exists(checkpoint))
            throw std::runtime_error("Checkpoint exists; use --resume or another filename");
        auto save = [&]() {
            if (checkpoint.empty())
                return;
            auto parent = std::filesystem::path(checkpoint).parent_path();
            if (!parent.empty())
                std::filesystem::create_directories(parent);
            std::string tmp = checkpoint + ".tmp";
            std::ofstream out(tmp, std::ios::trunc);
            out << "SY_ROOT_CHECKPOINT_V1\n" << identity << ' ' << results.size() << '\n';
            for (const auto &r : results) {
                out << r.value << ' ' << r.has;
                for (auto c : r.action)
                    out << ' ' << int(c);
                out << '\n';
            }
            out.flush();
            if (!out)
                throw std::runtime_error("Checkpoint write failed");
            out.close();
            replaceFile(tmp, checkpoint);
        };
        Stats stats;
        Control ctl{stats};
        ctl.seconds = seconds;
        ctl.maxNodes = maxNodes;
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);
        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<size_t> next{0};
        std::atomic<int> active{0};
        std::string error;
        threads = std::min(threads, int(roots.size()));
        std::vector<std::thread> workers;
        auto worker = [&]() {
            try {
                for (;;) {
                    size_t i = next.fetch_add(1);
                    if (i >= roots.size())
                        break;
                    if (results[i].value != -1)
                        continue;
                    Search search(g, ix, oracle.get(), mode, horizon, reveals, ctl, size_t(memo));
                    bool win = search.solve(start, roots[i]);
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        results[i] = {win ? 1 : 0, search.rootHasMove, search.rootMove};
                        save();
                    }
                }
            } catch (const Interrupted &) {
            } catch (const std::exception &e) {
                ctl.abort = true;
                std::lock_guard<std::mutex> lock(mutex);
                error = e.what();
            }
            --active;
            cv.notify_one();
        };
        try {
            for (int t = 0; t < threads; ++t) {
                ++active;
                try {
                    workers.emplace_back(worker);
                } catch (...) {
                    --active;
                    throw;
                }
            }
        } catch (...) {
            ctl.abort = true;
            for (auto &w : workers)
                w.join();
            throw;
        }
        {
            std::unique_lock<std::mutex> lock(mutex);
            while (active) {
                cv.wait_for(lock, std::chrono::seconds(5));
                if (active)
                    std::cerr << "nodes=" << stats.nodes << " oracle_reads=" << stats.oracleReads
                              << " pruned=" << stats.pruned << '\n';
            }
        }
        for (auto &w : workers)
            w.join();
        if (!error.empty())
            throw std::runtime_error(error);
        save();
        bool all = true, loss = false;
        for (auto r : results) {
            all &= r.value == 1;
            loss |= r.value == 0;
        }
        const char *verdict = loss ? "LOSS" : all ? "WIN" : "UNKNOWN";
        std::cout << "{\"status\":\"" << verdict << "\",\"rounds\":" << horizon << ",\"start\":";
        printCops(start, k);
        std::cout << ",\"roots\":[";
        for (size_t i = 0; i < roots.size(); ++i) {
            if (i)
                std::cout << ',';
            std::cout << "{\"robbers\":[";
            bool first = true;
            roots[i].each([&](int r) {
                if (!first)
                    std::cout << ',';
                first = false;
                std::cout << r;
            });
            std::cout << "],\"status\":\""
                      << (results[i].value < 0 ? "UNKNOWN"
                          : results[i].value   ? "WIN"
                                               : "LOSS")
                      << "\",\"first_cop_move\":";
            if (results[i].has)
                printCops(results[i].action, k);
            else
                std::cout << "null";
            std::cout << '}';
        }
        std::cout
            << "],\"nodes\":" << stats.nodes << ",\"oracle_reads\":" << stats.oracleReads
            << ",\"pruned\":" << stats.pruned << ",\"memo_hits\":" << stats.hits << ",\"seconds\":"
            << std::chrono::duration<double>(std::chrono::steady_clock::now() - ctl.start).count()
            << "}\n";
        return !loss && !all ? 2 : 0;
    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 1;
    }
}
