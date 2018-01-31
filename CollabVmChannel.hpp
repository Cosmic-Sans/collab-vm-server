#pragma once

namespace CollabVm::Server {
	template<typename TClient, typename TChatRoom>
	class CollabVmChannel{
public:
		CollabVmChannel() {}
		void AddClient(const std::shared_ptr<TClient>& client) {
			chat_room_.AddClient(client);
		}
		TChatRoom& GetChatRoom() {
			return chat_room_;
		}
		std::vector<std::shared_ptr<TClient>>& GetClients() {
			return chat_room_.GetClients();
		}
	private:
		TChatRoom chat_room_;
	};
}
