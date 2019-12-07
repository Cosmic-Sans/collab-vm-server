#include "CollabVm.capnp.h"
#include "Guacamole.capnp.h"
#include <capnp/any.h>
#include <capnp/blob.h>
#include <capnp/common.h>
#include <capnp/dynamic.h>
#include <capnp/layout.h>
#include <capnp/schema.h>
#include "GuacamoleClient.hpp"
#include <argon2.h>
#include <openssl/opensslv.h>
#include <sqlite3.h>
#include <cstdint>
#include <clipp.h>
#include <iostream>
#include <sqlite_modern_cpp.h>
#include <string>
#include <algorithm>
#include <thread>
#include <rfb/rfbconfig.h>
#include <freerdp/freerdp.h>
#include <cairo/cairo.h>

#include "CollabVmServer.hpp"
#include "WebSocketServer.hpp"

int main(int argc, char* argv[]) {
  using namespace clipp;
  using namespace std::string_literals;

  auto host = "localhost"s;
  // Use half the cores so the remaining can be used by
  // the hypervisor and Guacamole client threads
  const auto cores = std::thread::hardware_concurrency();
  auto threads = std::max(cores / 2, 1u);
  auto port = 0u;
  auto root = "."s;
  auto auto_start_vms = true;
  auto invalid_arguments = std::vector<std::string>();
  enum {
    start,
    help,
    version
  } mode = start;
  const auto cli_arguments = (
      (option("--host", "-l") & value("address", host))
        .doc("ip or host to listen on (default: localhost)"),
      (option("--threads", "-t") & integer("number", threads))
        .doc("the number of threads the server will use (default: "
          + std::to_string(threads) + " - half the number of cores)"),
      (option("--port", "-p") & integer("number", port))
        .doc("the port to listen on (default: random)"),
      (option("--root", "-r") & value("path", root))
        .doc("the root directory to serve files from (default: '.')"),
      option("--cert", "-c") // TODO: use this argument
        .doc("path to PEM certificate to use for SSL/TLS"),
      option("--no-autostart", "-n").set(auto_start_vms, false)
        .doc("don't automatically start any VMs"),
      option("--version", "-v").set(mode, version)
        .doc("show version and dependencies"),
      option("--help", "-h").set(mode, help)
        .doc("show this help message"),
      any_other(invalid_arguments)
    );

  if (!parse(argc, argv, cli_arguments)
      || !invalid_arguments.empty()
      || mode == help) {
    std::for_each(
      invalid_arguments.begin(),
      invalid_arguments.end(),
      [](const auto& arg)
      {
        std::cout << "invalid argument '" << arg << "'\n";
      });
    std::cout << usage_lines(cli_arguments, "collab-vm-server") << '\n'
      << documentation(cli_arguments) << std::endl;
    return 0;
  }
  if (mode == version) {
    std::cout << "collab-vm-server " BOOST_STRINGIZE(PROJECT_VERSION) "\n\n"
      "Third-Party Libraries:\n"
      "Argon2 " << ARGON2_VERSION_NUMBER << "\n"
      "Boost " << BOOST_VERSION / 100000 << '.'
        << BOOST_VERSION / 100 % 1000 << '.'
        << BOOST_VERSION % 100 << "\n"
      "cairo " << cairo_version_string() << "\n"
      "Cap'n Proto " BOOST_STRINGIZE(CAPNP_VERSION_STR) "\n"
      "FreeRDP " << freerdp_get_version_string() << "\n"
      "Guacamole (patched)" "\n"
      LIBVNCSERVER_PACKAGE_STRING "\n"
      "sqlite modern cpp " << MODERN_SQLITE_VERSION / 1000000 << '.'
        << MODERN_SQLITE_VERSION / 1000 % 1000 << '.'
        << MODERN_SQLITE_VERSION / 1000 % 1000 << "\n"
      "OpenSSL " OPENSSL_VERSION_TEXT "\n"
      "SQLite3 " SQLITE_VERSION "\n" << std::endl;
    return 0;
  }

  using Server = CollabVm::Server::CollabVmServer<CollabVm::Server::WebServer>;
  Server(root).Start(threads, host, port, auto_start_vms);
}
