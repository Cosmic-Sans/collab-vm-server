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

struct UsersList
{
	void Add(guac_user* user)
	{
		std::lock_guard<std::mutex> lock(lock_);
    users_.push_back(user);
	}

	void Remove(guac_user* user)
	{
		std::lock_guard<std::mutex> lock(lock_);
    users_.erase(std::find(users_.begin(), users_.end(), user));
	}

	template<typename TCallback>
	void ForEach(TCallback&& callback)
	{
		std::lock_guard<std::mutex> lock(lock_);
		for (auto& user : users_)
		{
			callback(user);
		}
	}

private:
	std::mutex lock_;
	std::list<guac_user*> users_;
} users_list;

void StartWebSocketServer(guac_client& client, std::vector<char*>& args)
{
	boost::asio::io_context ioc{1};
	boost::asio::ip::tcp::acceptor acceptor{ioc, {boost::asio::ip::make_address("127.0.0.1"), 8081}};
	while (true)
	{
		boost::asio::ip::tcp::socket socket{ioc};
		boost::system::error_code ec1;
		acceptor.accept(socket, ec1);
		std::thread(
			[socket=std::move(socket), &client, &args]() mutable
			{
				boost::beast::websocket::stream<boost::asio::ip::tcp::socket> websocket(std::move(socket));
				websocket.accept();
				websocket.binary(true);

				auto& user = *guac_user_alloc();
				auto* guac_socket = guac_socket_alloc();
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
        // user.owner = true;
				user.socket = guac_socket;
				user.client = &client;
				//	user.owner  = params->owner;
				user.info.optimal_resolution = 96;
				user.info.optimal_width = 800;
				user.info.optimal_height = 600;
				const char* audio_mimetypes[] = {
					static_cast<const char*>("audio/L16")
				};
				user.info.audio_mimetypes = audio_mimetypes;
/*
					(char**){
					static_cast<const char*>("asdf"),
					static_cast<const char*>("asdf"),
				};
*/
//				if (guac_client_add_user(&client, &user, args.size(), args.data()))
				if (client.join_handler && client.join_handler(&user, args.size(), args.data()))
				{
					return;
				}
				users_list.Add(&user);

				while (client.state == GUAC_CLIENT_RUNNING && user.active)
				{
          boost::beast::multi_buffer buffer;
					boost::system::error_code ec;
					websocket.read(buffer, ec);
					if (ec) { break; }
          assert(buffer.data().end() == ++buffer.data().begin());
				  const kj::ArrayPtr<capnp::word> array_ptr(
				    const_cast<capnp::word*>(static_cast<const capnp::word*>((*buffer.data().begin()).data())
				    ), buffer.size() / sizeof(capnp::word));
					// Instead of using FlatArrayMessageReader the message is copied into a
				  // MessageBuilder because Guacamole's user handlers require non-const char pointers
          capnp::MallocMessageBuilder message_builder;
          capnp::initMessageBuilderFromFlatArrayCopy(array_ptr, message_builder);
				  const auto message = message_builder.getRoot<Guacamole::GuacClientInstruction>();
					guac_call_instruction_handler(&user, message);
          buffer.consume(buffer.size());
				}

				users_list.Remove(&user);
			}).detach();
	}
}

void StartVncClient(guac_client* client) {
  guac_vnc_client_init(client);

  char* argv[] = { static_cast<char*>("") };
  guac_vnc_client* vnc_client = static_cast<guac_vnc_client*>(client->data);
  const auto my_args = std::map<std::string, std::string>{
    {"hostname", "10.6.6.212"},
    {"port",     "5900"}
  };
  std::vector<char*> args;
  const char* arg_name;
  for (auto i = 0u; arg_name = client->args[i]; i++)
  {
    auto it = my_args.find(arg_name);
    args.push_back(it == my_args.end() ? "" : it->second.data());
  }

  const auto user = guac_user_alloc();
  //	user->socket = socket;
  user->client = client;
  //	user->owner  = params->owner;
  user->info.optimal_resolution = 96;
  user->info.optimal_width = 800;
  user->info.optimal_height = 600;

  vnc_client->settings = guac_vnc_parse_args(user, args.size(), const_cast<const char**>(args.data()));
  guac_user_free(user);

  std::thread(guac_vnc_client_thread, client).detach();
  StartWebSocketServer(*client, args);
}

void StartRdpClient(guac_client* client) {
  guac_rdp_client_init(client);

  guac_rdp_client* rdp_client = static_cast<guac_rdp_client*>(client->data);
  const auto my_args = std::map<std::string, std::string>{
    // VirtualBox
    {"hostname", "localhost"},
    {"port", "3389"},
  };
  std::vector<char*> args;
  const char* arg_name;
  for (auto i = 0u; arg_name = client->args[i]; i++)
  {
    auto it = my_args.find(arg_name);
    args.push_back(it == my_args.end() ? "" : it->second.data());
  }

  const auto user =
    std::unique_ptr<guac_user, decltype(&guac_user_free)>(
                                      guac_user_alloc(), guac_user_free);
  //	user->socket = socket;
  user->client = client;
  //	user->owner  = params->owner;
  user->info.optimal_resolution = 96;
  user->info.optimal_width = 800;
  user->info.optimal_height = 600;

  rdp_client->settings = guac_rdp_parse_args(user.get(), args.size(), const_cast<const char**>(args.data()));

  std::thread(guac_rdp_client_thread, client).detach();
  StartWebSocketServer(*client, args);
}

int main()
{
  auto io_context = boost::asio::io_context(1);
  auto stopping = std::atomic_bool();
  auto work = boost::asio::make_work_guard(io_context);
  using CollabVm::Server::GuacamoleClient;
  auto guacamole_client = GuacamoleClient(io_context,
    [](auto& guacamole_client)
    {
      guacamole_client.AddUser();
    },
    [&stopping, &io_context, &work](auto& guacamole_client)
    {
      if (stopping)
      {
        work.reset();
      }
      else
      {
        // guacamole_client.StartRDP();
        guacamole_client.StartVNC();
      }
    });
  /*
  const auto args = std::unordered_map<std::string_view, std::string_view>{
    {"hostname", "localhost"},
    {"port", "3389"}
  };
  guacamole_client.StartRDP(args);
  */
  const auto args = std::unordered_map<std::string_view, std::string_view>{
    {"hostname", "localhost"},
    {"port", "5900"}
  };
  guacamole_client.StartVNC(args);
  // guacamole_client.StartRDP(args);

  auto interrupt_signal = boost::asio::signal_set(io_context, SIGINT, SIGTERM);
  interrupt_signal.async_wait([&guacamole_client, &interrupt_signal, &stopping]
    (auto error_code, auto signal)
    {
      stopping = true;
      guacamole_client.Stop();
    });

  io_context.run();

  return 0;
}

int test()
{
	const auto client = guac_client_alloc();
	client->log_handler = [](guac_client* client, guac_client_log_level level,
	                         const char* format, va_list args)
	{
    if (level > GUAC_LOG_DEBUG)
    {
      return;
    }
		char message[2048];
		vsnprintf(message, sizeof(message), format, args);
		std::cout << message << std::endl;
	};
	auto* broadcast_socket = guac_socket_alloc();
	broadcast_socket->write_handler = [](auto* socket, auto* data)
	{
		users_list.ForEach([data](auto* guac_user)
		{
			auto* socket = guac_user->socket;
			socket->write_handler(socket, data);
		});
		return ssize_t(1);
	};
	client->socket = broadcast_socket;

  StartRdpClient(client);
  // StartVncClient(client);
	return 0;
}
