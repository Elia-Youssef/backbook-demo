#include "backbook/server/demo.hpp"
#include "backbook/server/http_server.hpp"
#include "backbook/service/command_service.hpp"
#include "backbook/storage/journal_store.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

struct Options final {
    std::string bind_address{"127.0.0.1"};
    int port{8080};
    std::optional<std::filesystem::path> journal_path{};
    bool allow_non_loopback{false};
    bool no_ui{false};
    bool demo{false};
    bool help{false};
};

[[nodiscard]] bool is_loopback(const std::string_view address) {
    return address == "127.0.0.1" || address == "localhost" || address == "::1";
}

[[nodiscard]] std::optional<int> parse_port(const std::string_view value) {
    int port = 0;
    const auto parsed =
        std::from_chars(value.data(), value.data() + value.size(), port);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        port < 0 || port > 65'535) {
        return std::nullopt;
    }
    return port;
}

[[nodiscard]] std::optional<Options> parse_options(const int argument_count,
                                                   char* arguments[]) {
    Options options;
    for (int index = 1; index < argument_count; ++index) {
        const std::string_view argument(arguments[index]);
        if (argument == "--help") {
            options.help = true;
        } else if (argument == "--allow-non-loopback") {
            options.allow_non_loopback = true;
        } else if (argument == "--no-ui") {
            options.no_ui = true;
        } else if (argument == "--demo") {
            options.demo = true;
        } else if (argument == "--bind") {
            if (index + 1 >= argument_count) {
                return std::nullopt;
            }
            options.bind_address = arguments[++index];
        } else if (argument == "--port") {
            if (index + 1 >= argument_count) {
                return std::nullopt;
            }
            const auto port = parse_port(arguments[++index]);
            if (!port.has_value()) {
                return std::nullopt;
            }
            options.port = *port;
        } else if (argument == "--journal") {
            if (index + 1 >= argument_count) {
                return std::nullopt;
            }
            options.journal_path = std::filesystem::path(arguments[++index]);
        } else {
            return std::nullopt;
        }
    }
    if (options.demo && options.journal_path.has_value()) {
        return std::nullopt;
    }
    if (!is_loopback(options.bind_address) && !options.allow_non_loopback) {
        return std::nullopt;
    }
    return options;
}

void print_usage() {
    std::cout
        << "Usage: backbook-server [options]\n"
        << "  --demo                 Load the canonical demo into an isolated "
           "journal\n"
        << "  --journal <path>       Use the specified append-only journal\n"
        << "  --bind <address>       Listener address (default 127.0.0.1)\n"
        << "  --port <0-65535>       Listener port (default 8080; 0 selects a "
           "free port)\n"
        << "  --allow-non-loopback   Explicitly permit a non-loopback bind\n"
        << "  --no-ui                Disable static UI serving\n"
        << "  --help                 Show this help\n";
}

