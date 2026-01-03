#pragma once
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include "coio/detail/config.h"

namespace coio {
    /// A dynamic buffer similar to boost::beast::flat_buffer
    ///
    /// Buffer layout:
    /// |<-- consumed -->|<-- readable data -->|<-- prepared -->|<-- free -->|
    /// ^                ^                     ^                ^            ^
    /// data_            in_                   out_             last_        end_
    ///
    template<typename Alloc>
    class basic_flat_buffer {
    public:
        using allocator_type = std::allocator_traits<Alloc>::template rebind_alloc<std::byte>;

    private:
        using alloc_traits = std::allocator_traits<allocator_type>;

    public:
        basic_flat_buffer() = default;

        explicit basic_flat_buffer(const allocator_type& alloc) noexcept : alloc_(alloc) {}

        explicit basic_flat_buffer(std::size_t max_size, const allocator_type& alloc = allocator_type()) noexcept :
            alloc_(alloc), max_(max_size) {}

        basic_flat_buffer(const basic_flat_buffer& other)
            : alloc_(alloc_traits::select_on_container_copy_construction(other.alloc_)),
              max_(other.max_) {
            const std::size_t len = other.size();
            if (len > 0) {
                data_ = alloc_traits::allocate(alloc_, len);
                std::memcpy(data_, other.data_ + other.in_, len);
                out_ = len;
                last_ = len;
                end_ = len;
            }
        }

        basic_flat_buffer(basic_flat_buffer&& other) noexcept
            : alloc_(std::move(other.alloc_)),
              max_(other.max_),
              data_(std::exchange(other.data_, nullptr)),
              in_(std::exchange(other.in_, 0)),
              out_(std::exchange(other.out_, 0)),
              last_(std::exchange(other.last_, 0)),
              end_(std::exchange(other.end_, 0)) {}

        auto operator= (const basic_flat_buffer& other) -> basic_flat_buffer& {
            if (this == &other) {
                return *this;
            }

            static constexpr bool propagate = alloc_traits::propagate_on_container_copy_assignment::value;
            const std::size_t len = other.size();

            if (len == 0) {
                if constexpr (propagate) {
                    if (alloc_ != other.alloc_) {
                        destroy();
                        alloc_ = other.alloc_;
                    }
                }
                clear();
                max_ = other.max_;
                return *this;
            }

            if constexpr (propagate) {
                if (alloc_ != other.alloc_) {
                    std::byte* new_data = alloc_traits::allocate(other.alloc_, len);
                    std::memcpy(new_data, other.data_ + other.in_, len);
                    destroy();
                    alloc_ = other.alloc_;
                    data_ = new_data;
                    in_ = 0;
                    out_ = len;
                    last_ = len;
                    end_ = len;
                    max_ = other.max_;
                    return *this;
                }
            }

            if (len > end_) {
                std::byte* new_data = alloc_traits::allocate(alloc_, len);
                destroy();
                data_ = new_data;
                end_ = len;
            }

            std::memcpy(data_, other.data_ + other.in_, len);
            in_ = 0;
            out_ = len;
            last_ = len;
            max_ = other.max_;

            return *this;
        }

        auto operator= (basic_flat_buffer&& other) noexcept(
            alloc_traits::propagate_on_container_move_assignment::value or
            alloc_traits::is_always_equal::value
        ) -> basic_flat_buffer& {
            if (this == &other) {
                return *this;
            }

            static constexpr bool propagate = alloc_traits::propagate_on_container_move_assignment::value;

            if constexpr (propagate) {
                destroy();
                alloc_ = std::move(other.alloc_);
                steal_from(other);
            }
            else if (alloc_ == other.alloc_) {
                destroy();
                steal_from(other);
            }
            else {
                const std::size_t len = other.size();
                if (len == 0) {
                    clear();
                }
                else {
                    if (len > end_) {
                        std::byte* new_data = alloc_traits::allocate(alloc_, len);
                        destroy();
                        data_ = new_data;
                        end_ = len;
                    }
                    std::memcpy(data_, other.data_ + other.in_, len);
                    in_ = 0;
                    out_ = len;
                    last_ = len;
                }
                other.clear();
            }

            max_ = other.max_;
            return *this;
        }

        ~basic_flat_buffer() {
            destroy();
        }

        [[nodiscard]]
        auto get_allocator() const noexcept -> allocator_type {
            return alloc_;
        }

        [[nodiscard]]
        auto size() const noexcept -> std::size_t {
            return out_ - in_;
        }

        [[nodiscard]]
        auto empty() const noexcept -> bool {
            return in_ == out_;
        }

        [[nodiscard]]
        auto max_size() const noexcept -> std::size_t {
            return max_;
        }

        [[nodiscard]]
        auto capacity() const noexcept -> std::size_t {
            return end_;
        }

        [[nodiscard]]
        auto data() const noexcept -> std::span<const std::byte> {
            return data_ ? std::span{data_ + in_, out_ - in_} : std::span<const std::byte>{};
        }

