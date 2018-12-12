#pragma once
#include <boost/asio.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <functional>
#include <optional>
#include <queue>
#include <string>
#include <utility>

namespace CollabVm::Server {
class RecaptchaVerifier {
  struct VerifyRequest {
    VerifyRequest(const std::function<void(bool)>&& callback,
                  const std::string& response_token,
                  const std::string& remote_ip)
        : callback_(std::move(callback)),
          response_token_(response_token),
          remote_ip_(remote_ip) {}
    std::function<void(bool)> callback_;
    std::string response_token_;
    const std::string& remote_ip_;
  };

  template <typename T>
  static void CallDestructor(const T& object) {
    object.~T();
  }

  std::optional<VerifyRequest> request_;
  std::queue<VerifyRequest> queue_;
  boost::asio::io_context::strand queue_strand_;
  boost::asio::ip::tcp::resolver resolver_;
  boost::asio::ssl::stream<boost::asio::ip::tcp::socket> stream_;
  boost::beast::flat_buffer buffer_;
  boost::beast::http::request<boost::beast::http::string_body> req_;
  boost::beast::http::response<boost::beast::http::string_body> res_;
  std::string recaptcha_key_;

 public:
  explicit RecaptchaVerifier(boost::asio::io_context& io_context,
                             boost::asio::ssl::context& ssl_ctx,
                             const std::string& recaptcha_key)
      : queue_strand_(io_context),
        resolver_(io_context),
        stream_(io_context, ssl_ctx),
        recaptcha_key_(recaptcha_key) {
    req_.method(boost::beast::http::verb::post);
    req_.keep_alive(true);
    req_.set(boost::beast::http::field::host, "www.google.com");
    req_.target("/recaptcha/api/siteverify");
    req_.set(boost::beast::http::field::content_type,
             "application/x-www-form-urlencoded");
  }

  void SetRecaptchaKey(const std::string& recaptcha_key) {
    recaptcha_key_ = recaptcha_key;
  }

  void Connect() {
    resolver_.async_resolve(
        "google.com", "443",
        [this](const boost::system::error_code& ec,
               boost::asio::ip::tcp::resolver::results_type results) {
          boost::asio::async_connect(
              stream_.lowest_layer(), results,
              [this](const boost::system::error_code& ec,
                     const boost::asio::ip::tcp::endpoint& endpoint) {
                if (ec) {
                  Connect();
                  return;
                }
                stream_.async_handshake(
                    boost::asio::ssl::stream_base::client,
                    [this](const boost::system::error_code& ec) {
                      if (ec) {
                        Connect();
                        return;
                      }
                      SendRequest();
                    });

              });
        });
  }

  void SendRequest() {
    req_.body() =
        "secret=" + recaptcha_key_ + "&response=" + request_->response_token_;
    if (!request_->remote_ip_.empty()) {
      req_.body() += "&remoteip=" + request_->remote_ip_;
    }
    req_.set(boost::beast::http::field::content_length, req_.body().length());
    boost::beast::http::async_write(
        stream_, req_,
        [this](const boost::system::error_code& ec,
               std::size_t bytes_transferred) {
          if (ec) {
            Connect();
            return;
          }
          boost::beast::http::async_read(
              stream_, buffer_, res_,
              [this](const boost::system::error_code& ec,
                     std::size_t bytes_transferred) {
                if (ec) {
                  Connect();
                  return;
                }

                {
                  boost::property_tree::ptree pt;
                  boost::property_tree::read_json(res_.body(), pt);
                  request_->callback_(pt.get<bool>("success"));
                }
                // Destruct and reconstruct the response in preparation
                // for another request
                CallDestructor(res_);
                new (&res_) boost::beast::http::response<
                    boost::beast::http::string_body>;

								boost::asio::dispatch(queue_strand_, [this]() {
                  if (queue_.empty()) {
                    request_.reset();
                    return;
                  }
                  request_.emplace(std::move(queue_.front()));
                  queue_.pop();
                  SendRequest();
                });
              });
        });
  }

  void Verify(const std::string& g_response,
              const std::function<void(bool)>&& callback,
              const std::string& remote_ip = "") {
    if (recaptcha_key_.empty()) {
      callback(true);
      return;
    }
    if (g_response.empty()) {
      callback(false);
      return;
    }
    const VerifyRequest request(std::move(callback), g_response, remote_ip);
		boost::asio::dispatch(queue_strand_, [this, request]() {
      if (request_) {
        queue_.emplace(std::move(request));
        return;
      }
      request_.emplace(std::move(request));
      if (stream_.lowest_layer().is_open()) {
        SendRequest();
      } else {
        Connect();
      }
    });
  }
};
}  // namespace CollabVmServer
