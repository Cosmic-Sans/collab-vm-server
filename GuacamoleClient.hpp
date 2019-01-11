#pragma once

extern "C" {
#include <guacamole/user.h>
#include <guacamole/client.h>

#include <protocols/rdp/rdp.h>
#include <protocols/rdp/client.h>
#include <protocols/vnc/vnc.h>
#include <protocols/vnc/client.h>
}
#ifdef WIN32
# undef CONST
# undef min
# undef max
#endif
#include <atomic>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <capnp/message.h>
#include <functional>
#include <gsl/span>
#include <optional>
#include <memory>
#include <string_view>

#include "Guacamole.capnp.h"

namespace CollabVm::Server
{
template<typename TStartCallback, typename TStopCallback, typename TLogCallback>
class GuacamoleClient final
{
public:
  GuacamoleClient(boost::asio::io_context& io_context,
                  TStartCallback&& start_callback,
                  TStopCallback&& stop_callback,
                  TLogCallback&& log_callback)
    : io_context_(io_context),
      start_callback_(start_callback),
      stop_callback_(stop_callback),
      log_callback_(log_callback),
      user_(nullptr, &guac_user_free),
      client_(nullptr, &guac_client_free)
  {
  }

  /**
   * Start a new RDP connection using the previously provided arguments.
   */
  void StartRDP()
  {
    StartRDP(std::nullopt);
  }

  /**
   * Start a new RDP connection using the provided arguments.
   */
  void StartRDP(
    const std::unordered_map<std::string_view, std::string_view>& args)
  {
    StartRDP(std::optional(std::reference_wrapper(args)));
  }

  /**
   * Start a new VNC connection using the previously provided arguments.
   */
  void StartVNC()
  {
    StartVNC(std::nullopt);
  }

  /**
   * Start a new VNC connection using the provided arguments.
   */
  void StartVNC(
    const std::unordered_map<std::string_view, std::string_view>& args)
  {
    StartVNC(std::optional(std::reference_wrapper(args)));
  }

  void Stop()
  {
    if (state_.exchange(State::kStopping) == State::kRunning)
    {
      // This function doesn't appear to be thread-safe because it
      // manipulates the state of the guac_client without any
      // synchronization, but Guacamole also uses it this way
      guac_client_stop(client_.get());
    }
  }

  void AddUser()
  {
    /*
    auto user =
      std::unique_ptr<guac_user, decltype(&guac_user_free)>(
        guac_user_alloc(), guac_user_free);
    */
    const auto user = guac_user_alloc();
    user->client = client_.get();
    user->socket = guac_socket_alloc();
    user->socket->write_handler = [](auto* socket, auto* data)
    {
      return ssize_t(0);
    };
    client_->join_handler(user, args_.size(), const_cast<char**>(args_.data()));
    client_->leave_handler(user);
  }
private:
  static ssize_t SocketWriteHandler(guac_socket* socket, void* data)
  {
    auto& message_builder = *static_cast<capnp::MallocMessageBuilder*>(data);
    auto instr = message_builder.getRoot<Guacamole::GuacServerInstruction>();
    if (   instr.which() == Guacamole::GuacServerInstruction::Which::DISCONNECT
        || instr.which() == Guacamole::GuacServerInstruction::Which::ERROR)
    {
      auto& guacamole_client =
        *static_cast<GuacamoleClient*>(socket->data);
      guacamole_client.state_ = State::kStopping;
    }
    return ssize_t(0);
  }

  void StartRDP(
    const std::optional<std::reference_wrapper<
      const std::unordered_map<std::string_view, std::string_view>>> args)
  {
    Start(guac_rdp_client_init, args);
  }

  void StartVNC(const std::optional<std::reference_wrapper<
    const std::unordered_map<std::string_view, std::string_view>>> args)
  {
    Start(guac_vnc_client_init, args);
  }

  void Start(guac_client_init_handler& init_handler,
    const std::optional<std::reference_wrapper<
    const std::unordered_map<std::string_view, std::string_view>>> args)
  {
    CreateClient();
    if (init_handler(client_.get()))
    {
      throw std::exception();
    }
    CreateUser();
    if (args)
    {
      args_ = CreateArgsArray(args.value().get(), client_->args);
    }
    else if (args_.empty())
    {
      args_ = CreateArgsArray({}, client_->args);
    }
    guac_client_add_user(client_.get(), user_.get(), args_.size(),
      const_cast<char**>(args_.data()));
  }

