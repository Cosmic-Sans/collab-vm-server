#include "config.h"
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#ifdef WIN32
#include <kj/windows-sanity.h>
#undef VOID
#undef CONST
#undef SendMessage
#endif
#include "Guacamole.capnp.h"
#include "StrandGuard.hpp"
#include <capnp/blob.h>
//
#include <capnp/pretty-print.h>
//
#include <capnp/dynamic.h>
#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/io.h>
#include "CapnpMessageFrameBuilder.hpp"


#include "GuacamoleClient.hpp"
#ifdef __cplusplus
extern "C" {
#endif
#include <protocols/rdp/rdp.h>
#include <protocols/rdp/client.h>
#include <protocols/vnc/vnc.h>
#include "protocols/vnc/client.h"
#ifdef __cplusplus
}
#endif

#include <libguac/guacamole/client.h>
#include <libguac/guacamole/user.h>
#include <libguac/user-handlers.h>
#include "freerdp/client/channels.h"
#include <freerdp/addin.h>
#include "protocols/rdp/client.h"
#include <atomic>
#include <string_view>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <iostream>
#include <queue>

template<typename T>
using TestStrandGuard = StrandGuard<boost::asio::io_context::strand, T>;

struct websocket_t
{
  websocket_t(boost::asio::io_context& io_context,
    boost::asio::ip::tcp::socket&& socket)
    : data_(io_context, std::move(socket))
  {
  }

  template<typename TCallback>
  void Accept(TCallback&& callback)
  {
    data_.dispatch(
      [this, callback = std::move(callback)](auto& data) mutable
    {
      data.socket.binary(true);
      data.socket.async_accept(std::move(callback));
    });
  }

  template<typename TCollectorCallback>
  void SendBatch(TCollectorCallback&& callback)
  {
    data_.dispatch(
      [this, callback = std::move(callback)](auto& data) mutable
    {
      callback([this, &data](std::shared_ptr<kj::Array<capnp::word>> socket_message)
      {
        if (data.sending)
        {
          data.send_queue.push(socket_message);
        }
        else
        {
          data.sending = true;
          SendMessage(data.socket, std::move(socket_message));
        }
      });
    });
  }

  void SetJoined() {
    data_.dispatch(
      [this](auto& data)
    {
      data.joined = true;
    });
  }

  void Send(std::shared_ptr<kj::Array<capnp::word>> socket_message)
  {
    data_.dispatch(
      [this, socket_message = std::move(socket_message)](auto& data) mutable
    {
      if (!data.joined)
      {
        return;
      }

      if (data.sending)
      {
        data.send_queue.push(socket_message);
      }
      else
      {
        data.sending = true;
        SendMessage(data.socket, std::move(socket_message));
      }
    });
  }

  void Close()
  {
    data_.dispatch(
      [this](auto& data)
    {
      auto ec = boost::system::error_code();
      data.socket.next_layer().close(ec);

      if (!data.sending && !data.receiving)
      {
        boost::asio::post(io_context_, []()
        {
          
        });
      }
    });
  }
private:
  void SendMessage(
    boost::beast::websocket::stream<boost::asio::ip::tcp::socket>& socket,
    std::shared_ptr<kj::Array<capnp::word>>&& socket_message)
  {
    const auto buffers = boost::asio::const_buffer(socket_message->asBytes().begin(),
      socket_message->asBytes().size());
    socket.async_write(buffers, 
       data_.wrap([ this, socket_message=std::move(socket_message) ](
        auto& data, const auto ec,
        auto bytes_transferred) mutable
        {
          if (ec)
          {
            // TSocket::Close();
            return;
          }
          if (data.send_queue.empty())
          {
            data.sending = false;
            return;
          }
          SendMessage(data.socket, std::move(socket_message));
          data.send_queue.pop();
        }));
  }

  struct GuardedData
  {
    GuardedData(boost::asio::ip::tcp::socket&& socket)
      : socket(std::move(socket)),
        sending(false),
        receiving(false),
        joined(false)
    {
    }
    boost::beast::websocket::stream<boost::asio::ip::tcp::socket> socket;
    std::queue<std::shared_ptr<kj::Array<capnp::word>>> send_queue;
    bool sending;
    bool receiving;
    bool joined;
  };

  boost::asio::io_context io_context_;
  TestStrandGuard<GuardedData> data_;
};

