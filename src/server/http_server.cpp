#include "backbook/server/http_server.hpp"

#include "backbook/server/json_codec.hpp"

#include <httplib.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace backbook::server {
namespace {

constexpr auto json_content_type = "application/json";
constexpr auto problem_content_type = "application/problem+json";

void set_json_response(httplib::Response& response, const int status,
                       std::string body) {
    response.status = status;
    response.set_content(std::move(body), json_content_type);
}

void set_problem_response(httplib::Response& response,
                          const ProblemDetails& problem) {
    response.status = problem.status;
    response.set_content(encode_problem_response(problem),
                         problem_content_type);
}

[[nodiscard]] ProblemDetails
validation_problem(const int status, std::string title, std::string detail,
                   std::vector<FieldViolation> violations = {}) {
    return ProblemDetails{ProblemCode::ValidationFailed, status,
                          std::move(title), std::move(detail),
                          std::move(violations)};
}

[[nodiscard]] ProblemDetails internal_problem() {
    return ProblemDetails{ProblemCode::InternalError, 500, "Internal error",
                          "The request could not be completed."};
}

[[nodiscard]] bool is_json_content_type(const std::string_view content_type) {
    const auto separator = content_type.find(';');
    const auto media_type = content_type.substr(0U, separator);
    std::string normalised;
    normalised.reserve(media_type.size());
    for (const char character : media_type) {
        if (character != ' ' && character != '\t') {
            normalised.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(character))));
        }
    }
    return normalised == json_content_type;
}

[[nodiscard]] ProblemDetails decode_problem(const CommandDecodeError& error) {
    if (error.code == CommandDecodeErrorCode::MalformedJson) {
        return validation_problem(400, "Malformed JSON",
                                  "The request body is not valid JSON.");
    }
    return validation_problem(422, "Validation failed",
                              "One or more request fields are invalid.",
                              error.violations);
}

void set_read_response(
    httplib::Response& response,
    domain::Outcome<std::string, ResponseEncodingError> encoded) {
    if (!encoded) {
        set_problem_response(response, internal_problem());
        return;
    }
    set_json_response(response, 200, std::move(encoded).value());
}

}  // namespace

class HttpServer::Implementation final {
public:
    Implementation(service::CommandService& command_service,
                   const std::optional<std::filesystem::path>& static_root)
        : command_service_(command_service) {
        server_.set_payload_max_length(maximum_request_body_bytes);
        install_routes();
        install_error_boundary();
        mount_static_root(static_root);
    }

    [[nodiscard]] bool bind_to_port(const std::string& host, const int port) {
        return server_.bind_to_port(host, port);
    }

    [[nodiscard]] int bind_to_any_port(const std::string& host) {
        return server_.bind_to_any_port(host);
    }

    [[nodiscard]] bool listen_after_bind() {
        return server_.listen_after_bind();
    }

    void stop() { server_.stop(); }

private:
    void install_routes() {
        server_.Get("/healthz",
                    [](const httplib::Request&, httplib::Response& response) {
                        set_json_response(response, 200, R"({"status":"ok"})");
                    });

        server_.Post("/api/v1/commands", [this](const httplib::Request& request,
                                                httplib::Response& response) {
            if (!request.has_header("Content-Type") ||
                !is_json_content_type(
                    request.get_header_value("Content-Type"))) {
                set_problem_response(
                    response, validation_problem(
                                  415, "Unsupported media type",
                                  "Content-Type must be application/json."));
                return;
            }
            if (request.body.size() > maximum_request_body_bytes) {
                set_problem_response(
                    response,
                    validation_problem(413, "Request body too large",
                                       "The request body exceeds 64 KiB."));
                return;
            }

            auto command = decode_command_request(request.body);
            if (!command) {
                set_problem_response(response, decode_problem(command.error()));
                return;
            }
            auto executed = command_service_.execute(command.value());
            if (!executed) {
                set_problem_response(
                    response, problem_from_service_error(executed.error()));
                return;
            }
            set_json_response(response, 200,
                              encode_command_response(executed.value()));
        });

        server_.Get("/api/v1/state", [this](const httplib::Request&,
                                            httplib::Response& response) {
            const auto snapshot = command_service_.snapshot();
            set_read_response(response, encode_state_response(*snapshot));
        });

        server_.Get("/api/v1/ledger", [this](const httplib::Request&,
                                             httplib::Response& response) {
            const auto snapshot = command_service_.snapshot();
            set_read_response(response, encode_ledger_response(*snapshot));
        });

        server_.Get("/api/v1/settlements", [this](const httplib::Request&,
                                                  httplib::Response& response) {
            const auto snapshot = command_service_.snapshot();
            set_read_response(response, encode_settlements_response(*snapshot));
        });
    }

    void install_error_boundary() {
        server_.set_error_handler([](const httplib::Request&,
                                     httplib::Response& response) {
            if (response.get_header_value("Content-Type") ==
                problem_content_type) {
                return;
            }
            if (response.status == 413) {
                set_problem_response(
                    response,
                    validation_problem(413, "Request body too large",
                                       "The request body exceeds 64 KiB."));
                return;
            }
            if (response.status == 405) {
                set_problem_response(
                    response,
                    validation_problem(
                        405, "Method not allowed",
                        "The HTTP method is not supported for this route."));
                return;
            }
            set_problem_response(
                response,
                ProblemDetails{ProblemCode::NotFound, 404, "Route not found",
                               "The requested route does not exist."});
        });
        server_.set_exception_handler([](const httplib::Request&,
                                         httplib::Response& response,
                                         std::exception_ptr exception) {
            static_cast<void>(exception);
            set_problem_response(response, internal_problem());
        });
    }

    void
    mount_static_root(const std::optional<std::filesystem::path>& static_root) {
        if (!static_root.has_value()) {
            return;
        }
        std::error_code filesystem_error;
        if (!std::filesystem::is_directory(*static_root, filesystem_error) ||
            filesystem_error) {
            return;
        }
        server_.set_file_extension_and_mimetype_mapping("js",
                                                        "text/javascript");
        server_.set_file_extension_and_mimetype_mapping("mjs",
                                                        "text/javascript");
        server_.set_file_extension_and_mimetype_mapping("css", "text/css");
        server_.set_file_extension_and_mimetype_mapping("map",
                                                        "application/json");
        static_cast<void>(server_.set_mount_point("/", static_root->string()));
    }

    service::CommandService& command_service_;
    httplib::Server server_;
};

HttpServer::HttpServer(service::CommandService& command_service,
                       std::optional<std::filesystem::path> static_root)
    : implementation_(
          std::make_unique<Implementation>(command_service, static_root)) {}

HttpServer::~HttpServer() = default;

bool HttpServer::bind_to_port(const std::string& host, const int port) {
    return implementation_->bind_to_port(host, port);
}

int HttpServer::bind_to_any_port(const std::string& host) {
    return implementation_->bind_to_any_port(host);
}

bool HttpServer::listen_after_bind() {
    return implementation_->listen_after_bind();
}

void HttpServer::stop() { implementation_->stop(); }

}  // namespace backbook::server
