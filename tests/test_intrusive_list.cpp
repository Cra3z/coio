#include <array>
#include <vector>
#include <doctest/doctest.h>
#include <coio/detail/intrusive_list.h>

namespace {
    struct item {
        int value;
        item* next = nullptr;
    };

    auto values_from(item* head) -> std::vector<int> {
        std::vector<int> values;
        for (auto* it = head; it != nullptr; it = it->next) {
            values.push_back(it->value);
        }
        return values;
    }
}

TEST_CASE("intrusive_list pushes and pops in FIFO order") {
    coio::detail::intrusive_list<item> list{&item::next};
    item first{1};
    item second{2};
    item third{3};

    CHECK(list.empty());
    CHECK_EQ(list.front(), nullptr);
    CHECK_EQ(list.back(), nullptr);

    list.push_back(first);
    list.push_back(second);
    list.push_back(third);

    CHECK_FALSE(list.empty());
    CHECK_EQ(list.front(), &first);
    CHECK_EQ(list.back(), &third);
    CHECK_EQ(values_from(list.front()), std::vector{1, 2, 3});

    CHECK_EQ(list.pop_front(), &first);
    CHECK_EQ(list.pop_front(), &second);
    CHECK_EQ(list.pop_front(), &third);
    CHECK_EQ(list.pop_front(), nullptr);
    CHECK(list.empty());
    CHECK_EQ(list.back(), nullptr);
}

TEST_CASE("intrusive_list appends a chain and detaches it") {
    coio::detail::intrusive_list<item> list{&item::next};
    std::array items{item{1}, item{2}, item{3}, item{4}};
    items[0].next = &items[1];
    items[1].next = &items[2];

    CHECK_EQ(list.append(items[0]), 3);
    list.push_back(items[3]);

    CHECK_EQ(values_from(list.front()), std::vector{1, 2, 3, 4});
    CHECK_EQ(list.back(), &items[3]);

    item* head = list.release();
    CHECK(list.empty());
    CHECK_EQ(list.front(), nullptr);
    CHECK_EQ(list.back(), nullptr);
    CHECK_EQ(values_from(head), std::vector{1, 2, 3, 4});
}
