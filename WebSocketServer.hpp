#pragma once
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/filesystem.hpp>
#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <variant>
#include <vector>
#include <list>
#include "StrandGuard.hpp"
#include "fields_alloc.hpp"
// #include "file_body.hpp"

namespace CollabVm::Server {
namespace asio = boost::asio;
namespace beast = boost::beast;

template <typename TServer, typename TThreadPool>
class WebServerSocket : public std::enable_shared_from_this<
                            WebServerSocket<TServer, TThreadPool>> {
 public:
  WebServerSocket(asio::io_context& io_context,
                  const boost::filesystem::path& doc_root)
      : socket_(io_context, io_context),
        request_deadline_(io_context,
                          std::chrono::steady_clock::time_point::max()),
        alloc_(8192),
        doc_root_(doc_root) {}

  virtual ~WebServerSocket() = default;

  void Start() {
    socket_.dispatch([ this,
                       self = this->shared_from_this() ](auto& socket) mutable {
      boost::system::error_code ec;
      ip_address_ = socket.socket.lowest_layer().remote_endpoint(ec).address();
      socket.socket.set_option(asio::ip::tcp::no_delay(true), ec);
      ReadHttpRequest(std::move(self));
    });
  }

  class MessageBuffer : public std::enable_shared_from_this<MessageBuffer>
  {
  public:
    MessageBuffer() = default;
    MessageBuffer(const MessageBuffer&) = delete;

  private:
    friend class WebServerSocket;
    virtual void StartRead(std::shared_ptr<WebServerSocket>&& socket) = 0;
  };

  virtual std::shared_ptr<MessageBuffer> CreateMessageBuffer() = 0;

  template<typename TMessageBuffer>
  void ReadWebSocketMessage(std::shared_ptr<WebServerSocket>&& self,
                            std::shared_ptr<TMessageBuffer>&& buffer_ptr) {
    socket_.dispatch([this, self = std::move(self),
                      buffer_ptr = std::move(buffer_ptr)](auto& socket) mutable {
      auto& buffer = buffer_ptr->GetBuffer();
      socket.websocket.async_read(
          buffer, socket_.wrap([
            this, self = std::move(self), buffer_ptr = std::move(buffer_ptr)
          ](auto& sockets, const boost::system::error_code& ec,
                  std::size_t bytes_transferred) mutable {
            if (ec) {
              Close();
              return;
            }
            OnMessage(std::move(buffer_ptr));
            CreateMessageBuffer()->StartRead(std::move(self));
          }));
    });
  }

  void ReadHttpRequest(std::shared_ptr<WebServerSocket>&& self) {
    // Request must be fully processed within 60 seconds.
    request_deadline_.expires_after(std::chrono::seconds(60));

    parser_.emplace(std::piecewise_construct, std::make_tuple(),
                    std::make_tuple(alloc_));

    socket_.dispatch([ this, self = std::move(self) ](auto& socket) {
      beast::http::async_read_header(
          socket.socket, buffer_, *parser_,
          socket_.wrap([ this, self = std::move(self) ](
              auto& sockets, const boost::system::error_code ec,
              std::size_t bytes_transferred) mutable {
            if (ec) {
              return;
            }
            auto& request = parser_->get();
            if (request.method() == beast::http::verb::get) {
              // Accept WebSocket connections
              if (request.target() == "/") {
                const auto connection_header =
                    request.find(beast::http::field::connection);
                if (connection_header != request.end() &&
                    beast::http::token_list(connection_header->value())
                        .exists("upgrade")) {
                  const auto upgrade_header =
                      request.find(beast::http::field::upgrade);
                  if (upgrade_header != request.end() &&
                      beast::http::token_list(upgrade_header->value())
                          .exists("websocket")) {
                    buffer_.consume(buffer_.size());
                    sockets.websocket.async_accept_ex(
                        request,
                        [](beast::websocket::response_type& res) {
                          res.set(beast::http::field::server,
                                  "collab-vm-server");
                        },
                        socket_.wrap([ this, self = std::move(self) ](
                            auto& sockets,
                            const boost::system::error_code ec) mutable {
                          if (ec) {
                            Close();
                            return;
                          }
                          OnConnect();
                          sockets.websocket.binary(true);
                          CreateMessageBuffer()->StartRead(std::move(self));
                        }));
                    return;
                  }
                }
              }

              // Serve static content from doc root
              boost::filesystem::path path(
                  request.target().substr(1).to_string());
              // Disallow relative paths
              if (std::none_of(path.begin(), path.end(), [](const auto& e) {
                    return e == ".." || e == ".";
                  })) {
                auto err = boost::system::error_code();
                path = boost::filesystem::canonical(path, doc_root_, err);

                // Verify that the path exists and is within the doc root
                if (!err && path.compare(doc_root_) >= 0 &&
                    std::equal(doc_root_.begin(), doc_root_.end(),
                               path.begin())) {
                  auto resp = beast::http::response<beast::http::file_body>();
                  resp.result(beast::http::status::ok);
                  resp.version(request.version());
                  resp.set(beast::http::field::server, "collab-vm-server");
                  resp.set(beast::http::field::content_type,
                           mime_type(request.target()));
                  auto file = beast::http::file_body::value_type();
                  file.open(path.string().c_str(), beast::file_mode::read, err);
                  if (!err) {
                    resp.body() = std::move(file);
                    try {
                      // prepare calls FileBody::write::init() which could fail
                      // and cause an exception to be thrown
                      resp.prepare_payload();
                      response_ = std::move(resp);

                      serializer_.emplace<beast::http::response_serializer<beast::http::file_body>>(
                          std::get<beast::http::response<
	                          beast::http::file_body>>(response_));
                      beast::http::async_write(
                          sockets.socket,
                          std::get<beast::http::response_serializer<beast::http::file_body>>(serializer_),
                          socket_.wrap([ this, self = std::move(self) ](
                              auto& sockets,
                              const boost::system::error_code ec,
                              std::size_t bytes_transferred) mutable {
                            std::get<
                                beast::http::response<beast::http::file_body>>(
                                response_)
                                .body()
                                .close();
                            if (!ec) {
                              ReadHttpRequest(std::move(self));
                            }
                          }));
                      return;
                    } catch (const boost::system::system_error&) {
                    }
                  }
                }
              }

              // Send 404 response
              auto resp = beast::http::response<beast::http::string_body>();
              resp.result(beast::http::status::not_found);
              resp.version(request.version());
              resp.set(beast::http::field::server, "collab-vm-server");
              resp.set(beast::http::field::content_type, "text/html");
              resp.body() = "The file '" + request.target().to_string() +
                            "' was not found";
              resp.prepare_payload();
              response_ = std::move(resp);

              serializer_.emplace<beast::http::response_serializer<beast::http::string_body>>(
                    std::get<beast::http::response<beast::http::string_body>>(
                        response_));
              beast::http::async_write(
                  sockets.socket,
                  std::get<beast::http::response_serializer<beast::http::string_body>>(serializer_),
                  socket_.wrap([ this, self = std::move(self) ](
                      auto& sockets, const boost::system::error_code ec,
                      std::size_t bytes_transferred) mutable {
                    if (!ec) {
                      ReadHttpRequest(std::move(self));
                    }
                  }));

              return;
            } else if (request.method() == beast::http::verb::post) {
              // File uploads
              // RFC 2616 § 8.2.2 requires clients to stop sending a message
              // body when an error response is received, but most browsers
              // don't comply with it
              if (request.target() == "/upload") {
                if (boost::iequals(request[beast::http::field::content_type],
                                   "application/octet-stream")) {
                  const auto content_length =
                      request.find(beast::http::field::content_length);
                  if (content_length != request.end()) {
                    unsigned long len = std::strtoul(
                        content_length->value().data(), nullptr, 10);
                  }
                }
              }

              // Disconnect socket to prevent data from being received
              auto err = boost::system::error_code();
              sockets.socket.close(err);
              return;
            }

            // Send 405 (Method Not Allowed)
            auto resp = beast::http::response<beast::http::string_body>();
            resp.result(beast::http::status::method_not_allowed);
            resp.version(request.version());
            resp.set(beast::http::field::server, "collab-vm-server");
            resp.set(beast::http::field::content_type, "text/html");
            resp.body() = "The method '" + request.method_string().to_string() +
                          "' is not allowed";
            resp.prepare_payload();
            response_ = std::move(resp);

            serializer_.emplace<beast::http::response_serializer<beast::http::string_body>>(
                    std::get<beast::http::response<beast::http::string_body>>(
                        response_));
            beast::http::async_write(
                sockets.socket,
                std::get<beast::http::response_serializer<beast::http::string_body>>(serializer_),
                socket_.wrap([ this, self = std::move(self) ](
                    auto& sockets, const boost::system::error_code ec,
                    std::size_t bytes_transferred) mutable {
                  if (!ec) {
                    ReadHttpRequest(std::move(self));
                  }
                }));
          }));
    });
  }

  /*template<class WriteHandler>
  beast::async_return_type<WriteHandler, void(boost::system::error_code)>
  async_write(WriteHandler&& handler)
  {
          auto self = shared_from_this();
          return beast::http::async_write(socket_, serializer_,
                  [this, self, handler](const boost::system::error_code& ec)
          {
                  handler(ec);
                  if (!ec)
                  {
                          //typedef
  beast::http::response_serializer<beast::http::string_body> serializer;
                          //(&serializer_)->~serializer();
                          //new(&serializer_)
  beast::http::response_serializer<beast::http::string_body>(response_);
                          DoRead();
                  }
          });
  }*/

  void read_body() {
    buffer_.consume(buffer_.size());
    beast::http::async_read(
        socket_, buffer_, *parser_,
        [ this, self = this->shared_from_this() ](
            const boost::system::error_code ec, std::size_t bytes_transferred){
            //                if (ec)
            //                    accept();
            //                else
            //                    process_request(parser_->get());
        });
  }

  template <class ConstBufferSequence, class WriteHandler>
  void WriteMessage(const ConstBufferSequence& buffers,
                    WriteHandler&& handler) {
    socket_.dispatch([
      self = this->shared_from_this(), &buffers,
      handler = std::forward<WriteHandler>(handler)
    ](auto& sockets) { sockets.websocket.async_write(buffers, handler); });
  }

  void Close() {
    socket_.dispatch([ this, self = this->shared_from_this() ](auto& sockets) {
      auto ec = boost::system::error_code();
      sockets.socket.shutdown(
          asio::ip::tcp::socket::shutdown_type::shutdown_both, ec);
      sockets.socket.close(ec);
      if (close_callback_) {
        close_callback_();
        close_callback_ = nullptr;
        OnDisconnect();
      }
    });
  }

  struct IpAddress {
    //    using IpBytes = std::array<std::byte, 16>;
    using IpBytes = std::array<std::uint8_t, 16>;

    IpAddress() = default;
    IpAddress(const boost::asio::ip::address& ip_address)
        : str_(ip_address.to_string()) {
      if (ip_address.is_v4()) {
        bytes_ = GetIpv4MappedBytes(ip_address.to_v4());
      } else {
        CopyIpAddressBytes(ip_address.to_v6(), bytes_.begin());
      }
    }

    const std::string& AsString() const { return str_; }
    const IpBytes& AsBytes() const { return bytes_; }

   private:
    // Creates an IPv4-mapped IPv6 address as described in section 2.5.5.2 of
    // RFC4291
    static IpBytes GetIpv4MappedBytes(
        const boost::asio::ip::address_v4& ip_address) {
      auto bytes = IpBytes();
      bytes[10] = 0xFF;
      bytes[11] = 0xFF;
      CopyIpAddressBytes(ip_address, bytes.begin() + 12);
      return bytes;
    }

    template <typename TIpAddress, typename TDestination>
    static void CopyIpAddressBytes(const TIpAddress& ip_address,
                                   TDestination dest) {
      const auto bytes = ip_address.to_bytes();
      std::copy_n(reinterpret_cast<
                      const std::iterator_traits<TDestination>::value_type*>(
                      bytes.data()),
                  bytes.size(), dest);
    }

    IpBytes bytes_;
    std::string str_;
  };

  const IpAddress& GetIpAddress() { return ip_address_; }

  // Return a reasonable mime type based on the extension of a file.
  boost::beast::string_view mime_type(boost::beast::string_view path) {
    using boost::beast::iequals;
    const auto ext = [&path] {
      const auto pos = path.rfind(".");
      if (pos == boost::beast::string_view::npos)
        return boost::beast::string_view{};
      return path.substr(pos);
    }();
    if (iequals(ext, ".htm"))
      return "text/html";
    if (iequals(ext, ".html"))
      return "text/html";
    if (iequals(ext, ".php"))
      return "text/html";
    if (iequals(ext, ".css"))
      return "text/css";
    if (iequals(ext, ".txt"))
      return "text/plain";
    if (iequals(ext, ".js"))
      return "application/javascript";
    if (iequals(ext, ".json"))
      return "application/json";
    if (iequals(ext, ".xml"))
      return "application/xml";
    if (iequals(ext, ".swf"))
      return "application/x-shockwave-flash";
    if (iequals(ext, ".flv"))
      return "video/x-flv";
    if (iequals(ext, ".png"))
      return "image/png";
    if (iequals(ext, ".jpe"))
      return "image/jpeg";
    if (iequals(ext, ".jpeg"))
      return "image/jpeg";
    if (iequals(ext, ".jpg"))
      return "image/jpeg";
    if (iequals(ext, ".gif"))
      return "image/gif";
    if (iequals(ext, ".bmp"))
      return "image/bmp";
    if (iequals(ext, ".ico"))
      return "image/vnd.microsoft.icon";
    if (iequals(ext, ".tiff"))
      return "image/tiff";
    if (iequals(ext, ".tif"))
      return "image/tiff";
    if (iequals(ext, ".svg"))
      return "image/svg+xml";
    if (iequals(ext, ".svgz"))
      return "image/svg+xml";
    return "application/text";
  }

  template <typename TCallback>
  void GetSocket(TCallback&& callback) {
    socket_.dispatch([
      this, self = this->shared_from_this(), callback = std::move(callback)
    ](auto& sockets) { callback(sockets.socket); });
  }

  void SetCloseCallback(std::function<void()>&& close_callback) {
    close_callback_ = close_callback;
  }

 protected:
  virtual void OnConnect() = 0;
  virtual void OnMessage(std::shared_ptr<MessageBuffer>&& buffer) = 0;
  virtual void OnDisconnect() = 0;

 private:
  struct SocketsWrapper {
    SocketsWrapper(boost::asio::io_context& io_context)
        : socket(io_context), websocket(socket) {}
    SocketsWrapper(const SocketsWrapper& io_context) = delete;
    asio::ip::tcp::socket socket;
    beast::websocket::stream<asio::ip::tcp::socket&> websocket;
    // asio::ssl::stream<asio::ip::tcp::socket&> stream_;
  };

  StrandGuard<typename TThreadPool::Strand, SocketsWrapper> socket_;

  beast::flat_static_buffer<8192> buffer_;

  boost::asio::steady_timer request_deadline_;

  std::variant<beast::http::response<beast::http::string_body>,
               beast::http::response<beast::http::file_body>>
      response_;

  std::variant<
      std::monostate,
      beast::http::response_serializer<beast::http::string_body>,
      beast::http::response_serializer<beast::http::file_body>>
      serializer_;

  using alloc_type = fields_alloc<char>;
  alloc_type alloc_;
  using request_body_t =
      beast::http::basic_dynamic_body<beast::flat_static_buffer<1024 * 1024>>;
  std::optional<beast::http::request_parser<request_body_t, alloc_type>>
      parser_;

  //  beast::websocket::stream<asio::ip::tcp::socket&> websocket_;
  //  typename StrandGuard<Strand,
  //  beast::websocket::stream<asio::ip::tcp::socket&>>::SharedStrandGuard
  //  websocket_;

  const boost::filesystem::path& doc_root_;
  IpAddress ip_address_;

  std::function<void()> close_callback_;
};

template <bool TSingleThreaded>
struct ThreadPool {
  explicit ThreadPool(unsigned long threads) : io_context_(threads) {
    assert(threads == 1);
  }

