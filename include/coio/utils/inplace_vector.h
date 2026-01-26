#pragma once
#include <concepts>
#include <stdexcept>
#include <iterator>
#include <ranges>
#include <memory>
#include <cassert>
#include <algorithm>
#include "../detail/config.h"
#include "../detail/concepts.h"

namespace coio {

    namespace detail {
        template<typename T>
        using cpp17_iter_reference_t = std::iter_reference_t<T>;

        template<typename T>
        using cpp17_iter_value_t = std::iter_value_t<T>;
    };

    struct from_range_t {
        explicit from_range_t() = default;
    };

    inline constexpr from_range_t from_range{};

    template<typename T, std::size_t N>
    class inplace_vector {
        static_assert(N > 0, "zero capacity is not allowed in `::coio::inplace_vector`.");
        static_assert(std::same_as<T, std::remove_cv_t<T>> and std::is_object_v<T> and std::movable<T>, "`T` must be a movable object-type.");
    public:
        using value_type = T;
        using size_type    = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference    = value_type&;
        using const_reference = const value_type&;
        using pointer = value_type*;
        using const_pointer = const value_type*;
        using iterator = pointer;
        using const_iterator = const_pointer;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    public:

        constexpr inplace_vector() noexcept {
            COIO_START_LIFETIME_AS_ARRAY(value_type, std::addressof(storages_), N);
        }

        constexpr explicit inplace_vector(size_type count) requires std::default_initializable<value_type> : inplace_vector() { // NOLINT: no need to initialize `storages_`
            resize(count);
        }

        constexpr inplace_vector(size_type count, const_reference value) requires std::copy_constructible<value_type> : inplace_vector() { // NOLINT: no need to initialize `storages_`
            resize(count, value);
        }

        constexpr inplace_vector(std::initializer_list<value_type> ilist) requires std::copy_constructible<value_type> : inplace_vector() { // NOLINT: no need to initialize `storages_`
            append_range(ilist);
        }

        template<cpp17_input_iterator InputIt> requires std::constructible_from<value_type, detail::cpp17_iter_reference_t<InputIt>>
        constexpr inplace_vector(InputIt first, InputIt last) : inplace_vector() { // NOLINT: no need to initialize `storages_`
            insert(cend(), std::move(first), std::move(last));
        }

        template<container_compatible_range<value_type> Range>
        constexpr inplace_vector(from_range_t, Range&& r) : inplace_vector(){ // NOLINT: no need to initialize `storages_`
            append_range(std::forward<Range>(r));
        }

        inplace_vector(const inplace_vector&) = default;

        constexpr inplace_vector(const inplace_vector& other) requires std::copy_constructible<value_type> and (not std::is_trivially_copy_constructible_v<value_type>) : inplace_vector() { // NOLINT: no need to initialize `storages_`
            append_range(other);
        }

        inplace_vector(inplace_vector&&) = default;

        constexpr inplace_vector(inplace_vector&& other) noexcept(std::is_nothrow_move_constructible_v<value_type>) requires (not std::is_trivially_move_constructible_v<value_type>) : inplace_vector() { // NOLINT: no need to initialize `storages_`
            append_range(std::ranges::subrange{std::make_move_iterator(other.begin()), std::make_move_iterator(other.end())});
        }

        ~inplace_vector() = default;

        constexpr ~inplace_vector() requires (not std::is_trivially_destructible_v<value_type>) {
            clear();
        }

        auto operator= (const inplace_vector&) -> inplace_vector& = default;

        constexpr auto operator= (const inplace_vector& other) -> inplace_vector& requires
            std::copyable<value_type> and
            (not(
                std::is_trivially_destructible_v<value_type> and
                std::is_trivially_copy_constructible_v<value_type> and
                std::is_trivially_copy_assignable_v<value_type>
            ))
        {
            if (this != &other) [[likely]] assign_range(other);
            return *this;
        }

        auto operator= (inplace_vector&& other) -> inplace_vector& = default;