[[nodiscard]] std::optional<std::filesystem::path> create_demo_journal_path() {
    std::error_code filesystem_error;
    const auto temporary_root =
        std::filesystem::temp_directory_path(filesystem_error);
    if (filesystem_error) {
        return std::nullopt;
    }

    std::random_device random;
    for (std::uint32_t attempt = 0U; attempt < 32U; ++attempt) {
        const auto token = (static_cast<std::uint64_t>(random()) << 32U) |
                           static_cast<std::uint64_t>(random());
        auto directory =
            temporary_root / ("backbook-demo-" + std::to_string(token));
        filesystem_error.clear();
        if (std::filesystem::create_directory(directory, filesystem_error)) {
            return directory / "backbook.journal";
        }
        if (filesystem_error) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::filesystem::path>
find_from_ancestors(std::filesystem::path start,
                    const std::filesystem::path& relative,
                    const bool require_directory) {
    std::error_code filesystem_error;
    for (std::uint32_t depth = 0U; depth < 6U; ++depth) {
        const auto candidate = start / relative;
        const bool found =
            require_directory
                ? std::filesystem::is_directory(candidate, filesystem_error)
                : std::filesystem::is_regular_file(candidate, filesystem_error);
        if (!filesystem_error && found) {
            return candidate;
        }
        filesystem_error.clear();
        if (!start.has_parent_path() || start.parent_path() == start) {
            break;
        }
        start = start.parent_path();
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::filesystem::path>
locate_resource(const std::filesystem::path& relative,
                const std::filesystem::path& executable,
                const bool require_directory) {
    std::error_code filesystem_error;
    const auto current = std::filesystem::current_path(filesystem_error);
    if (!filesystem_error) {
        auto located =
            find_from_ancestors(current, relative, require_directory);
        if (located.has_value()) {
            return located;
        }
    }
    filesystem_error.clear();
    const auto absolute_executable =
        std::filesystem::absolute(executable, filesystem_error);
    if (filesystem_error) {
        return std::nullopt;
    }
    return find_from_ancestors(absolute_executable.parent_path(), relative,
                               require_directory);
}

[[nodiscard]] int run(const Options& options,
                      const std::filesystem::path& executable) {
    if (!is_loopback(options.bind_address)) {
        std::cerr << "Warning: Backbook is being bound beyond the local "
                     "loopback interface.\n";
    }

    const auto limits = backbook::server::canonical_demo_limits();
    if (!limits) {
        std::cerr << "Unable to construct the configured limit hierarchy.\n";
        return 1;
    }

    std::filesystem::path journal_path{"backbook.journal"};
    if (options.demo) {
        const auto unique_path = create_demo_journal_path();
        if (!unique_path.has_value()) {
            std::cerr << "Unable to create an isolated demo journal.\n";
            return 1;
        }
        journal_path = *unique_path;
    } else if (options.journal_path.has_value()) {
        journal_path = *options.journal_path;
    }

    auto created = backbook::service::CommandService::create(
        std::make_unique<backbook::storage::FileJournalStore>(journal_path),
        std::move(limits).value());
    if (!created) {
        std::cerr << "Unable to recover the command journal.\n";
        return 1;
    }
    auto command_service = std::move(created).value();

    if (options.demo) {
        const auto seed_path = locate_resource(
            std::filesystem::path("demo") / "day1.jsonl", executable, false);
        if (!seed_path.has_value()) {
            std::cerr << "Unable to locate the canonical demo seed.\n";
            return 1;
        }
        const auto seeded =
            backbook::server::run_seed_file(*command_service, *seed_path);
        if (!seeded) {
            std::cerr << "Unable to execute the canonical demo seed.\n";
            return 1;
        }
        const auto verified = backbook::server::verify_canonical_demo(
            seeded.value(), *command_service->snapshot());
        if (!verified) {
            std::cerr
                << "The canonical demo did not reach its expected state.\n";
            return 1;
        }
        std::cout << "Demo journal: " << journal_path.string() << '\n'
                  << "Accepted commands: " << verified.value().accepted_count
                  << '\n'
                  << "Expected rejections: " << verified.value().rejected_count
                  << '\n'
                  << "State fingerprint: 0x" << std::hex << std::setw(16)
                  << std::setfill('0') << verified.value().state_fingerprint
                  << std::dec << '\n';
    }

    std::optional<std::filesystem::path> static_root;
    if (!options.no_ui) {
        static_root = locate_resource(std::filesystem::path("web") / "dist",
                                      executable, true);
    }
    backbook::server::HttpServer server(*command_service,
                                        std::move(static_root));

    int bound_port = options.port;
    const bool bound =
        options.port == 0
            ? (bound_port = server.bind_to_any_port(options.bind_address)) > 0
            : server.bind_to_port(options.bind_address, options.port);
    if (!bound) {
        std::cerr << "Unable to bind the HTTP listener.\n";
        return 1;
    }

    std::cout << "Backbook listening at http://" << options.bind_address << ':'
              << bound_port << '\n'
              << std::flush;
    if (!server.listen_after_bind()) {
        std::cerr << "The HTTP listener stopped unexpectedly.\n";
        return 1;
    }
    return 0;
}

}  // namespace

int main(const int argument_count, char* arguments[]) {
    const auto options = parse_options(argument_count, arguments);
    if (!options.has_value()) {
        print_usage();
        return 2;
    }
    if (options->help) {
        print_usage();
        return 0;
    }
    try {
        return run(*options, std::filesystem::path(arguments[0]));
    } catch (...) {
        std::cerr
            << "Backbook stopped because of an unexpected boundary failure.\n";
        return 1;
    }
}