  struct NullStrand {
    explicit NullStrand(asio::io_context& io_context)
        : io_context_(io_context) {}

    static bool running_in_this_thread() { return true; }

    template <typename TCompletionHandler, typename TAllocator>
    void dispatch(TCompletionHandler&& handler, TAllocator) {
			boost::asio::dispatch(io_context_, std::forward<TCompletionHandler>(handler));
    }

    template <typename TCompletionHandler, typename TAllocator>
    void post(TCompletionHandler&& handler, TAllocator) {
      boost::asio::post(io_context_, std::forward<TCompletionHandler>(handler));
    }

    template <typename THandler>
    THandler&& wrap(THandler&& handler) {
      return std::forward(handler);
    }

   private:
    asio::io_context& io_context_;
  };

  using Strand = NullStrand;

 protected:
  void RunWorkers() { io_context_.run(); }
  asio::io_context io_context_;
};

template <>
struct ThreadPool<false> {
  explicit ThreadPool(unsigned long threads)
      : io_context_(threads), thread_count_(threads) {}
  using Strand = asio::io_context::strand;

 protected:
  void RunWorkers() {
    auto threads = thread_count_;
    // Decrement because the current thread will also become a worker
    --threads;
    threads_.reserve(threads);
    for (auto i = 0u; i < threads; i++) {
      threads_.emplace_back([&] { io_context_.run(); });
    }

    io_context_.run();

    for (auto&& thread : threads_) {
      thread.join();
    }
  }

