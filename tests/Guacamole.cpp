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
#include <capnp/dynamic.h>
#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/io.h>
#include "CapnpMessageFrameBuilder.hpp"

#include <protocols/rdp/rdp.h>
#include <libguac/guacamole/client.h>
#include <libguac/guacamole/user.h>
#include <libguac/user-handlers.h>
#include "freerdp/client/channels.h"
#include <freerdp/addin.h>
#include "protocols/rdp/client.h"
#include <string>
#include <vector>
#include <map>
#include <iostream>


PVIRTUALCHANNELENTRY guac_channels_load_static_addin_entry(LPCSTR pszName,
                                                           LPSTR pszSubsystem, LPSTR pszType, DWORD dwFlags)
{
	return nullptr;
}

void StartWebSocketServer(guac_client& client)
{
	boost::asio::io_context ioc{1};
	boost::asio::ip::tcp::acceptor acceptor{ioc, {boost::asio::ip::make_address("127.0.0.1"), 8080}};
	while (true)
	{
		boost::asio::ip::tcp::socket socket{ioc};
		acceptor.accept(socket);
		std::thread(
			[socket=std::move(socket), &client]() mutable
			{
				auto& user = *guac_user_alloc();
				//	user->socket = socket;
				user.client = &client;
				//	user.owner  = params->owner;
				user.info.optimal_resolution = 96;
				user.info.optimal_width = 800;
				user.info.optimal_height = 600;
/*
                if (guac_client_add_user(client, user, args.size(), args.data()))
                {
                }
*/
				boost::beast::websocket::stream<boost::asio::ip::tcp::socket> websocket(std::move(socket));
				websocket.accept();
				boost::beast::multi_buffer buffer;

				// Read a message
				websocket.read(buffer);
                // NOTE: FlatMessageBuilder is used instead of FlatArrayMessageReader
			    // because Guacamole's user handlers require non-const char pointers
				const kj::ArrayPtr<capnp::word> array_ptr(
					static_cast<capnp::word*>((*buffer.data().begin()).data()),
					buffer.size() / sizeof(capnp::word));
				capnp::FlatMessageBuilder builder(array_ptr);
				auto message = builder.getRoot<Guacamole::GuacClientInstruction>();
                guac_call_instruction_handler(&user, message);

				// Echo the message back
				websocket.text(websocket.got_text());
				websocket.write(buffer.data());
			}).detach();
	}
}

int main()
{
	const auto client = guac_client_alloc();
	client->log_handler = [](guac_client* client, guac_client_log_level level,
	                         const char* format, va_list args)
	{
		char message[2048];
		vsnprintf(message, sizeof(message), format, args);
		std::cout << message << std::endl;
	};
    client->socket = nullptr;
	char* argv[] = {static_cast<char*>("")};
	guac_client_init(client, 0, argv);
	StartWebSocketServer(*client);

	guac_rdp_client* rdp_client = static_cast<guac_rdp_client*>(client->data);
	std::map<std::string, std::string> my_args{
		{"hostname", "localhost"},
		{"port", "3389"}
	};
	std::vector<char*> args;
	const char* arg_name;
	for (auto i = 0u; arg_name = client->args[i]; i++)
	{
		auto it = my_args.find(arg_name);
		args.push_back(it == my_args.end() ? static_cast<char*>("") : it->second.data());
	}

	const auto user = guac_user_alloc();
	//	user->socket = socket;
	user->client = client;
	//	user->owner  = params->owner;
	user->info.optimal_resolution = 96;
	user->info.optimal_width = 800;
	user->info.optimal_height = 600;

	rdp_client->settings = guac_rdp_parse_args(user, args.size(), const_cast<const char**>(args.data()));
	freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
	guac_rdp_client_thread(client);
	return 0;
}
