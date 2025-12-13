#pragma once
// from asio::streambuf
#include <cstring>
#include <limits>
#include <span>
#include <streambuf>
#include <vector>

namespace coio {
    template<typename Allocator>
    class basic_streambuf : public std::streambuf {
    public:
        explicit basic_streambuf(
            std::size_t max_size = std::numeric_limits<std::size_t>::max(),
            const Allocator& allocator = Allocator()
        ) : max_size_(max_size), buffer_(allocator) {
            const std::size_t pend = std::min(max_size_, buffer_delta);
            buffer_.resize(std::max(pend, std::size_t{1}));
            setg(&buffer_[0], &buffer_[0], &buffer_[0]);
            setp(&buffer_[0], &buffer_[0] + pend);
        }

        basic_streambuf(const basic_streambuf&) = delete;

        ~basic_streambuf() override = default;

        auto operator= (const basic_streambuf&) -> basic_streambuf& = delete;

        [[nodiscard]]
        auto size() const noexcept -> std::size_t {
            return pptr() - gptr();
        }

        [[nodiscard]]
        auto max_size() const noexcept -> std::size_t {
            return max_size_;
        }

        [[nodiscard]]
        auto capacity() const noexcept -> std::size_t {
            return buffer_.capacity();
        }

        [[nodiscard]]
        auto data() const noexcept -> std::span<const std::byte> {
            return {reinterpret_cast<const std::byte*>(gptr()), size()};
        }

        [[nodiscard]]
        auto prepare(std::size_t n) -> std::span<std::byte> {
            reserve(n);
            return {reinterpret_cast<std::byte*>(pptr()), n};
        }

        auto commit(std::size_t n) -> void {
            n = std::min<std::size_t>(n, epptr() - pptr());
            pbump(int(n));
            setg(eback(), gptr(), pptr());
        }

        auto consume(std::size_t n) -> void {
            if (egptr() < pptr()) setg(&buffer_[0], gptr(), pptr());
            if (gptr() + n > pptr()) n = pptr() - gptr();
            gbump(int(n));
        }

    private:
        auto underflow() -> int_type override {
            if (gptr() < pptr()) {
                setg(&buffer_[0], gptr(), pptr());
                return traits_type::to_int_type(*gptr());
            }
            return traits_type::eof();
        }

        auto overflow(int_type c) -> int_type override {
            if (!traits_type::eq_int_type(c, traits_type::eof())) {
                if (pptr() == epptr()) {
                    const std::size_t buffer_size = pptr() - gptr();
                    if (buffer_size < max_size_ and max_size_ - buffer_size < buffer_delta) {
                        reserve(max_size_ - buffer_size);
                    }
                    else {
                        reserve(buffer_delta);
                    }
                }

                *pptr() = traits_type::to_char_type(c);
                pbump(1);
                return c;
            }
            return traits_type::not_eof(c);
        }

        auto reserve(std::size_t n) -> void {
            std::size_t gnext = gptr() - &buffer_[0];
            std::size_t pnext = pptr() - &buffer_[0];
            std::size_t pend = epptr() - &buffer_[0];

            if (n <= pend - pnext) return;

            if (gnext > 0) {
                pnext -= gnext;
                std::memmove(&buffer_[0], &buffer_[0] + gnext, pnext);
            }

            if (n > pend - pnext) {
                if (n <= max_size_ and pnext <= max_size_ - n) {
                    pend = pnext + n;
                    buffer_.resize(std::max(pend, std::size_t{1}));
                }
                else {
                    throw std::length_error{"coio::streambuf too long"};
                }
            }

            setg(&buffer_[0], &buffer_[0], &buffer_[0] + pnext);
            setp(&buffer_[0] + pnext, &buffer_[0] + pend);
        }

    private:
        static constexpr std::size_t buffer_delta = 128;
        std::size_t max_size_;
        std::vector<char, Allocator> buffer_;
    };

    using streambuf = basic_streambuf<std::allocator<char>>;
}