  asio::io_context io_context_;
  unsigned long thread_count_;
  std::vector<std::thread> threads_;
};

template <typename TThreadPool>
class WebServer : public TThreadPool {
 public:
  WebServer(const std::string& doc_root, const std::uint8_t threads)
      : TThreadPool(threads),
        stopping_(false),
        sockets_(TThreadPool::io_context_),
        acceptor_(TThreadPool::io_context_),
        doc_root_(doc_root),
        interrupt_signal_(TThreadPool::io_context_, SIGINT, SIGTERM) {}

  void Start(const std::string& host,
             const std::uint16_t port,
             const bool auto_start) {
    auto ec = boost::system::error_code();
    CreateDocRoot(doc_root_, ec);
    if (ec) {
      return;
    }

    interrupt_signal_.async_wait([this](const auto error,
                                        const auto signal_number) { Stop(); });

    auto resolver = asio::ip::tcp::resolver(TThreadPool::io_context_);
    auto it = resolver.resolve(host, std::to_string(port), ec);
    if (ec) {
      std::cout << "Could not resolve hostname \"" << host << '"' << std::endl;
      return;
    }

    const auto ep = asio::ip::tcp::endpoint(*it.begin());
    std::cout << "Listening on " << ep << std::endl;

    acceptor_.open(ep.protocol());
    acceptor_.bind(ep);
    acceptor_.listen();

    //    sockets_strand_.dispatch([this] { DoAccept(); });
    DoAccept();

    TThreadPool::RunWorkers();
  }

