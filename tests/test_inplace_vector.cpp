#include <array>
#include <concepts>
#include <stdexcept>
#include <doctest/doctest.h>
#include <coio/utils/inplace_vector.h>

TEST_CASE("inplace_vector supports basic sequence operations") {
    coio::inplace_vector<int, 4> values;

    CHECK(values.empty());
    values.push_back(1);
    values.push_back(3);
    values.insert(values.cbegin() + 1, 2);

    CHECK(values == coio::inplace_vector<int, 4>{1, 2, 3});
    CHECK(values.front() == 1);
    CHECK(values.back() == 3);

    auto next = values.erase(values.cbegin() + 1);
    CHECK(next == values.begin() + 1);
    CHECK(values == coio::inplace_vector<int, 4>{1, 3});

    const auto erased = coio::erase(values, 1);
    CHECK(erased == 1);
    CHECK(values == coio::inplace_vector<int, 4>{3});
    CHECK_THROWS_AS(static_cast<void>(values.at(1)), std::out_of_range);

    coio::inplace_vector<int, 3> full{1, 2, 3};
    CHECK_THROWS_AS(full.push_back(4), std::bad_alloc);
}

TEST_CASE("inplace_vector handles range construction and mutation") {
    std::array<int, 5> source{1, 2, 3, 4, 5};
    coio::inplace_vector<int, 8> values{coio::from_range, source};

    CHECK(values == coio::inplace_vector<int, 8>{1, 2, 3, 4, 5});

    auto where = values.insert_range(values.cbegin() + 2, std::array{9, 8});
    CHECK(where == values.begin() + 2);
    CHECK(values == coio::inplace_vector<int, 8>{1, 2, 9, 8, 3, 4, 5});

    const auto removed = coio::erase_if(values, [](int value) noexcept { return value % 2 == 0; });
    CHECK(removed == 3);
    CHECK(values == coio::inplace_vector<int, 8>{1, 9, 3, 5});

    coio::inplace_vector<int, 8> other{7, 11};
    std::ranges::swap(values, other);

    CHECK(values == coio::inplace_vector<int, 8>{7, 11});
    CHECK(other == coio::inplace_vector<int, 8>{1, 9, 3, 5});
}
