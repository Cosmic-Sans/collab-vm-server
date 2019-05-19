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
#include <fstream>
#include <string_view>
#include <string>
#include <type_traits>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <iostream>
#include <queue>

template<typename T>
using TestStrandGuard = StrandGuard<boost::asio::io_context::strand, T>;

template<typename TCallbacks>
struct TestWebSocket
{
  TestWebSocket(boost::asio::io_context& io_context,
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

  void Read()
  {
    data_.dispatch(
      [this](auto& data) mutable
    {
      data.receiving = true;
      auto buffer_ptr = std::make_shared<boost::beast::flat_static_buffer<1024>>();
      auto& buffer = *buffer_ptr;
      data.socket.async_read(buffer,
        data_.wrap([this, buffer_ptr=std::move(buffer_ptr)]
          (auto& data, const auto error_code, const auto bytes_transferred)
          {
            if (error_code)
            {
              data.receiving = false;
              data.joined = false;
              Close();
              return;
            }
            static_cast<TCallbacks&>(*this).OnMessage(*buffer_ptr);
            Read();
          }));
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
    data_.dispatch([this](auto& data)
    {
      auto error_code = boost::system::error_code();
      data.socket.next_layer().close(error_code);

      if (!data.sending && !data.receiving)
      {
        static_cast<TCallbacks&>(*this).OnClose();
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
      data_.wrap([this, socket_message = std::move(socket_message)](
        auto& data, const auto error_code, const auto bytes_transferred) mutable
        {
          if (error_code)
          {
            data.sending = false;
            data.joined = false;
            Close();
            return;
          }
          if (data.send_queue.empty())
          {
            data.sending = false;
            return;
          }
          SendMessage(data.socket, std::move(data.send_queue.front()));
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

struct TestGuacamoleClient
  final : CollabVm::Server::GuacamoleClient<TestGuacamoleClient>
{
struct TestGuacamoleWebSocket
  final : TestWebSocket<TestGuacamoleWebSocket>
{
  TestGuacamoleWebSocket(
    boost::asio::io_context& io_context,
    boost::asio::ip::tcp::socket&& socket,
    TestGuacamoleClient& guacamole_client)
    : TestWebSocket(io_context, std::move(socket)),
      guacamole_client_(guacamole_client)
  {
  }

  void SetIterator(const std::list<TestGuacamoleWebSocket>::iterator iterator)
  {
    list_iterator_ = iterator;
  }

  void OnMessage(boost::beast::flat_static_buffer<1024>& buffer)
  {
    const auto buffer_data = buffer.data();
    const auto array_ptr = kj::ArrayPtr<const capnp::word>(
      static_cast<const capnp::word*>(buffer_data.data()),
      buffer_data.size() / sizeof(capnp::word));
    auto reader = capnp::FlatArrayMessageReader(array_ptr);
    auto instr = reader.getRoot<Guacamole::GuacClientInstruction>();

    static auto created_screenshot = false;
    if (instr.isKey() && !created_screenshot)
    {
      auto png = std::ofstream("preview.png", std::ios::out | std::ios::binary);
      using file_char_type = std::remove_reference<decltype(png)>::type::char_type;
      guacamole_client_.CreateScreenshot([&png](const auto png_bytes)
      {
        std::cout << "png_bytes.size(): " << png_bytes.size() << std::endl;
        png.write(
          reinterpret_cast<const file_char_type*>(png_bytes.data()),
          png_bytes.size());
      });
      created_screenshot = true;
      return;
    }

    guacamole_client_.ReadInstruction(instr);
  }

  void OnClose()
  {
    guacamole_client_.RemoveSocket(list_iterator_);
  }

  std::list<TestGuacamoleWebSocket>::iterator list_iterator_;
  TestGuacamoleClient& guacamole_client_;
};

  TestGuacamoleClient(boost::asio::io_context& io_context,
    TestStrandGuard<std::list<TestGuacamoleWebSocket>>& websockets)
    : GuacamoleClient<TestGuacamoleClient>(io_context),
      state_(io_context, State::kDisconnected),
      websockets_(websockets)
  {
  }

  void OnStart()
  {
    state_.dispatch([this](auto& state)
    {
      state = State::kConnected;
      websockets_.dispatch(
        [this](auto& websockets)
        {
          for (auto it = websockets.begin(); it != websockets.end(); it++)
          {
            AddUser(*it);
          }
        });
    });
  }

  void Add(TestGuacamoleWebSocket& websocket)
  {
    state_.dispatch([this, &websocket](const auto state)
    {
      if (state != State::kConnected)
      {
        return;
      }
      AddUser(websocket);
    });
  }

  void RemoveSocket(std::list<TestGuacamoleWebSocket>::iterator list_iterator)
  {
    websockets_.post([list_iterator](auto& sockets)
    {
      sockets.erase(list_iterator);
      // TestGuacamoleWebSocket is destructed
    });
  }

  void Stop()
  {
    state_.dispatch([this](auto& state)
    {
      state = State::kStopped;
    });
  }

  void OnStop()
  {
    state_.dispatch([this](auto& state)
    {
      if (state == State::kStopped)
      {
        websockets_.dispatch(
          [](auto& websockets)
          {
            for (auto& websocket : websockets)
            {
              websocket.Close();
            }
          });
      }
      else
      {
        state = State::kDisconnected;
        StartRDP();
        // StartVNC();
      }
    });
  }

  void OnLog(const std::string_view message)
  {
    std::cout << message << std::endl;
  }

  void OnInstruction(capnp::MallocMessageBuilder& message_builder)
  {
    auto message = std::make_shared<kj::Array<capnp::word>>(
      capnp::messageToFlatArray(message_builder));
    websockets_.dispatch(
      [message = std::move(message)](auto& websockets)
      {
        for (auto& websocket : websockets)
        {
          websocket.Send(message);
        }
      });
  }

  void OnFlush()
  {
  }

private:
  void AddUser(TestGuacamoleWebSocket& websocket)
  {
    websocket.SendBatch([this, &websocket](auto send)
    {
      websocket.SetJoined();
      GuacamoleClient::AddUser([send = std::move(send)](
        capnp::MallocMessageBuilder&& message_builder)
        {
          send(std::make_shared<kj::Array<capnp::word>>(
            capnp::messageToFlatArray(message_builder)));
        });
    });
  }

  enum class State : std::uint8_t
  {
    kStopped,
    kConnected,
    kDisconnected
  };

  TestStrandGuard<State> state_;
  TestStrandGuard<std::list<TestGuacamoleWebSocket>>& websockets_;
};

void AcceptSocket(
  boost::asio::io_context& io_context,
  TestGuacamoleClient& guacamole_client,
  boost::asio::ip::tcp::acceptor& acceptor,
  TestStrandGuard<std::list<TestGuacamoleClient::TestGuacamoleWebSocket>>& websockets)
{
  acceptor.async_accept(websockets.wrap(
    [&io_context, &guacamole_client, &acceptor, &websockets_guard = websockets]
    (auto& websockets, const auto error_code, auto&& socket)
    {
      if (error_code)
      {
        return;
      }
      auto& websocket =
        websockets.emplace_back(io_context, std::move(socket), guacamole_client);
      websocket.SetIterator(std::prev(websockets.end()));
      websocket.Accept(
        [&websocket, &guacamole_client](const auto error_code)
        {
          if (!error_code)
          {
            guacamole_client.Add(websocket);
            websocket.Read();
          }
        });
      AcceptSocket(io_context, guacamole_client, acceptor, websockets_guard);
    }));
}

int main()
{
  auto io_context = boost::asio::io_context(1);
  using CollabVm::Server::GuacamoleClient;
  auto websockets = TestStrandGuard<std::list<TestGuacamoleClient::TestGuacamoleWebSocket>>(io_context);
  auto guacamole_client = TestGuacamoleClient(io_context, websockets);
  const auto args = std::unordered_map<std::string_view, std::string_view>{
    {"hostname", "localhost"},
    // {"port", "5900"}
    {"port", "3389"}
  };
  guacamole_client.StartRDP(args);
  // guacamole_client.StartVNC(args);

  auto websockets_strand = boost::asio::io_context::strand(io_context);

  auto acceptor = boost::asio::ip::tcp::acceptor(io_context, {
    boost::asio::ip::make_address("127.0.0.1"), 8081
  });
  AcceptSocket(io_context, guacamole_client, acceptor, websockets);

  auto interrupt_signal = boost::asio::signal_set(io_context, SIGINT, SIGTERM);
  interrupt_signal.async_wait(
    [&](auto error_code, const auto signal)
    {
      guacamole_client.Stop();
      acceptor.close(error_code);
    });

  io_context.run();

  return 0;
}