  void Stop() {
    auto ec = boost::system::error_code();
    interrupt_signal_.cancel(ec);
    acceptor_.close(ec);

    sockets_.dispatch([this](auto& sockets) {
      if (stopping_) {
        return;
      }
      stopping_ = true;
      for (auto&& socket : sockets) {
        socket->Close();
      }
    });
  }

 protected:
  using TSocket = WebServerSocket<WebServer, TThreadPool>;

  virtual std::shared_ptr<TSocket> CreateSocket(
      asio::io_context& io_context,
      const boost::filesystem::path& doc_root) = 0;

 private:
  static void CreateDocRoot(boost::filesystem::path& path,
                            boost::system::error_code& ec) {
    auto status = boost::filesystem::status(path, ec);
    if (ec) {
      if (status.type() == boost::filesystem::file_type::file_not_found) {
        if (boost::filesystem::create_directories(path, ec)) {
          std::cout << "The path " << path << " has been created" << std::endl;
        } else {
          std::cout << "Failed to create directory " << path << std::endl;
          return;
        }
      } else {
        std::cout << ec.category().message(ec.value()) << std::endl;
        return;
      }
    } else if (status.type() != boost::filesystem::file_type::directory_file) {
      std::cout << "The doc root should be a directory, but it's a file"
                << std::endl;
      ec = boost::system::errc::make_error_code(
          boost::system::errc::no_such_file_or_directory);
      return;
    }

    path = boost::filesystem::canonical(path, ec);
    if (ec) {
      std::cout << ec.category().message(ec.value()) << std::endl;
    }
  }

