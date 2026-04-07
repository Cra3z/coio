#include <coio/asyncio/io.h>
#include <coio/utils/flat_buffer.h>
#include "json_rpc.h"

auto generate_id() noexcept -> int {
    static int id = 0;
    return id++;
}

auto call(json_rpc::tcp_socket& socket, const json_rpc::value& request) -> coio::task<json_rpc::value> {
    coio::flat_buffer buffer;
    const std::string line = json_rpc::dump(request) + '\n';
    co_await coio::async_write(socket, coio::as_bytes(line));

    const auto n = co_await coio::async_read_until(socket, buffer, '\n');
    const auto data = buffer.data();
    auto value = json_rpc::parse(std::string_view{reinterpret_cast<const char*>(data.data()), n});
    buffer.consume(n);
    co_return value;
}

auto run_client(json_rpc::io_context::scheduler sched) -> coio::task<> try {
    static auto json_rpc_version = "2.0";
    json_rpc::tcp_socket socket{sched};
    co_await socket.async_connect({coio::ipv4_address::loopback(), 9090});
    ::println("connected to {}", socket.remote_endpoint());

    ::println("{}", json_rpc::dump(co_await call(socket, json_rpc::object{
        {"jsonrpc", json_rpc_version},
        {"method", "add"},
        {"params", json_rpc::array{100, 50}},
        {"id", generate_id()},
    })));

    ::println("{}", json_rpc::dump(co_await call(socket, json_rpc::object{
        {"jsonrpc", json_rpc_version},
        {"method", "subtract"},
        {"params", json_rpc::array{100, 50}},
        {"id", generate_id()},
    })));

    ::println("{}", json_rpc::dump(co_await call(socket, json_rpc::array{
        json_rpc::object{
            {"jsonrpc", json_rpc_version},
            {"method", "add"},
            {"params", json_rpc::array{114, 514}},
            {"id", generate_id()},
        },
        json_rpc::object{
            {"jsonrpc", json_rpc_version},
            {"method", "subtract"},
            {"params", json_rpc::array{1919, 810}},
            {"id", generate_id()},
        }
    })));
}
catch (const std::exception& e) {
    ::println("client error: {}", e.what());
}

auto main() -> int {
    json_rpc::io_context context;
    coio::async_scope scope;
    scope.spawn(run_client(context.get_scheduler()));
    context.run();
    coio::this_thread::sync_wait(scope.join());
}
