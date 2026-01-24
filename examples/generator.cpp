#include <memory_resource>
#include <coio/generator.h>
#include "common.h"

auto fibonacci(std::size_t n) -> coio::generator<int> {
    int a = 0, b = 1;
    while (n--) {
        co_yield b;
        a = std::exchange(b, a + b);
    }
}

auto iota(int n) -> coio::generator<int> {
    for (int i = 0; i < n; ++i) co_yield i;
}

template<typename T>
struct Node {
    auto traverse_inorder() const -> coio::generator<const T&> {
        if (left) {
            co_yield coio::elements_of{left->traverse_inorder()};
        }

        co_yield value;

        if (right) {
            co_yield coio::elements_of{right->traverse_inorder()};
        }
    }

    T value;
    Node *left{}, *right{};
};

auto main() -> int {
    for (const auto& n : fibonacci(10)) {
        ::println("{}", n);
    }

    ::println("=========");

    for (const auto& n : iota(10) |
        std::views::filter([](int i) noexcept { return i % 2 == 0; }) |
        std::views::transform([](int i) noexcept { return i * i; })
    ) {
        ::println("{}", n);
    }

    ::println("=========");

    std::array<Node<char>, 7> tree;
    tree = {
                        Node{'D', &tree[1], &tree[2]},
//                            │
//            ┌───────────────┴────────────────┐
//            │                                │
        Node{'B', &tree[3], &tree[4]},   Node{'F', &tree[5], &tree[6]},
//            │                                │
//    ┌───────┴─────────────┐          ┌───────┴──────────┐
//    │                     │          │                  │
Node{'A'},            Node{'C'}, Node{'E'},         Node{'G'}
    };

    for (char x : tree[0].traverse_inorder()) {
        std::cout << x << ' ';
    }
    std::cout << '\n';
}