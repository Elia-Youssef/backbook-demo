#pragma once

#include "backbook/service/command_service.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace backbook::server {

inline constexpr std::size_t maximum_request_body_bytes = 64U * 1024U;

class HttpServer final {
public:
    explicit HttpServer(
        service::CommandService& command_service,
        std::optional<std::filesystem::path> static_root = std::nullopt);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
    HttpServer(HttpServer&&) = delete;
    HttpServer& operator=(HttpServer&&) = delete;

    [[nodiscard]] bool bind_to_port(const std::string& host, int port);
    [[nodiscard]] int bind_to_any_port(const std::string& host);
    [[nodiscard]] bool listen_after_bind();
    void stop();

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

}  // namespace backbook::server
