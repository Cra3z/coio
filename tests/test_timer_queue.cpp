#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <random>
#include <set>
#include <thread>
#include <vector>
#include <doctest/doctest.h>
#include <coio/detail/intrusive_list.h>
#include <coio/detail/op_queue.h>

using namespace std::chrono_literals;

namespace {
    using time_point = std::chrono::steady_clock::time_point;

    struct test_timer {
        time_point deadline;
        coio::detail::timer_heap_links<test_timer> links;
        test_timer* next = nullptr; // for coio::detail::intrusive_list
    };

    using test_queue = coio::detail::timer_queue<test_timer, &test_timer::deadline, &test_timer::links>;

    auto make_ready_list() -> coio::detail::intrusive_list<test_timer> {
        return coio::detail::intrusive_list<test_timer>{&test_timer::next};
    }

    auto pop_all(coio::detail::intrusive_list<test_timer>& list) -> std::vector<test_timer*> {
        std::vector<test_timer*> result;
        while (test_timer* node = list.pop_front()) result.push_back(node);
        return result;
    }

    auto deadlines_of(const std::vector<test_timer*>& nodes) -> std::vector<time_point> {
        std::vector<time_point> result;
        result.reserve(nodes.size());
        for (const test_timer* node : nodes) result.push_back(node->deadline);
        return result;
    }
}

TEST_CASE("timer_queue add reports new-earliest and earliest tracks the minimum") {
    const auto base = std::chrono::steady_clock::now();
    test_queue queue;

    CHECK_FALSE(queue.earliest().has_value());

    test_timer mid{.deadline = base + 10min};
    CHECK(queue.add(mid)); // first add is always the new earliest
    CHECK_EQ(queue.earliest(), mid.deadline);

    test_timer late{.deadline = base + 20min};
    CHECK_FALSE(queue.add(late)); // later deadline is not the new earliest
    CHECK_EQ(queue.earliest(), mid.deadline);

    test_timer early{.deadline = base + 5min};
    CHECK(queue.add(early)); // strictly earlier deadline is the new earliest
    CHECK_EQ(queue.earliest(), early.deadline);

    test_timer tie{.deadline = base + 5min};
    CHECK_FALSE(queue.add(tie)); // a tie with the current minimum must not claim new-earliest
    CHECK_EQ(queue.earliest(), early.deadline);
}

TEST_CASE("timer_queue take_ready_timers pops only expired timers in deadline order") {
    const auto base = std::chrono::steady_clock::now();
    test_queue queue;

    test_timer expired_a{.deadline = base - 3s};
    test_timer expired_b{.deadline = base - 1s};
    test_timer expired_c{.deadline = base - 2s};
    test_timer expired_dup{.deadline = base - 2s};
    test_timer future_a{.deadline = base + 30min};
    test_timer future_b{.deadline = base + 10min};

    for (test_timer* timer : {&expired_b, &future_a, &expired_a, &future_b, &expired_c, &expired_dup}) {
        queue.add(*timer);
    }

    auto ready = make_ready_list();
    queue.take_ready_timers(ready);
    const auto popped = pop_all(ready);

    REQUIRE_EQ(popped.size(), 4);
    const auto popped_deadlines = deadlines_of(popped);
    CHECK(std::is_sorted(popped_deadlines.begin(), popped_deadlines.end()));
    CHECK_EQ(popped.front(), &expired_a);
    CHECK_EQ(popped.back(), &expired_b);
    CHECK((
        (popped[1] == &expired_c and popped[2] == &expired_dup) or
        (popped[1] == &expired_dup and popped[2] == &expired_c)
    ));

    // the far-future timers are still linked and the minimum is correct
    CHECK_EQ(queue.earliest(), future_b.deadline);

    // a second call must not pop anything new
    queue.take_ready_timers(ready);
    CHECK(ready.empty());
    CHECK_EQ(queue.earliest(), future_b.deadline);
}

