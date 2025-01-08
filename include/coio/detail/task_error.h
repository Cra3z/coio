#pragma once
#include <stdexcept>
#include <system_error>
#include "../utils/utility.h"

namespace coio {

    enum class task_errc {
        no_state,
        already_retrieved
    };

}

template <>
struct std::is_error_code_enum<coio::task_errc> : std::true_type {};

namespace coio {

    class task_error : public std::exception {
    public:
        explicit task_error(task_errc ec) : ec(ec) {}

        [[nodiscard]]
        auto code() const noexcept ->task_errc {
            return ec;
        }

        [[nodiscard]]
        auto what() const noexcept ->const char* override {
            switch (ec) {
            case task_errc::no_state:
                return "no associated coroutines for this task.";
            case task_errc::already_retrieved:
                return "the result of task has been retrieved.";
            default:
                unreachable();
            }
        }
    private:
        task_errc ec;
    };

}