        constexpr auto operator= (inplace_vector&& other) noexcept(std::is_nothrow_move_assignable_v<value_type> and std::is_nothrow_move_constructible_v<value_type>) -> inplace_vector& requires (not(
                std::is_trivially_destructible_v<value_type> and
                std::is_trivially_move_constructible_v<value_type> and
                std::is_trivially_move_assignable_v<value_type>
            ))
        {
            if (this != &other) [[likely]] assign_range(std::ranges::subrange{std::make_move_iterator(other.begin()), std::make_move_iterator(other.end())});
            return *this;
        }

        constexpr auto operator= (std::initializer_list<value_type> ilist) -> inplace_vector& {
            assign_range(ilist);
            return *this;
        }

        constexpr auto assign(size_type count, const_reference value) -> void requires std::copy_constructible<value_type> {
            size_type i = 0;
            auto old_size = cur_size_;
            while (count--) {
                if (i < old_size) {
                    (*this)[i] = value;
                }
                else {
                    void(push_back(value));
                }
                ++i;
            }
        }

        template<cpp17_input_iterator InputIt> requires std::constructible_from<value_type, detail::cpp17_iter_reference_t<InputIt>> and std::assignable_from<value_type&, std::iter_reference_t<InputIt>>
        constexpr auto assign(InputIt first, InputIt last) -> void {
            if constexpr (cpp17_random_access_iterator<InputIt>) {
                if (std::distance(first, last) > N) throw std::bad_alloc{};
            }
            size_type i = 0;
            auto old_size = cur_size_;
            for (auto it = std::move(first); it != last; ++i, ++it) {
                if (i < old_size) {
                    (*this)[i] = *it;
                }
                else {
                    void(emplace_back(*it));
                }
            }
            if (i < old_size) erase(cbegin() + i, cend());
        }

        constexpr auto assign(std::initializer_list<value_type> ilist) -> void requires std::assignable_from<value_type&, const_reference> {
            assign_range(ilist);
        }

        template<container_compatible_range<value_type> Range> requires std::assignable_from<value_type&, std::ranges::range_reference_t<Range>>
        constexpr auto assign_range(Range&& r) -> void {
            if constexpr (std::ranges::sized_range<Range> or std::ranges::random_access_range<Range>) {
                if (std::ranges::distance(std::forward<Range>(r)) > N) throw std::bad_alloc{};
            }
            size_type i = 0;
            auto old_size = cur_size_;
            for (auto&& elem : r) {
                if (i < old_size) {
                    (*this)[i] = std::forward<decltype(elem)>(elem);
                }
                else {
                    void(emplace_back(std::forward<decltype(elem)>(elem)));
                }
                ++i;
            }
            if (i < old_size) erase(cbegin() + i, cend());
        }

        [[nodiscard]]
        constexpr auto begin() noexcept -> iterator {
            return empty() ? nullptr : std::addressof(storages_[0]);
        }

        [[nodiscard]]
        constexpr auto begin() const noexcept -> const_iterator {
            return empty() ? nullptr : std::addressof(storages_[0]);
        }

        [[nodiscard]]
        constexpr auto cbegin() const noexcept -> const_iterator {
            return begin();
        }