TEST_CASE("timer_queue remove unlinks root and interior nodes and keeps the heap well-formed") {
    const auto base = std::chrono::steady_clock::now();
    test_queue queue;

    constexpr std::size_t node_count = 16;
    std::vector<test_timer> nodes(node_count);
    for (std::size_t i = 0; i < node_count; ++i) {
        nodes[i].deadline = base - 1h + std::chrono::milliseconds{7 * i}; // distinct, all expired
    }

    // add in a shuffled order so the heap gets a non-trivial shape
    for (const std::size_t i : {9, 2, 14, 0, 7, 11, 4, 15, 1, 13, 6, 10, 3, 12, 8, 5}) {
        queue.add(nodes[i]);
    }
    REQUIRE_EQ(queue.earliest(), nodes[0].deadline);

    CHECK(queue.remove(nodes[0])); // the root
    CHECK_EQ(queue.earliest(), nodes[1].deadline);

    CHECK(queue.remove(nodes[7]));  // interior/leaf nodes
    CHECK(queue.remove(nodes[15]));
    CHECK(queue.remove(nodes[3]));
    CHECK_FALSE(queue.remove(nodes[7])); // second remove of the same node
    CHECK_EQ(queue.earliest(), nodes[1].deadline);

    // drain the survivors (all expired): they must come out sorted and complete
    auto ready = make_ready_list();
    queue.take_ready_timers(ready);
    const auto popped = pop_all(ready);

    std::vector<time_point> expected;
    for (const std::size_t i : {1, 2, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14}) {
        expected.push_back(nodes[i].deadline);
    }
    CHECK_EQ(deadlines_of(popped), expected);
    CHECK_FALSE(queue.earliest().has_value());
}

TEST_CASE("timer_queue remove returns false for nodes that are not linked") {
    const auto base = std::chrono::steady_clock::now();
    test_queue queue;

    test_timer never_added{.deadline = base + 1min};
    CHECK_FALSE(queue.remove(never_added));

    test_timer popped{.deadline = base - 1s};
    queue.add(popped);
    auto ready = make_ready_list();
    queue.take_ready_timers(ready);
    REQUIRE_EQ(ready.pop_front(), &popped);
    CHECK_FALSE(queue.remove(popped)); // already popped by take_ready_timers

    test_timer removed_twice{.deadline = base + 1min};
    queue.add(removed_twice);
    CHECK(queue.remove(removed_twice));
    CHECK_FALSE(queue.remove(removed_twice)); // second remove of the same node
}

TEST_CASE("timer_queue nodes can be re-added after being popped or removed") {
    const auto base = std::chrono::steady_clock::now();
    test_queue queue;

    test_timer other{.deadline = base + 10min};
    queue.add(other);

    // pop a node via take_ready_timers, then reuse it
    test_timer node{.deadline = base - 1s};
    queue.add(node);
    auto ready = make_ready_list();
    queue.take_ready_timers(ready);
    REQUIRE_EQ(ready.pop_front(), &node);
    REQUIRE(ready.empty());

    node.deadline = base + 5min;
    CHECK(queue.add(node)); // earlier than `other`, so it is the new earliest
    CHECK_EQ(queue.earliest(), node.deadline);

    // remove it again, then reuse it once more
    CHECK(queue.remove(node));
    CHECK_EQ(queue.earliest(), other.deadline);

    node.deadline = base - 1s;
    CHECK(queue.add(node));
    queue.take_ready_timers(ready);
    REQUIRE_EQ(ready.pop_front(), &node);
    REQUIRE(ready.empty());
    CHECK_EQ(queue.earliest(), other.deadline);
}

TEST_CASE("timer_queue randomized adds and removes match a reference model") {
    constexpr std::size_t node_count = 400;
    std::mt19937 rng{20260806u};

    const auto base = std::chrono::steady_clock::now() - 1h; // everything is already expired
    std::vector<test_timer> nodes(node_count);
    std::uniform_int_distribution<int> deadline_dist{0, 63}; // small range => plenty of duplicates
    for (test_timer& node : nodes) {
        node.deadline = base + std::chrono::milliseconds{deadline_dist(rng)};
    }

    test_queue queue;
    std::multiset<time_point> model;
    std::vector<bool> linked(node_count, false);

    std::uniform_int_distribution<std::size_t> victim_dist{0, node_count - 1};
    std::uniform_int_distribution<int> percent_dist{0, 99};

    for (std::size_t i = 0; i < node_count; ++i) {
        const bool expect_new_earliest = model.empty() or nodes[i].deadline < *model.begin();
        CHECK_EQ(queue.add(nodes[i]), expect_new_earliest);
        model.insert(nodes[i].deadline);
        linked[i] = true;

        if (percent_dist(rng) < 40) {
            // may pick a node not yet added, already removed, or currently linked
            const std::size_t victim = victim_dist(rng);
            const bool expect_removed = linked[victim];
            CHECK_EQ(queue.remove(nodes[victim]), expect_removed);
            if (expect_removed) {
                model.erase(model.find(nodes[victim].deadline));
                linked[victim] = false;
            }
        }

        if (model.empty()) REQUIRE_FALSE(queue.earliest().has_value());
        else REQUIRE_EQ(queue.earliest(), *model.begin());
    }

    auto ready = make_ready_list();
    queue.take_ready_timers(ready);
    const auto popped = pop_all(ready);
    const auto popped_deadlines = deadlines_of(popped);

    CHECK(std::is_sorted(popped_deadlines.begin(), popped_deadlines.end()));
    const std::multiset<time_point> drained(popped_deadlines.begin(), popped_deadlines.end());
    CHECK(drained == model);
    CHECK_FALSE(queue.earliest().has_value());
}

