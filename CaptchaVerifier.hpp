#pragma once
#include "CollabVm.capnp.h"
#include <capnp/message.h>
#include <boost/algorithm/string/replace.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
// TODO: Replace Boost Property Tree with Cap'N Proto's JSON parser
#include <boost/property_tree/json_parser.hpp>
#include <functional>
#include <iostream>
#include <optional>
#include <queue>
#include <string>
#include <utility>

namespace CollabVm::Server {
class CaptchaVerifier {
  struct VerifyRequest {
    VerifyRequest(std::function<void(bool)>&& callback,
                  const std::string_view response_token,
                  const std::string_view remote_ip)
        : callback_(std::move(callback)),
          response_token_(response_token),
          remote_ip_(remote_ip) {}
    std::function<void(bool)> callback_;
    std::string response_token_;
    std::string remote_ip_;
  };

  std::optional<VerifyRequest> request_;
  std::queue<VerifyRequest> queue_;
  boost::asio::io_context::strand strand_;
  boost::asio::ip::tcp::resolver resolver_;
  boost::asio::ssl::stream<boost::asio::ip::tcp::socket> stream_;
  boost::beast::flat_buffer buffer_;
  boost::beast::http::request<boost::beast::http::string_body> req_;
  boost::beast::http::response<boost::beast::http::string_body> response_;
  std::unique_ptr<capnp::MallocMessageBuilder> settings_;
  std::unique_ptr<capnp::MallocMessageBuilder> pending_settings_;
  boost::asio::steady_timer retry_timer_;
  int failed_count_ = 0;
  constexpr static int max_fail_count_ = 3;

 public:
  explicit CaptchaVerifier(boost::asio::io_context& io_context,
                           boost::asio::ssl::context& ssl_ctx)
      : strand_(io_context),
        resolver_(io_context),
        stream_(io_context, ssl_ctx),
        retry_timer_(io_context, std::chrono::seconds(10)) {
    req_.keep_alive(true);
  }

  void SetSettings(ServerSetting::Captcha::Reader settings) {
    auto message_builder = std::make_unique<capnp::MallocMessageBuilder>();
    message_builder->setRoot(settings);
    SetSettings([message_builder = std::move(message_builder)]() mutable {
      return std::move(message_builder);
    });
  }

  template<typename TGetSettings>
  void SetSettings(TGetSettings&& getSettings) {
    boost::asio::dispatch(strand_, [this, getSettings = std::forward<TGetSettings>(getSettings)]() mutable {
      if constexpr (std::is_same_v<std::invoke_result_t<TGetSettings>, std::unique_ptr<capnp::MallocMessageBuilder>>) {
        pending_settings_ = getSettings();
      } else {
        pending_settings_ = std::make_unique<capnp::MallocMessageBuilder>();
        pending_settings_->setRoot(ServerSetting::Captcha::Reader(getSettings()));
      }
      auto new_settings = pending_settings_->getRoot<ServerSetting::Captcha>();
      req_.set(boost::beast::http::field::host,
        std::string_view(new_settings.getUrlHost().cStr(),
          new_settings.getUrlHost().size()));
      if (new_settings.getPostParams().size()) {
        req_.method(boost::beast::http::verb::post);
        req_.set(boost::beast::http::field::content_type,
                 "application/x-www-form-urlencoded");
      } else {
        req_.method(boost::beast::http::verb::get);
        req_.erase(boost::beast::http::field::content_type);
      }

      // Close the socket in case the host changed
      boost::system::error_code ec;
      stream_.lowest_layer().close(ec);
    });
  }