// Required for std::list::remove()
bool operator==(const websocket_t& lhs, const websocket_t& rhs)
{
  return &lhs == &rhs;
}

struct TestGuacamoleClient
  final : CollabVm::Server::GuacamoleClient<TestGuacamoleClient>
{
  TestGuacamoleClient(boost::asio::io_context& io_context,
    StrandGuard<
        boost::asio::io_context::strand, std::list<websocket_t>>&
      websockets,
    std::atomic_bool& stopping,
    boost::asio::executor_work_guard<
      boost::asio::io_context::executor_type>& work)
    : GuacamoleClient<TestGuacamoleClient>(io_context),
      websockets_(websockets),
      stopping_(stopping),
      work_(work)
  {
  }

  void OnStart()
  {
    // AddUser();
  }

  void OnStop()
  {
    if (stopping_)
    {
      work_.reset();
    }
    else
    {
      // StartRDP();
      StartVNC();
    }
  }

  void OnLog(const std::string_view message)
  {
    std::cout << message << std::endl;
  }

  void OnInstruction(capnp::MallocMessageBuilder&& message_builder)
  {
    auto instr = message_builder.getRoot<Guacamole::GuacServerInstruction>();
    if (instr.isBlob())
    {
      const auto stream = instr.getBlob().getStream();
      std::cout << "blob stream: " << stream << std::endl;
    }
    auto array = std::make_shared<kj::Array<capnp::word>>(
      capnp::messageToFlatArray(message_builder));
    websockets_.dispatch(
      [array = std::move(array)](auto& websockets)
      {
        for (auto it = websockets.begin(); it != websockets.end();)
        {
          if (true)
          {
            it->Send(array);
            it++;
          }
          else
          {
            it = websockets.erase(it);
          }
        }
      });
  }

private:
  std::atomic_bool& stopping_;
  boost::asio::executor_work_guard<
    boost::asio::io_context::executor_type>& work_;
  TestStrandGuard<std::list<websocket_t>>& websockets_;
};

void AcceptSocket(
  boost::asio::io_context& io_context,
  TestGuacamoleClient& guacamole_client,
  boost::asio::ip::tcp::acceptor& acceptor,
  TestStrandGuard<std::list<websocket_t>>& websockets)
{
  acceptor.async_accept(websockets.wrap(
    [&io_context, &guacamole_client, &acceptor, &websockets_guard = websockets](auto& websockets, auto ec, auto&& socket)
  {
    auto& websocket = websockets.emplace_back(io_context, std::move(socket));
    websocket.Accept(
      [&websocket, &guacamole_client](auto ec)
      {
        if (ec)
        {
          return;
        }
        websocket.SendBatch([&guacamole_client, &websocket](auto send)
        {
          websocket.SetJoined();
          guacamole_client.AddUser(
            [send = std::move(send)](
            capnp::MallocMessageBuilder&& message_builder)
            {
              send(std::make_shared<kj::Array<capnp::word>>(
                capnp::messageToFlatArray(message_builder)));
            });
        });
      });
    AcceptSocket(io_context, guacamole_client, acceptor, websockets_guard);
  }));
}

int main()
{
  auto io_context = boost::asio::io_context(1);
  auto stopping = std::atomic_bool();
  auto work = boost::asio::make_work_guard(io_context);
  using CollabVm::Server::GuacamoleClient;
  auto websockets = TestStrandGuard<std::list<websocket_t>>(io_context);
  auto guacamole_client = TestGuacamoleClient(io_context, websockets, stopping, work);
  const auto args = std::unordered_map<std::string_view, std::string_view>{
    {"hostname", "localhost"},
    {"port", "5900"}
  };
  // guacamole_client.StartRDP(args);
  guacamole_client.StartVNC(args);

  auto interrupt_signal = boost::asio::signal_set(io_context, SIGINT, SIGTERM);
  interrupt_signal.async_wait([&guacamole_client, &interrupt_signal, &stopping]
    (const auto error_code, auto const signal)
    {
      stopping = true;
      guacamole_client.Stop();
    });

  auto websockets_strand = boost::asio::io_context::strand(io_context);

  auto acceptor = boost::asio::ip::tcp::acceptor(io_context, {
    boost::asio::ip::make_address("127.0.0.1"), 8081
  });
  AcceptSocket(io_context, guacamole_client, acceptor, websockets);

  io_context.run();

  return 0;
}