        [[nodiscard]]
        auto data() noexcept -> std::span<std::byte> {
            return data_ ? std::span{data_ + in_, out_ - in_} : std::span<std::byte>{};
        }

        [[nodiscard]]
        auto cdata() const noexcept -> std::span<const std::byte> { return data(); }

        [[nodiscard]]
        auto prepare(std::size_t n) -> std::span<std::byte> {
            if (n == 0) {
                return {};
            }

            if (data_ and n <= end_ - out_) {
                last_ = out_ + n;
                return {data_ + out_, n};
            }

            const std::size_t len = size();

            if (data_ and n <= end_ - len) {
                if (len > 0) {
                    std::memmove(data_, data_ + in_, len);
                }
                in_ = 0;
                out_ = len;
                last_ = out_ + n;
                return {data_ + out_, n};
            }

            if (n > max_ - len) {
                throw std::length_error("coio::flat_buffer too long");
            }

            std::size_t new_cap = std::max(end_, std::size_t{512});
            while (new_cap < len + n) {
                if (const std::size_t next = new_cap * 2; next > new_cap and next <= max_) {
                    new_cap = next;
                }
                else {
                    new_cap = std::min(len + n, max_);
                    break;
                }
            }

            std::byte* new_data = alloc_traits::allocate(alloc_, new_cap);
            if (len > 0) {
                std::memcpy(new_data, data_ + in_, len);
            }
            destroy();

            data_ = new_data;
            in_ = 0;
            out_ = len;
            last_ = out_ + n;
            end_ = new_cap;

            return {data_ + out_, n};
        }

        auto commit(std::size_t n) noexcept -> void {
            out_ += std::min(n, last_ - out_);
        }

        auto consume(std::size_t n) noexcept -> void {
            in_ += std::min(n, size());
        }

        auto clear() noexcept -> void {
            in_ = 0;
            out_ = 0;
            last_ = 0;
        }

        auto reserve(std::size_t n) -> void {
            if (n > max_) {
                throw std::length_error("coio::flat_buffer reserve exceeds max_size");
            }
            if (n <= end_) {
                return;
            }

            const std::size_t len = size();
            std::byte* new_data = alloc_traits::allocate(alloc_, n);
            if (len > 0) {
                std::memcpy(new_data, data_ + in_, len);
            }
            destroy();

            data_ = new_data;
            in_ = 0;
            out_ = len;
            last_ = out_;
            end_ = n;
        }

        auto shrink_to_fit() -> void {
            const std::size_t len = size();

            if (len == 0) {
                destroy();
                return;
            }

            if (len == end_ and in_ == 0) {
                return;
            }

            if (len == end_) {
                std::memmove(data_, data_ + in_, len);
                in_ = 0;
                out_ = len;
                last_ = len;
                return;
            }

            std::byte* new_data = alloc_traits::allocate(alloc_, len);
            std::memcpy(new_data, data_ + in_, len);
            alloc_traits::deallocate(alloc_, data_, end_);

            data_ = new_data;
            in_ = 0;
            out_ = len;
            last_ = len;
            end_ = len;
        }

        auto swap(
            basic_flat_buffer& other
        ) noexcept(
            alloc_traits::propagate_on_container_swap::value or
            alloc_traits::is_always_equal::value
        ) -> void {
            static constexpr bool propagate = alloc_traits::propagate_on_container_swap::value;
            if constexpr (propagate) {
                std::swap(alloc_, other.alloc_);
            }
            COIO_ASSERT(propagate or alloc_ == other.alloc_);

            std::swap(max_, other.max_);
            std::swap(data_, other.data_);
            std::swap(in_, other.in_);
            std::swap(out_, other.out_);
            std::swap(last_, other.last_);
            std::swap(end_, other.end_);
        }

        friend auto swap(basic_flat_buffer& lhs, basic_flat_buffer& rhs) noexcept(noexcept(lhs.swap(rhs))) -> void {
            lhs.swap(rhs);
        }

    private:
        auto destroy() noexcept -> void {
            if (data_) {
                alloc_traits::deallocate(alloc_, data_, end_);
                data_ = nullptr;
            }
            in_ = 0;
            out_ = 0;
            last_ = 0;
            end_ = 0;
        }

        auto steal_from(basic_flat_buffer& other) noexcept -> void {
            data_ = std::exchange(other.data_, nullptr);
            in_ = std::exchange(other.in_, 0);
            out_ = std::exchange(other.out_, 0);
            last_ = std::exchange(other.last_, 0);
            end_ = std::exchange(other.end_, 0);
        }

    private:
        COIO_NO_UNIQUE_ADDRESS allocator_type alloc_{};
        std::size_t max_ = std::numeric_limits<std::size_t>::max();
        std::byte* data_ = nullptr;
        std::size_t in_ = 0;
        std::size_t out_ = 0;
        std::size_t last_ = 0;
        std::size_t end_ = 0;
    };

    using flat_buffer = basic_flat_buffer<std::allocator<std::byte>>;
}