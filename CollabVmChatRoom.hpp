#pragma once

#include <algorithm>
#include <capnp/message.h>
#include "CollabVm.capnp.h"

namespace CollabVm::Server {
template <typename TClient, unsigned MaxUsernameLen, unsigned MaxMessageLen>
class CollabVmChatRoom {
  const std::uint32_t id_;
  std::uint8_t next_message_offset_;
  capnp::MallocMessageBuilder history_message_builder_;
  CollabVmServerMessage::ChannelChatMessages::Builder channel_messages_;
  constexpr static auto max_chat_message_history = 20;
public:

  explicit CollabVmChatRoom(const std::uint32_t id)
    : id_(id),
      next_message_offset_(0),
      channel_messages_(
        history_message_builder_.initRoot<CollabVmServerMessage>()
        .initMessage()
        .initChatMessages()) {
    channel_messages_.setChannel(id);
    auto messages = channel_messages_.initMessages(max_chat_message_history);
    for (auto&& message : messages) {
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
        std::chrono::system_clock::now().time_since_epoch())
      .count();
    channel_chat_message.setChannel(id_);
    auto chat_message = channel_chat_message.initMessage();
    chat_message.setMessage(message);
    chat_message.setSender(username);
    chat_message.setTimestamp(timestamp);

    channel_messages_.setCount(
      (std::min)(channel_messages_.getCount() + 1, max_chat_message_history));
    chat_message = channel_messages_.getMessages()[next_message_offset_];
    next_message_offset_ = next_message_offset_ + 1 % max_chat_message_history;
    const auto message_body = chat_message.getMessage();
    copyStringToTextBuilder(message, message_body);
    copyStringToTextBuilder(username, chat_message.getSender());
    chat_message.setTimestamp(timestamp);
  }

  static void copyStringToTextBuilder(const std::string& string,
                                      capnp::Text::Builder text_builder)
  {
    const auto new_text_end = std::copy(string.begin(),
                                        string.end(),
                                        text_builder.begin());
    std::fill(new_text_end, text_builder.end(), '\0');
  }

  void GetChatHistory(
      CollabVmServerMessage::ChannelConnectResponse::ConnectInfo::Builder
      connect_success) {
    const auto messages_count = channel_messages_.getCount();
    if (messages_count == max_chat_message_history) {
      connect_success.setChatMessages(channel_messages_.getMessages());
    } else {
      auto messages = connect_success.initChatMessages(messages_count);
      for (auto i = 0u; i < messages_count; i++) {
        messages.setWithCaveats(i, channel_messages_.getMessages()[i]);
      }
    }
  }

  std::uint32_t GetId() const {
    return id_;
  }
};
}  // namespace CollabVm::Server