  void CreateClient()
  {
    state_ = State::kStarting;
    client_.reset(guac_client_alloc());
    const auto broadcast_socket = guac_socket_alloc();
    broadcast_socket->data = this;
    broadcast_socket->write_handler = [](auto* socket, auto* data)
    {
      auto& message_builder = *static_cast<capnp::MallocMessageBuilder*>(data);
      auto instr = message_builder.getRoot<Guacamole::GuacServerInstruction>();
      if (instr.which() == Guacamole::GuacServerInstruction::Which::NOP)
      {
        // Ignore NOPs because they may be sent from a keep-alive thread
        return SocketWriteHandler(socket, data);
      }
      // This callback is invoked from a new thread and
      // when it exits it is safe to deallocate the guac_client
      auto& guacamole_client =
        *static_cast<GuacamoleClient*>(socket->data);
      boost::this_thread::at_thread_exit([&guacamole_client]
      {
        // The stop callback is posted to an io_context thread
        // to prevent a deadlock with Windows pthreads
        guacamole_client.state_ = State::kStopped;
        boost::asio::post(guacamole_client.io_context_,
          [&guacamole_client]
          {
            guacamole_client.stop_callback_(guacamole_client);
          });
      });
      socket->write_handler = [](auto* socket, auto* data)
      {
        auto& message_builder = *static_cast<capnp::MallocMessageBuilder*>(data);
        auto instr = message_builder.getRoot<Guacamole::GuacServerInstruction>();
        if (instr.which() == Guacamole::GuacServerInstruction::Which::IMG
            && instr.getImg().getLayer() == 0)
        {
          auto& guacamole_client =
            *static_cast<GuacamoleClient*>(socket->data);
          auto state = State::kStarting;
          const auto was_starting =
            guacamole_client.state_.compare_exchange_strong(state, State::kRunning);
          if (was_starting)
          {
            boost::asio::post(guacamole_client.io_context_,
              [&guacamole_client]
              {
                guacamole_client.start_callback_(guacamole_client);
              });
          }
          else if (state == State::kStopping)
          {
            // Now that the client thread is known, the client can be stopped
            guac_client_stop(guacamole_client.client_.get());
          }
          socket->write_handler = SocketWriteHandler;
        }
        return SocketWriteHandler(socket, data);
      };
      return SocketWriteHandler(socket, data);
    };
    client_->socket = broadcast_socket;
    client_->data = this;
    client_->log_handler =
      [](auto* client,
         guac_client_log_level level,
         const char* format,
         va_list args)
      {
        auto& guacamole_client =
          *static_cast<GuacamoleClient*>(client->data);
        auto message = std::array<char, 2048>();
        if (::vsnprintf(message.data(), sizeof(message), format, args) > 0)
        {
          guacamole_client.log_callback_(guacamole_client, message.data());
        }
      };
  }
    
  void CreateUser()
  {
    user_.reset(guac_user_alloc());
    user_->client = client_.get();
    user_->owner = true;
    user_->info.optimal_resolution = 96;
    user_->info.optimal_width = 800;
    user_->info.optimal_height = 600;
    static const char* audio_mimetypes[1] = {
      static_cast<const char*>("audio/L16")
    };

    user_->info.audio_mimetypes = audio_mimetypes;

    auto* guac_socket = guac_socket_alloc();
    /*
    guac_socket->data = &websocket;
    guac_socket->write_handler = [](auto* guac_socket, auto* data)
    {
      auto& websocket = *static_cast<boost::beast::websocket::stream<boost::asio::ip::tcp::socket>*>(guac_socket->data);
      auto& message_builder = *static_cast<capnp::MallocMessageBuilder*>(data);
      const auto array = capnp::messageToFlatArray(message_builder);
      const boost::asio::const_buffer buffers(array.asBytes().begin(), array.asBytes().size());
      //auto printed = capnp::prettyPrint(message_builder.getRoot<Guacamole::GuacServerInstruction>()).flatten();
      boost::system::error_code ec;
      ssize_t written = websocket.write(buffers, ec);
      return ec ? 0 : written;
    };
  */
    user_->socket = guac_socket;
  }

  static std::vector<const char*> CreateArgsArray(
    const std::unordered_map<std::string_view, std::string_view>& args_map,
    const char** arg_names)
  {
    const auto args_end =
      std::find(arg_names,
        reinterpret_cast<const char**>(std::numeric_limits<std::size_t>::max()),
        nullptr);
    const auto args_count = std::distance(arg_names, args_end);
    if (args_map.empty())
    {
      return std::vector<const char*>(args_count, "");
    }
    auto args = std::vector<const char*>();
    args.reserve(args_count);
    for (const auto arg_name : gsl::span(arg_names, args_count))
    {
      auto it = args_map.find(arg_name);
      args.push_back(it == args_map.end() ? "" : it->second.data());
    }
    return args;
  }

  boost::asio::io_context& io_context_;
  TStartCallback start_callback_;
  TStopCallback stop_callback_;
  TLogCallback log_callback_;
  std::unique_ptr<guac_user, decltype(&guac_user_free)> user_;
  std::unique_ptr<guac_client, decltype(&guac_client_free)> client_;
  std::vector<const char*> args_;

  enum class State : std::uint8_t
  {
    kStopped,
    kStarting,
    kRunning,
    kStopping
  };
  std::atomic<State> state_;
};
}