        [[nodiscard]]
        constexpr auto rbegin() noexcept -> reverse_iterator {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]]
        constexpr auto rbegin() const noexcept -> const_reverse_iterator {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]]
        constexpr auto crbegin() const noexcept -> const_reverse_iterator {
            return rbegin();
        }

        [[nodiscard]]
        constexpr auto end() noexcept -> iterator {
            return empty() ? nullptr : begin() + size();
        }

        [[nodiscard]]
        constexpr auto end() const noexcept -> const_iterator {
            return empty() ? nullptr : begin() + size();
        }

        [[nodiscard]]
        constexpr auto cend() const noexcept -> const_iterator {
            return end();
        }

        [[nodiscard]]
        constexpr auto rend() noexcept -> reverse_iterator {
            return std::make_reverse_iterator(begin());
        }

        [[nodiscard]]
        constexpr auto rend() const noexcept -> const_reverse_iterator {
            return std::make_reverse_iterator(begin());
        }

        [[nodiscard]]
        constexpr auto crend() const noexcept -> const_reverse_iterator {
            return rend();
        }

        [[nodiscard]]
        static constexpr auto capacity() noexcept -> size_type {
            return N;
        }

        [[nodiscard]]
        static constexpr auto max_size() noexcept -> size_type {
            return N;
        }

        [[nodiscard]]
        constexpr auto size() const noexcept -> size_type {
            return cur_size_;
        }

        [[nodiscard]]
        constexpr auto empty() const noexcept -> bool {
            return cur_size_ == 0;
        }

        [[nodiscard]]
        constexpr auto data() noexcept -> pointer {
            return begin();
        }

        [[nodiscard]]
        constexpr auto data() const noexcept -> const_pointer {
            return begin();
        }

        [[nodiscard]]
        constexpr auto front() noexcept -> reference {
            return *begin();
        }

        [[nodiscard]]
        constexpr auto front() const noexcept -> const_reference {
            return *begin();
        }

        [[nodiscard]]
        constexpr auto back() noexcept -> reference {
            return *rbegin();
        }

        [[nodiscard]]
        constexpr auto back() const noexcept -> const_reference {
            return *rbegin();
        }

        [[nodiscard]]
        constexpr auto operator[] (size_type index) noexcept -> reference {
            return *(begin() + index);
        }

        [[nodiscard]]
        constexpr auto operator[] (size_type index) const noexcept -> const_reference {
            return *(begin() + index);
        }

        [[nodiscard]]
        constexpr auto at(size_type index) -> reference {
            if (index >= cur_size_) throw std::out_of_range{"`index` too big."};
            return (*this)[index];
        }

        [[nodiscard]]
        constexpr auto at(size_type index) const -> const_reference {
            if (index >= cur_size_) throw std::out_of_range{"`index` too big."};
            return (*this)[index];
        }

        constexpr static auto reserve(size_type count) -> void {
            if (count > N) throw std::bad_alloc{};
        }

        constexpr static auto shrink_to_fit() noexcept -> void {}

        constexpr auto resize(size_type count) -> void requires std::default_initializable<value_type> {
            if (count > N) throw std::bad_alloc{};
            if (count >= cur_size_) {
                if (try_emplace_back_n_(count - cur_size_) == nullptr) throw std::bad_alloc{};
            }
            else erase(cbegin() + count, cend());
        }

        constexpr auto resize(size_type count, const_reference value) -> void requires std::copy_constructible<value_type> {
            if (count > N) throw std::bad_alloc{};
            if (count >= cur_size_) {
                if (try_emplace_back_n_(count - cur_size_, value) == nullptr) throw std::bad_alloc{};
            }
            else std::destroy_n(begin() + count, cur_size_ - count);
        }

        template<typename... Args> requires std::constructible_from<value_type, Args...>
        constexpr auto try_emplace_back(Args&&... args) -> pointer {
            return try_emplace_back_n_(1, std::forward<Args>(args)...);
        }

        constexpr auto try_push_back(const_reference value) -> pointer requires std::copy_constructible<value_type> {
            return try_emplace_back(value);
        }

        constexpr auto try_push_back(value_type&& value) -> pointer {
            return try_emplace_back(std::move(value));
        }

        template<typename... Args> requires std::constructible_from<value_type, Args...>
        constexpr auto emplace_back(Args&&... args) -> reference {
            if (try_emplace_back(std::forward<Args>(args)...) == nullptr) throw std::bad_alloc{};
            return back();
        }

        constexpr auto push_back(const_reference value) -> reference requires std::copy_constructible<value_type> {
            return emplace_back(value);
        }

        constexpr auto push_back(value_type&& value) -> reference {
            return emplace_back(std::move(value));
        }

        template<typename... Args> requires std::constructible_from<value_type, Args...>
        constexpr auto unchecked_emplace_back(Args&&... args) -> reference {
            auto p = try_emplace_back(std::forward<Args>(args)...);
            COIO_ASSERT(p != nullptr);
            return *p;
        }

        constexpr auto unchecked_push_back(const_reference value) -> reference {
            return unchecked_emplace_back(value);
        }

        constexpr auto unchecked_push_back(value_type&& value) -> reference {
            return unchecked_emplace_back(std::move(value));
        }

        template<container_compatible_range<value_type> Range>
        constexpr auto append_range(Range&& r) -> void {
            if constexpr (std::ranges::sized_range<Range> or std::ranges::random_access_range<Range>) {
                if (std::ranges::distance(r) + cur_size_ > N) throw std::bad_alloc{};
            }
            auto n = cur_size_;
            try {
                for (auto&& elem : r) {
                    if (n == N) [[unlikely]] throw std::bad_alloc{};
                    std::construct_at(std::addressof(storages_[n]), std::forward<decltype(elem)>(elem));
                    ++n;
                }
                cur_size_ = n;
            }
            catch (...) {
                while (n-- > cur_size_) std::destroy_at(std::addressof(storages_[n]));
                throw;
            }
        }

        template<container_compatible_range<value_type> Range>
        constexpr auto try_append_range(Range&& r) -> std::ranges::borrowed_iterator_t<Range> {
            auto n = cur_size_;
            auto it = std::ranges::begin(r);
            for (; it != std::ranges::end(r) and n != N; ++it) {
                void(try_emplace_back(*it));
                ++n;
            }
            return it;
        }

        template<typename... Args> requires std::constructible_from<value_type, Args...>
        constexpr auto emplace(const_iterator pos, Args&&... args) -> iterator {
            auto index = pos - cbegin();
            emplace_back(std::forward<Args>(args)...);
            shift_right_(index, 1);
            return begin() + index;
        }

        constexpr auto insert(const_iterator pos, size_type count, const_reference value) -> iterator requires std::copyable<value_type> {
            auto index = pos - cbegin();
            if (try_emplace_back_n_(count, value) == nullptr) throw std::bad_alloc{};
            shift_right_(index, count);
            return begin() + index;
        }

        constexpr auto insert(const_iterator pos, const_reference value) -> iterator {
            return insert(pos, 1, value);
        }

        constexpr auto insert(const_iterator pos, value_type&& value) -> iterator {
            return emplace(pos, std::move(value));
        }

        template<cpp17_input_iterator InputIt> requires std::constructible_from<value_type, detail::cpp17_iter_reference_t<InputIt>>
        constexpr auto insert(const_iterator pos, InputIt first, InputIt last) -> iterator {
            if constexpr (cpp17_random_access_iterator<InputIt>) {
                if (std::distance(first, last) + cur_size_ > N) throw std::bad_alloc{};
            }
            auto index = pos - cbegin();
            auto n = cur_size_, old_size = cur_size_;
            try {
                for (auto it = std::move(first); it != last; ++n, ++it) {
                    if (n == N) [[unlikely]] throw std::bad_alloc{};
                    std::construct_at(std::addressof(storages_[n]), *it);
                }
                cur_size_ = n;
            }
            catch (...) {
                while (n-- > cur_size_) std::destroy_at(std::addressof(storages_[n]));
                throw;
            }
            shift_right_(index, cur_size_ - old_size);
            return begin() + index;
        }

        constexpr auto insert(const_iterator pos, std::initializer_list<value_type> ilist) -> iterator {
            return insert(pos, ilist.begin(), ilist.end());
        }

        template<container_compatible_range<value_type> Range>
        constexpr auto insert_range(const_iterator pos, Range&& r) -> iterator {
            auto old_size = cur_size_;
            auto index = pos - cbegin();
            append_range(std::forward<Range>(r));
            shift_right_(index, cur_size_ - old_size);
            return begin() + index;
        }

        constexpr auto erase(const_iterator pos) -> iterator {
            return erase(pos, std::ranges::next(pos));
        }

        constexpr auto erase(const_iterator first, const_iterator last) -> iterator {
            auto index = std::ranges::distance(cbegin(), first);
            if (first != last) [[likely]] {
                auto count = std::ranges::distance(first, last);
                auto [_, first_destroyed] = std::ranges::move(last, cend(), begin() + index);
                std::ranges::destroy(first_destroyed, cend());
                cur_size_ -= count;
            }
            return begin() + index;
        }

        constexpr auto pop_back() -> void {
            COIO_ASSERT(not empty());
            void(erase(std::ranges::prev(end())));
        }

        constexpr auto clear() noexcept -> void {
            void(erase(begin(), end()));
        }

        constexpr auto swap(inplace_vector& other) noexcept(std::is_nothrow_swappable_v<value_type> and std::is_nothrow_move_constructible_v<value_type>) -> void {
            size_type i = 0;
            for (; i < std::min(cur_size_, other.cur_size_); ++i) {
                std::ranges::swap((*this)[i], other[i]);
            }
            if (i < cur_size_) {
                auto first = begin() + i;
                auto last = end();
                other.append_range(std::ranges::subrange{std::make_move_iterator(first), std::make_move_iterator(last)});
                erase(first, last);
            }
            else if (i < other.cur_size_) {
                auto first = other.begin() + i;
                auto last = other.end();
                append_range(std::ranges::subrange{std::make_move_iterator(first), std::make_move_iterator(last)});
                other.erase(first, last);
            }
        }

        friend constexpr auto swap(inplace_vector& lhs, inplace_vector& rhs) noexcept(noexcept(lhs.swap(rhs))) -> void {
            lhs.swap(rhs);
        }

        friend constexpr auto operator== (const inplace_vector& lhs, const inplace_vector& rhs) noexcept(noexcept(std::declval<const_reference>() == std::declval<const_reference>())) -> bool requires std::equality_comparable<value_type> {
            if (lhs.size() != rhs.size()) return false;
            for (size_type i = 0; i < lhs.size(); ++i) {
                if (not(lhs[i] == rhs[i])) return false;
            }
            return true;
        }

        friend constexpr auto operator<=>(const inplace_vector& lhs, const inplace_vector& rhs) noexcept(noexcept(std::declval<const_reference>() <=> std::declval<const_reference>())) requires std::three_way_comparable<value_type> {
            return std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
        }

    private:

        template<typename... Args>
        constexpr auto try_emplace_back_n_(size_type count, Args&&... args) -> pointer {
            if (cur_size_ + count > N) return nullptr;
            auto n = cur_size_;
            try {
                for (; n < cur_size_ + count; ++n) {
                    std::construct_at(std::addressof(storages_[n]), std::forward<Args>(args)...);
                }
                cur_size_ = n;
            }
            catch (...) {
                while (n-- > cur_size_) std::destroy_at(std::addressof(storages_[n]));
                throw;
            }
            return std::addressof(back());
        }

        constexpr void shift_right_(size_type start, size_type n) {
            if (n == 0 or start + n >= cur_size_) return;
            n = cur_size_ - start - n;
            std::ranges::reverse(begin() + start, begin() + start + n);
            std::ranges::reverse(begin() + start + n, end());
            std::ranges::reverse(begin() + start, end());
        }

    private:
        union {
            value_type storages_[N];
        };
        size_type cur_size_ = 0;
    };

    template<typename T, std::size_t N, typename U = T>
    constexpr auto erase(inplace_vector<T, N>& c, const U& value) -> typename inplace_vector<T, N>::size_type {
        auto [first, last] = std::ranges::remove(c, value);
        auto r = std::ranges::distance(first, last);
        void(c.erase(first, last));
        return r;
    }

    template<typename T, std::size_t N>
    constexpr auto erase_if(inplace_vector<T, N>& c, std::predicate<const T&> auto pred) -> typename inplace_vector<T, N>::size_type {
        auto [first, last] = std::ranges::remove_if(c, pred);
        auto r = std::ranges::distance(first, last);
        void(c.erase(first, last));
        return r;
    }
}