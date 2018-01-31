#include <argon2.h>
#include <openssl/opensslv.h>
#include <sqlite3.h>
#include <cstdint>
#include <boost/program_options.hpp>
#include <iostream>
#include <odb/version.hxx>
#include <string>
#include <thread>
#include <rfb/rfbclient.h>
#undef min
#undef max
#include <freerdp/api.h>
#define FREERDP_API
#include <freerdp/freerdp.h>
#include <cairo/cairo.h>

#include "CollabVmServer.hpp"
#include "WebSocketServer.hpp"

template <typename TThreadPool>
using Server =
    CollabVm::Server::CollabVmServer<CollabVm::Server::WebServer<TThreadPool>>;

template <bool SingleThreaded>
void Start(const std::string& root,
           const std::uint8_t threads,
           const std::string host,
           uint16_t port,
           bool auto_start) {
  using ThreadPool = CollabVm::Server::ThreadPool<SingleThreaded>;
  Server<ThreadPool>(root, threads).Start(host, port, auto_start);
}

int main(const int argc, const char* argv[]) {
  namespace po = boost::program_options;

  po::options_description desc("Options");
  desc.add_options()("help,h", "display help message")("version,v",
                                                       "display version")(
      "host,h", po::value<std::string>()->default_value("localhost"),
      "ip or host to listen on")("port,p",
                                 po::value<uint16_t>()->default_value(80))(
      "root,r", po::value<std::string>()->default_value("."),
      "the root directory to serve files from")(
      "cert,c", po::value<std::string>(), "PEM certificate to use for SSL/TLS")(
      "no-autostart,n", "don't automatically start any VMs")(
      "threads,t", po::value<std::uint32_t>()->default_value(0, "number of cores"),
      "the number of threads the server will use");

  po::variables_map vars;
  try {
    po::positional_options_description p;
    p.add("help", -1);
    po::store(
        po::command_line_parser(argc, argv).options(desc).positional(p).run(),
        vars);

    if (vars.count("help")) {
      std::cout << desc << std::endl;
      return 0;
    }

    if (vars.count("version")) {
      std::cout << "collab-vm-server " BOOST_STRINGIZE(PROJECT_VERSION) "\n\n"
        "Third-Party Libraries:\n"
//        "Argon2 " ARGON2_VERSION_NUMBER "\n"
        "Beast " BOOST_STRINGIZE(BOOST_BEAST_VERSION) "\n"
        "Boost " << BOOST_VERSION / 100000 << '.'
          << BOOST_VERSION / 100 % 1000 << '.'
          << BOOST_VERSION % 100 << "\n"
				"cairo " << cairo_version_string() << "\n"
        "Cap'n Proto " BOOST_STRINGIZE(CAPNP_VERSION_STR) "\n"
				"FreeRDP " << freerdp_get_version_string() << "\n"
        "ODB " LIBODB_VERSION_STR "\n"
        "OpenSSL " OPENSSL_VERSION_TEXT "\n"
        "SQLite3 " << SQLITE_VERSION << '\n' << std::endl;
      return 0;
    }

    po::notify(vars);
  } catch (po::unknown_option& e) {
    std::cerr << "Unknown option '" << e.get_option_name() << '\'' << std::endl;
    return 1;
  } catch (po::error& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
  std::string host = vars["host"].as<std::string>();
  std::uint16_t port = vars["port"].as<std::uint16_t>();
  std::string root = vars["root"].as<std::string>();
  auto threads = vars["threads"].as<std::uint32_t>();

  if (!threads) {
		const auto cores = std::thread::hardware_concurrency();
		// Use half the cores, so the hypervisor can use some
	  threads = cores > 2 ? cores / 2 : 1;
  }

  const auto auto_start = !vars.count("no-autostart");
  if (threads == 1) {
    Start<true>(root, threads, host, port, auto_start);
  } else {
    Start<false>(root, threads, host, port, auto_start);
  }
}