  void Verify(const std::string_view token,
              std::function<void(bool)> callback,
              const std::string_view remote_ip = "") {
    boost::asio::dispatch(strand_, [&, callback = std::move(callback)]() mutable {
      auto settings = pending_settings_ ? pending_settings_.get() : settings_.get();
      if (!settings || !settings->getRoot<ServerSetting::Captcha>().getEnabled()) {
        callback(true);
        return;
      }
      if (token.empty()) {
        callback(false);
        return;
      }
      auto request = VerifyRequest(std::move(callback), token, remote_ip);
      if (request_.has_value()) {
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
private:
  void Connect() {
    boost::asio::dispatch(strand_, [this]() {
      if (pending_settings_) {
        settings_.reset(pending_settings_.release());
        failed_count_ = 0;
      }
      auto settings = settings_->getRoot<ServerSetting::Captcha>();
      const auto host = settings.getUrlHost();
      resolver_.async_resolve(
        { host.cStr(), host.size() }, std::to_string(settings.getUrlPort()),
        [this, useSSL = settings.getHttps()](const boost::system::error_code& ec,
          boost::asio::ip::tcp::resolver::results_type results) {
            boost::asio::async_connect(
              stream_.lowest_layer(), results,
              [this, useSSL](const boost::system::error_code& ec,
                const boost::asio::ip::tcp::endpoint& endpoint) {
                  if (ec) {
                    std::cout << "Failed to connect to captcha service" << std::endl;
                    Retry();
                    return;
                  }
                  if (!useSSL) {
                    SendRequest();
                    return;
                  }
                  stream_.async_handshake(
                    boost::asio::ssl::stream_base::client,
                    [this](const boost::system::error_code& ec) {
                      if (ec) {
                        std::cout << "SSL handshake with captcha service failed" << std::endl;
                        Retry();
                        return;
                      }

                      SendRequest();
                    });
              });
        });
      });
  }

  static void ReplaceVariables(std::string& body, const VerifyRequest& request) {
    boost::algorithm::replace_all(body, "$TOKEN",
      request.response_token_);
    boost::algorithm::replace_all(body, "$IP",
      request.remote_ip_);
  }

  void SendRequest() {
    boost::asio::dispatch(strand_, [this]() {
      failed_count_ = 0;
      auto settings = settings_->getRoot<ServerSetting::Captcha>();
      auto url_path = std::string(settings.getUrlPath().asString());
      ReplaceVariables(url_path, request_.value());
      req_.target(url_path);
      if (settings.getPostParams().size()) {
        req_.body() = settings.getPostParams().asString();
        ReplaceVariables(req_.body(), request_.value());
        req_.set(boost::beast::http::field::content_length, req_.body().length());
      }
      auto send_request = [this](auto& stream) mutable {
        boost::beast::http::async_write(
          stream, req_,
          [this, &stream](const boost::system::error_code& ec,
            std::size_t bytes_transferred) mutable {
              if (ec) {
                std::cout << "Failed to send HTTP request for captcha verification" << std::endl;
                Retry();
                return;
              }
              boost::beast::http::async_read(
                stream, buffer_, response_,
                boost::asio::bind_executor(strand_,
                  [this](const boost::system::error_code& ec,
                    std::size_t bytes_transferred) {
                      if (ec) {
                        std::cout << "Failed to read HTTP response for captcha verification" << std::endl;
                        Retry();
                        return;
                      }
                      auto success = false;
                      try {
                        boost::property_tree::ptree pt;
                        auto string_stream = std::istringstream(response_.body());
                        boost::property_tree::read_json(string_stream, pt);
                        auto settings = settings_->getRoot<ServerSetting::Captcha>();
                        success = pt.get<bool>(settings.getValidJSONVariableName().cStr());
                      } catch (boost::property_tree::ptree_error&) {
                        std::cout << "Failed to parse JSON response for captcha" << std::endl;
                      }
                      request_->callback_(success);

                      // Destruct and reconstruct the response in preparation
                      // for another request
                      ([](auto& response) {
                        using T = std::remove_reference_t<decltype(response)>;
                        response.~T();
                        new (&response) T;
                      })(response_);

                      if (queue_.empty()) {
                        request_.reset();
                        return;
                      }
                      request_.emplace(std::move(queue_.front()));
                      queue_.pop();
                      SendRequest();
                  }));
          });
        };
        if (settings.getHttps()) {
          send_request(stream_);
        } else {
          send_request(static_cast<boost::asio::ip::tcp::socket&>(stream_.lowest_layer()));
        }
    });
  }

  void Retry() {
    boost::asio::dispatch(strand_, [this]() {
      if (++failed_count_ >= max_fail_count_) {
        // Reject all captcha verification requests because
        // the tokens may have timed out
        request_->callback_(false);
        request_.reset();
        while (!queue_.empty()) {
          queue_.front().callback_(false);
          queue_.pop();
        }
        return;
      }
      retry_timer_.async_wait([this](auto& ec) {
        Connect();
      });
    });
  }
};
}  // namespace CollabVm::Server
