#pragma once

extern "C" {
#include <guacamole/user.h>
#include <guacamole/client.h>

#include <protocols/rdp/rdp.h>
#include <protocols/rdp/client.h>
#include <protocols/rdp/user.h>
#include <protocols/vnc/vnc.h>
#include <protocols/vnc/client.h>
#include <protocols/vnc/user.h>
}
#ifdef WIN32
# undef CONST
# undef ERROR
# undef min
# undef max
#endif
#include <libguac/user-handlers.h>

#include <atomic>
#include <boost/asio.hpp>
#include <cairo.h>
#include <capnp/message.h>
#include <functional>
#include <gsl/span>
#include <optional>
#include <pthread.h>
#include <memory>
#include <string_view>
#include <unordered_map>

#include "Guacamole.capnp.h"
#include "GuacamoleScreenshot.hpp"

namespace CollabVm::Server
{
template<typename TCallbacks>
class GuacamoleClient
{
public:
  GuacamoleClient(boost::asio::io_context::strand& execution_context)
    : execution_context_(execution_context),
      user_(nullptr, &guac_user_free),
      client_(nullptr, &guac_client_free)
  {
  }

  void StartRDP()
  {
    Start(guac_rdp_client_init);
  }

  void StartVNC()
  {
    Start(guac_vnc_client_init);
  }

  void SetArguments(
    const std::unordered_map<std::string_view, std::string_view>& args)
  {
    args_map_ = args;
    if (client_)
    {
      args_ = CreateArgsArray(args_map_, client_->args);
    }
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

  template<typename TJoinInstructionsCallback>
  void AddUser(TJoinInstructionsCallback&& callback)
  {
    if (state_ != State::kRunning)
    {
      return;
    }
    const auto user = guac_user_alloc();
    user->client = client_.get();
    user->socket = guac_socket_alloc();
    struct SocketData
    {
      SocketData(GuacamoleClient& guacamole_client, TJoinInstructionsCallback&& callback)
        : guacamole_client(guacamole_client),
          callback(callback)
      {}
      GuacamoleClient& guacamole_client;
      TJoinInstructionsCallback callback;
    } socket_data(*this, std::move(callback));
    user->socket->data = &socket_data;
    user->socket->write_handler = [](auto* socket, auto* data)
    {
      auto& socket_data = *static_cast<SocketData*>(socket->data);
      auto& guacamole_client = socket_data.guacamole_client;
      auto& message_builder = *static_cast<capnp::MallocMessageBuilder*>(data);
      socket_data.callback(std::move(message_builder));
      return ssize_t(0);
    };
    user->info.optimal_resolution = 96;
    user->info.optimal_width = 800;
    user->info.optimal_height = 600;
    const char* audio_mimetypes[] = {
      static_cast<const char*>("audio/L8"),
      static_cast<const char*>("audio/L16"),
      NULL
    };
    user->info.audio_mimetypes = audio_mimetypes;
    client_->join_handler(user, args_.size(), const_cast<char**>(args_.data()));
    client_->leave_handler(user);
    guac_socket_free(user->socket);
    guac_user_free(user);
  }

  void ReadInstruction(Guacamole::GuacClientInstruction::Reader instr)
  {
    if (user_)
    {
      guac_call_instruction_handler(user_.get(), instr);
    }
  }

  /*
   * Creates a scaled screenshot and uses the callback to return the PNG bytes.
   * @returns true if successful
   */
  template<typename TWriteCallback>
  bool CreateScreenshot(TWriteCallback&& callback)
  {
    if (state_ != State::kRunning)
    {
      return false;
    }

    auto screenshot = GuacamoleScreenshot();
    AddUser([&screenshot](auto&& message_builder)
    {
      screenshot.WriteInstruction(
        message_builder.template getRoot<Guacamole::GuacServerInstruction>());
    });

    return screenshot.CreateScreenshot(400, 400, std::forward<TWriteCallback>(callback));
  }

private:
  static ssize_t SocketWriteHandler(guac_socket* socket, void* data)
  {
    auto& guacamole_client =
      *static_cast<GuacamoleClient*>(socket->data);
    auto& message_builder = *static_cast<capnp::MallocMessageBuilder*>(data);
    auto instr = message_builder.getRoot<Guacamole::GuacServerInstruction>();
    if (   instr.which() == Guacamole::GuacServerInstruction::Which::DISCONNECT
        || instr.which() == Guacamole::GuacServerInstruction::Which::ERROR)
    {
      guacamole_client.state_ = State::kStopping;
    }
    static_cast<TCallbacks&>(guacamole_client).OnInstruction(
      message_builder);
    return ssize_t(0);
  }

  void Start(guac_client_init_handler& init_handler)
  {
    CreateClient();
    if (init_handler(client_.get()))
    {
      throw std::exception();
    }
    CreateUser();
    args_ = CreateArgsArray(args_map_, client_->args);
    guac_client_add_user(client_.get(), user_.get(), args_.size(),
      const_cast<char**>(args_.data()));
  }

  void CreateClient()
  {
    state_ = State::kStarting;
    client_.reset(guac_client_alloc());
    const auto broadcast_socket = guac_socket_alloc();
    broadcast_socket->data = this;
    broadcast_socket->flush_handler = [](auto* socket)
    {
      auto& guacamole_client =
        *static_cast<GuacamoleClient*>(socket->data);
      static_cast<TCallbacks&>(guacamole_client).OnFlush();
      return ssize_t(0);
    };
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
      auto* destructor_key = &guacamole_client.guacamole_thread_destructor_key;
      pthread_key_create(destructor_key, &GuacamoleThreadDestructor);
      pthread_setspecific(*destructor_key, &guacamole_client);

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
            boost::asio::post(guacamole_client.execution_context_,
              [&guacamole_client]
              {
                static_cast<TCallbacks&>(guacamole_client).OnStart();
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
        if (const auto message_len = ::vsnprintf(
                message.data(), sizeof(message), format, args);
            message_len > 0)
        {
          static_cast<TCallbacks&>(guacamole_client).OnLog(
            std::string_view(message.data(), message_len));
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
    user_->socket = guac_socket_alloc();
  }

  static std::vector<const char*> CreateArgsArray(
    const std::unordered_map<std::string_view, std::string_view>& args_map,
    const char** arg_names)
  {
    auto args_count = 0u;
    while (arg_names[args_count])
    {
      args_count++;
    }
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

  static void GuacamoleThreadDestructor(void* data)
  {
    auto& guacamole_client =
      *static_cast<GuacamoleClient*>(data);

    pthread_key_delete(guacamole_client.guacamole_thread_destructor_key);
    guacamole_client.state_ = State::kStopped;
    boost::asio::post(guacamole_client.execution_context_,
      [&guacamole_client]
      {
        guacamole_client.client_.reset();
        guacamole_client.user_.reset();
        static_cast<TCallbacks&>(guacamole_client).OnStop();
      });
  }

  boost::asio::io_context::strand& execution_context_;
  std::unique_ptr<guac_user, decltype(&guac_user_free)> user_;
  std::unique_ptr<guac_client, decltype(&guac_client_free)> client_;
  std::vector<const char*> args_;
  std::unordered_map<std::string_view, std::string_view> args_map_;
  ::pthread_key_t guacamole_thread_destructor_key;

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