  void DoAccept() {
    sockets_.dispatch([this](auto& sockets) {
      if (stopping_) {
        return;
      }
      const auto socket_ptr = sockets.emplace_front(
          CreateSocket(TThreadPool::io_context_, doc_root_));
      const auto socket_it = sockets.cbegin();
      socket_ptr->SetCloseCallback(
          [this, socket_it] { RemoveSocket(socket_it); });

      socket_ptr->GetSocket([this, socket_ptr](auto& socket) {
        acceptor_.async_accept(
            socket,
            [this, socket_ptr](const boost::system::error_code ec) {
              if (ec || !acceptor_.is_open()) {
                socket_ptr->Close();
                return;
              }
              socket_ptr->Start();
              DoAccept();
            });
      });
    });
  }

  void RemoveSocket(
      typename std::list<std::shared_ptr<TSocket>>::const_iterator it) {
    sockets_.dispatch([this, it](auto& sockets) {
      if (!stopping_) {
        sockets.erase(it);
      }
    });
  }

  bool stopping_;
  StrandGuard<
    typename TThreadPool::Strand,
    std::list<std::shared_ptr<TSocket>>> sockets_;

  boost::asio::ip::tcp::acceptor acceptor_;
  boost::filesystem::path doc_root_;
  boost::asio::signal_set interrupt_signal_;
};
}  // namespace CollabVm::Server
