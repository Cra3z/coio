#include <array>
#include <coio/asyncio/io.h>
#include <coio/utils/flat_buffer.h>
#include <coio/utils/signal_set.h>
#include "json_rpc.h"

auto get_operands(const json_rpc::value& params) -> std::optional<std::array<json_rpc::integer, 2>> {
    if (params.is<json_rpc::array>()) {
        const auto& array = params.as<json_rpc::array>();
        if (array.size() != 2 or not array[0].is<json_rpc::integer>() or not array[1].is<json_rpc::integer>()) {
            return std::nullopt;
        }
        return std::array{array[0].as<json_rpc::integer>(), array[1].as<json_rpc::integer>()};
    }
    else if (params.is<json_rpc::object>()) {
        const auto& object = params.as<json_rpc::object>();
        if (object.size() != 2) {
            return std::nullopt;
        }
        if (not object.contains("lhs") or not object.at("lhs").is<json_rpc::integer>()) {
            return std::nullopt;
        }
        if (not object.contains("rhs") or not object.at("rhs").is<json_rpc::integer>()) {
            return std::nullopt;
        }
        return std::array{object.at("lhs").as<json_rpc::integer>(), object.at("rhs").as<json_rpc::integer>()};
    }
    return std::nullopt;
}

auto add(json_rpc::integer lhs, json_rpc::integer rhs) noexcept -> json_rpc::integer {
    return lhs + rhs;
}

auto subtract(json_rpc::integer lhs, json_rpc::integer rhs) noexcept -> json_rpc::integer {
    return lhs - rhs;
}

auto dispatch(const json_rpc::value& id, const std::string& method, const json_rpc::value& params) -> json_rpc::object {
    auto operands = get_operands(params);
    if (not operands.has_value()) {
        return {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", json_rpc::object{
                {"code", json_rpc::errc::invalid_params},
                {"message", "invalid params"}
            }}
        };
    }
    const auto [lhs, rhs] = *operands;
    json_rpc::integer result = 0;
    if (method == "add") result = add(lhs, rhs);
    else if (method == "subtract") result = subtract(lhs, rhs);
    else {
        return {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", json_rpc::object{
                {"code", json_rpc::errc::method_not_found},
                {"message", "method not found"}
            }}
        };
    }
    return  {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
}

auto handle_object(const json_rpc::object& request) -> json_rpc::object {
    auto id = request.contains("id") ? request.at("id") : nullptr;
    if (not request.contains("jsonrpc") or not request.contains("method")) {
        return  {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", json_rpc::object{
                {"code", json_rpc::errc::invalid_request},
                {"message", "invalid JSON-RPC request"},
            }}
        };
    }
    auto version = request.at("jsonrpc");
    if (version != "2.0") {
        return {
            {"jsonrpc", version},
            {"id", id},
            {"error", json_rpc::object{
                {"code", json_rpc::errc::invalid_request},
                {"message",  "invalid JSON-RPC version"},
            }}
        };
    }
    auto method = request.at("method");
    if (not method.is<json_rpc::string>()) {
        return {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", json_rpc::object{
                {"code", json_rpc::errc::method_not_found},
                {"message",  "method not found"},
            }}
        };
    }
    return dispatch(id, method.as<std::string>(), request.contains("params") ? request.at("params") : json_rpc::array{});
}

auto handle_array(const json_rpc::array& requests) -> json_rpc::array {
    json_rpc::array response;
    for (const auto& request : requests) response.emplace_back(handle_object(request.as<json_rpc::object>()));
    return response;
}

auto handle_connection(json_rpc::tcp_socket socket) -> coio::task<> {
    auto remote_endpoint = socket.remote_endpoint();
    ::debug("new connection from [{}]", remote_endpoint);
    try {
        coio::flat_buffer buffer;
        while (true) {
            const auto n = co_await coio::async_read_until(socket, buffer, '\n');
            const auto data = buffer.data();
            const std::string_view line{reinterpret_cast<const char*>(data.data()), n};
            json_rpc::value response = json_rpc::object{
                {"jsonrpc", "2.0"},
                {"id", nullptr},
                {"error", json_rpc::object{
                    {"code", json_rpc::errc::parse_error},
                    {"message",  "parse error"},
                }}
            };

            bool has_continuation = false;
            try {
                auto json = json_rpc::parse(line);
                buffer.consume(n);
                if (json.is<json_rpc::object>()) {
                    response = handle_object(json.as<json_rpc::object>());
                    has_continuation = true;
                }
                else if (json.is<json_rpc::array>()) {
                    const auto& requests = json.as<json_rpc::array>();
                    if (std::ranges::all_of(requests, [](const json_rpc::value& elem) noexcept {
                        return elem.is<json_rpc::object>();
                    })) {
                        response = handle_array(requests);
                        has_continuation = true;
                    }
                }
            }
            catch (json_rpc::bad_json& e) {}
            auto response_data = json_rpc::dump(response) + '\n';
            co_await coio::async_write(socket, coio::as_bytes(response_data));
            if (not has_continuation) co_return;
        }
    }
    catch (const std::exception& e) {
        ::debug("connection with [{}] broken because \"{}\"", remote_endpoint, e.what());
    }
}

auto start_server(json_rpc::io_context::scheduler sched, coio::async_scope& scope) -> coio::task<> try {
    json_rpc::tcp_acceptor acceptor{sched, coio::endpoint{coio::ipv4_address::any(), 9090}};
    ::debug("JSON-RPC server listening on {}", acceptor.local_endpoint());
    while (true) {
        scope.spawn(handle_connection(co_await acceptor.async_accept()));
    }
}
catch (const std::system_error& e) {
    ::println("acceptor error: {}", e.what());
}

auto signal_watchdog(json_rpc::io_context& context) -> coio::task<> {
    coio::signal_set signals{SIGINT, SIGTERM};
    const int signum = co_await signals.async_wait();
    ::debug("server stopping: signal ({}){}", signum, coio::strsignal(signum));
    context.request_stop();
}

auto main() -> int {
    json_rpc::io_context context;
    coio::async_scope scope;
    scope.spawn(start_server(context.get_scheduler(), scope));
    scope.spawn(signal_watchdog(context));
    context.run();
    coio::this_thread::sync_wait(scope.join());
}