TEST_CASE("timer_queue concurrent producers with a single drainer deliver every timer exactly once") {
    constexpr std::size_t producer_count = 4;
    constexpr std::size_t per_producer = 256;
    constexpr std::size_t total = producer_count * per_producer;

    const auto base = std::chrono::steady_clock::now();
    test_queue queue;
    std::vector<test_timer> nodes(total);
    for (std::size_t i = 0; i < total; ++i) {
        nodes[i].deadline = base - 1s - std::chrono::microseconds{i}; // all expired
    }

    std::vector<std::thread> producers;
    producers.reserve(producer_count);
    for (std::size_t p = 0; p < producer_count; ++p) {
        producers.emplace_back([&queue, &nodes, p] {
            for (std::size_t i = 0; i < per_producer; ++i) {
                queue.add(nodes[p * per_producer + i]);
            }
        });
    }

    std::vector<int> pop_counts(total, 0);
    std::size_t collected = 0;
    auto ready = make_ready_list();
    while (collected < total) {
        queue.take_ready_timers(ready);
        while (test_timer* node = ready.pop_front()) {
            ++pop_counts[static_cast<std::size_t>(node - nodes.data())];
            ++collected;
        }
        std::this_thread::yield();
    }
    for (std::thread& producer : producers) producer.join();

    CHECK(std::all_of(pop_counts.begin(), pop_counts.end(), [](int count) { return count == 1; }));
    CHECK_FALSE(queue.earliest().has_value());

    queue.take_ready_timers(ready);
    CHECK(ready.empty());
}

TEST_CASE("timer_queue concurrent add and remove of future timers leaves the queue empty") {
    constexpr std::size_t producer_count = 4;
    constexpr std::size_t per_producer = 256;
    constexpr std::size_t total = producer_count * per_producer;

    const auto base = std::chrono::steady_clock::now();
    test_queue queue;
    std::vector<test_timer> nodes(total);
    for (std::size_t i = 0; i < total; ++i) {
        nodes[i].deadline = base + 1h + std::chrono::milliseconds{i}; // far future: cannot fire
    }

    std::atomic<std::size_t> successful_removes{0};
    std::atomic<std::size_t> done_producers{0};

    std::vector<std::thread> producers;
    producers.reserve(producer_count);
    for (std::size_t p = 0; p < producer_count; ++p) {
        producers.emplace_back([&, p] {
            // add the whole batch first so removes interleave with other producers' adds
            for (std::size_t i = 0; i < per_producer; ++i) {
                queue.add(nodes[p * per_producer + i]);
            }
            std::size_t removed = 0;
            for (std::size_t i = 0; i < per_producer; ++i) {
                removed += queue.remove(nodes[p * per_producer + i]) ? 1 : 0;
            }
            successful_removes.fetch_add(removed, std::memory_order_relaxed);
            done_producers.fetch_add(1, std::memory_order_release);
        });
    }

    std::size_t drained = 0;
    auto ready = make_ready_list();
    while (done_producers.load(std::memory_order_acquire) < producer_count) {
        queue.take_ready_timers(ready);
        drained += pop_all(ready).size();
        std::this_thread::yield();
    }
    for (std::thread& producer : producers) producer.join();

    queue.take_ready_timers(ready);
    drained += pop_all(ready).size();

    // the fire/remove race is impossible for future deadlines: every remove must have won
    CHECK_EQ(drained, 0);
    CHECK_EQ(successful_removes.load(), total);
    CHECK_FALSE(queue.earliest().has_value());
}
