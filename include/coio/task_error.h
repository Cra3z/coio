#pragma once
#include <stdexcept>
#include <system_error>

namespace coio {

	enum class task_errc {
		no_state,
		broken_promise
	};

}

template <>
struct std::is_error_code_enum<coio::task_errc> : std::true_type {};

namespace coio {

	class task_error final : public std::runtime_error {
	public:
		explicit task_error(task_errc ec) : runtime_error{""}, ec(ec) {}

		[[nodiscard]]
		auto code() const noexcept ->task_errc {
			return ec;
		}

		[[nodiscard]]
		auto what() const noexcept ->const char* override {
			switch (ec) {
			case task_errc::no_state:
				return "no state";
			case task_errc::broken_promise:
				return "broken promise";
			default:
				return "unknown error";
			}
		}
	private:
		task_errc ec;
	};

}