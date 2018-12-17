#pragma once
#include <capnp/message.h>
#include "CollabVm.capnp.h"

namespace CollabVm::Server {
template <typename TClient, unsigned MaxUsernameLen, unsigned MaxMessageLen>
class CollabVmChatRoom {
  constexpr static auto max_chat_message_history = 20;

 public:
  explicit CollabVmChatRoom(const std::uint32_t id)
      : id_(id), next_message_offset_(0) {
    auto channel_messages =
        history_message_builder_.initRoot<CollabVmServerMessage>()
            .initMessage()
            .initChatMessages();
    channel_messages.setChannel(id);
    channel_messages.initMessages(max_chat_message_history);
    for (auto&& message : chat_message_history_) {
      message.initSender(MaxUsernameLen);
      message.initMessage(MaxMessageLen);
    }
  }

  void AddUserMessage(
		CollabVmServerMessage::ChannelChatMessage::Builder channel_chat_message,
      const std::string& username,
      const std::string& message) {
    const auto timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    channel_chat_message.setChannel(id_);
    auto chat_message = channel_chat_message.initMessage();
    chat_message.setMessage(message);
    chat_message.setSender(username);
    chat_message.setTimestamp(timestamp);

    chat_message = chat_message_history_[next_message_offset_];
    next_message_offset_ = next_message_offset_ + 1 % max_chat_message_history;
    chat_message.setMessage(message);
    chat_message.setSender(username);
    chat_message.setTimestamp(timestamp);
  }

  capnp::MallocMessageBuilder& GetHistoryMessageBuilder() {
    return history_message_builder_;
  }

  capnp::List<CollabVmServerMessage::ChatMessage>::Reader GetChatHistory() const {
    return chat_message_history_.asReader();
  }

  std::uint32_t GetId() const {
    return id_;
  }
  /*
   * VmController vm_;
   * ChatMessages chat_msgs_;
   * UsersList users_;
   */
 private:
  std::uint32_t id_;
  std::uint8_t next_message_offset_;
  capnp::MallocMessageBuilder history_message_builder_;
  capnp::List<CollabVmServerMessage::ChatMessage>::Builder
      chat_message_history_;
};
}  // namespace CollabVm::Server
