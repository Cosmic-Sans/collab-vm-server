#pragma once
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/range/adaptors.hpp>
//#include <cctype>
#include <gsl/span>
#include <memory>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>
#ifdef WIN32
#include <kj/windows-sanity.h>
#undef VOID
#undef CONST
#undef SendMessage
#endif
#include <capnp/blob.h>
#include <capnp/dynamic.h>
#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/io.h>
#include "CapnpMessageFrameBuilder.hpp"
#include "CollabVm.capnp.h"
#include "CollabVmShared.hpp"
#include "CollabVmChannel.hpp"
#include "CollabVmChatRoom.hpp"
#include "Database/Database.h"
#include "Recaptcha.hpp"
#include "StrandGuard.hpp"
#include "Totp.hpp"
#include "WebSocketServer.hpp"

namespace CollabVm::Server {
template <typename TServer>
class CollabVmServer final : public TServer {
  using strand = typename TServer::strand;
  template <typename T>
  using StrandGuard = StrandGuard<strand, T>;

 public:
  //constexpr static auto min_username_len = 3;
  //constexpr static auto max_username_len = 20;
  constexpr static auto max_chat_message_len = 100;

  template <typename TSocket>
  class CollabVmSocket final : public TSocket {
    using SessionMap = std::unordered_map<
        Database::SessionId,
        std::shared_ptr<CollabVmSocket<typename TServer::TSocket>>,
        Database::SessionIdHasher>;

   public:
    CollabVmSocket(asio::io_context& io_context,
                   const boost::filesystem::path& doc_root,
                   CollabVmServer& server)
        : TSocket(io_context, doc_root),
          server_(server),
          io_context_(io_context),
          send_queue_(io_context),
          sending_(false),
          chat_rooms_(io_context_),
          chat_rooms_id_(1),
          is_logged_in_(false),
          is_admin_(false),
          is_viewing_server_config(false) {}

    void OnMessage(std::shared_ptr<beast::flat_static_buffer_base>&& buffer) {
      const auto buffer_data = buffer->data();
      const kj::ArrayPtr<const capnp::word> array_ptr(
          static_cast<const capnp::word*>(buffer_data.data()),
          buffer_data.size() / sizeof(capnp::word));
      try {
        HandleMessage(std::move(buffer), array_ptr);
      } catch (...) {
        TSocket::Close();
      }
    }

    void HandleMessage(std::shared_ptr<beast::flat_static_buffer_base>&& buffer,
                       const kj::ArrayPtr<const capnp::word> array) {
      capnp::FlatArrayMessageReader reader(array);
      auto message = reader.getRoot<CollabVmClientMessage>().getMessage();
      switch (message.which()) {
        case CollabVmClientMessage::Message::CONNECT_TO_CHANNEL: {
          auto socket_message = CreateSharedSocketMessage();
          auto& message_builder = socket_message->GetMessageBuilder();
          auto connect_result =
              message_builder.initRoot<CollabVmServerMessage>()
                  .initMessage()
                  .initConnectResponse()
                  .initResult();
          server_.channels_.dispatch([
            this, self = shared_from_this(),
            channel_id = message.getConnectToChannel(), socket_message,
            connect_result
          ](auto& channels) mutable {
            auto channel_it = channels.find(channel_id);
            if (channel_it == channels.end()) {
              connect_result.setFail();
              QueueMessage(std::move(socket_message));
              return;
            }
            auto channel = channel_it->second;
            channel->AddClient(shared_from_this());
            auto connect_success = connect_result.initSuccess();
            connect_success.setChatMessages(
                channel->GetChatRoom().GetChatHistory());
          });
          break;
        }
        case CollabVmClientMessage::Message::CHANGE_USERNAME: {
          if (is_logged_in_) {
            // Registered users can't change their usernames
            break;
          }
          const auto new_username = message.getChangeUsername();
          if (!CollabVm::Shared::ValidateUsername({new_username.begin(), new_username.size()})) {
            break;
          }
          server_.guests_.dispatch([
            this, self = shared_from_this(), buffer = std::move(buffer),
            new_username
          ](auto& guests) {
            auto guests_it = guests.find(new_username);
            auto socket_message = CreateSharedSocketMessage();
            auto message = socket_message->GetMessageBuilder()
                               .initRoot<CollabVmServerMessage>()
                               .initMessage();
            if (guests_it != guests.end()) {
              message.setUsernameTaken();
              QueueMessage(std::move(socket_message));
              return;
            }
            auto username_change = message.initChangeUsername();
            username_change.setOldUsername(username_);
            username_change.setNewUsername(new_username);
            username_ = new_username;
            guests[username_] = self;
            QueueMessage(std::move(socket_message));
          });
          break;
        }
        case CollabVmClientMessage::Message::CHAT_MESSAGE: {
          if (username_.empty()) {
            break;
          }
          const auto chat_message = message.getChatMessage();
          const auto message_len = chat_message.getMessage().size();
          if (!message_len || message_len > max_chat_message_len) {
            break;
          }
          const auto destination =
              chat_message.getDestination().getDestination();
          switch (destination.which()) {
            case CollabVmClientMessage::ChatMessageDestination::Destination::
                NEW_DIRECT:
              server_.guests_.dispatch([
                this, self = shared_from_this(), buffer = std::move(buffer),
                chat_message, username = destination.getNewDirect()
              ](auto& guests) mutable {
                auto guests_it = guests.find(username);
                if (guests_it == guests.end()) {
                  SendChatMessageResponse(
                      CollabVmServerMessage::ChatMessageResponse::
                          USER_NOT_FOUND);
                  return;
                }
                chat_rooms_.dispatch([
                  this, self = shared_from_this(), buffer = std::move(buffer),
                  chat_message, recipient = guests_it->second
                ](auto& chat_rooms) {
                  if (chat_rooms.size() >= 10) {
                    SendChatMessageResponse(
                        CollabVmServerMessage::ChatMessageResponse::
                            USER_CHAT_LIMIT);
                    return;
                  }
                  auto existing_chat_room =
                      std::find_if(chat_rooms.begin(), chat_rooms.end(),
                                   [&recipient](const auto& room) {
                                     return room.second.first == recipient;
                                   });
                  if (existing_chat_room != chat_rooms.end()) {
                    SendChatChannelId(existing_chat_room->second.second);
                    return;
                  }
                  const auto id = chat_rooms_id_++;
                  chat_rooms.emplace(id, std::make_pair(recipient, 0));
                  recipient->chat_rooms_.dispatch([
                    this, self = shared_from_this(), buffer = std::move(buffer),
                    chat_message, recipient, sender_id = id
                  ](auto& recipient_chat_rooms) {
                    auto existing_chat_room = std::find_if(
                        recipient_chat_rooms.begin(),
                        recipient_chat_rooms.end(), [&self](const auto& room) {
                          return room.second.first == self;
                        });
                    if (existing_chat_room != recipient_chat_rooms.end()) {
                      if (!existing_chat_room->second.second) {
                        existing_chat_room->second.second = sender_id;
                        return;
                      }
                      SendChatChannelId(sender_id);
                      return;
                    }
                    if (recipient_chat_rooms.size() >= 10) {
                      chat_rooms_.dispatch([
                        this, self = shared_from_this(), sender_id
                      ](auto& chat_rooms) {
                        chat_rooms.erase(sender_id);
                        SendChatMessageResponse(
                            CollabVmServerMessage::ChatMessageResponse::
                                RECIPIENT_CHAT_LIMIT);
                      });
                      return;
                    }
                    const auto recipient_id = recipient->chat_rooms_id_++;
                    recipient_chat_rooms.emplace(
                        recipient_id, std::make_pair(recipient, sender_id));
                    chat_rooms_.dispatch([
                      this, self = shared_from_this(),
                      buffer = std::move(buffer), chat_message, recipient,
                      sender_id, recipient_id
                    ](auto& chat_rooms) {
                      auto chat_rooms_it = chat_rooms.find(sender_id);
                      if (chat_rooms_it != chat_rooms.end() &&
                          !chat_rooms_it->second.second) {
                        chat_rooms_it->second.second = recipient_id;
                        SendChatChannelId(sender_id);

                        auto socket_message = CreateSharedSocketMessage();
                        auto channel_message =
                            socket_message->GetMessageBuilder()
                                .initRoot<CollabVmServerMessage>()
                                .initMessage()
                                .initNewChatChannel();
                        channel_message.setChannel(recipient_id);
                        auto message = channel_message.initMessage();
                        message.setMessage(chat_message.getMessage());
                        //                        message.setSender(username);
                        //    message.setTimestamp(timestamp);
                        recipient->QueueMessage(std::move(socket_message));
                        QueueMessage(std::move(socket_message));
                      }
                    });
                  });
                });
              });
              break;
            case CollabVmClientMessage::ChatMessageDestination::Destination::
                DIRECT: {
              chat_rooms_.dispatch([
                this, self = shared_from_this(), buffer = std::move(buffer),
                chat_message, destination
              ](const auto& chat_rooms) mutable {
                const auto id = destination.getDirect();
                const auto chat_rooms_it = chat_rooms.find(id);
                if (chat_rooms_it == chat_rooms.end()) {
                  // TODO: Tell the client the message could not be sent
                  return;
                }
                const auto recipient = chat_rooms_it->second.first;
                recipient->QueueMessage(CreateChatMessage(
                    id, username_, chat_message.getMessage()));
              });
              break;
            }
            case CollabVmClientMessage::ChatMessageDestination::Destination::VM:
              server_.channels_.dispatch([
                this, self = TSocket::shared_from_this(),
                buffer = std::move(buffer), chat_message,
                id = destination.getVm()
              ](auto& channels) {
                auto channel_it = channels.find(id);
                if (channel_it == channels.end()) {
                  return;
                }
                auto& chat_room = channel_it->second->GetChatRoom();
                auto new_chat_message = CreateSharedSocketMessage();
                auto channel_chat_message =
                    new_chat_message->GetMessageBuilder()
                        .initRoot<CollabVmServerMessage>()
                        .initMessage()
                        .initChatMessage();
                chat_room.AddUserMessage(channel_chat_message, username_,
                                         chat_message.getMessage());
                for (auto&& client_ptr : chat_room.GetClients()) {
                  client_ptr->QueueMessage(new_chat_message);
                }
              });
              break;
            default:
              break;
          }
          break;
        }
        case CollabVmClientMessage::Message::VM_LIST_REQUEST: {
          server_.SendChannelList(*this);
          break;
        }
        case CollabVmClientMessage::Message::LOGIN_REQUEST: {
          auto login_request = message.getLoginRequest();
          const auto username = login_request.getUsername();
          const auto password = login_request.getPassword();
          const auto captcha_token = login_request.getCaptchaToken();
          boost::system::error_code ec;
          server_.recaptcha_.Verify(
              captcha_token,
              [
                this, self = TSocket::shared_from_this(),
                buffer = std::move(buffer), username, password
              ](bool is_valid) mutable {
                auto socket_message = CreateSharedSocketMessage();
                auto& message_builder = socket_message->GetMessageBuilder();
                auto login_response =
                    message_builder.initRoot<CollabVmServerMessage::Message>()
                        .initLoginResponse()
                        .initResult();
                if (is_valid) {
                  auto lambda = [
                    this, self = std::move(self), socket_message,
                    buffer = std::move(buffer), login_response, username,
                    password
                  ]() mutable {
                    const auto[login_result, totp_key] =
                        server_.db_.Login(username, password);
                    if (login_result == CollabVmServerMessage::LoginResponse::
                                            LoginResult::SUCCESS) {
                      server_.CreateSession(
                          shared_from_this(), username,
                          [
                            this, self = std::move(self), socket_message,
                            login_response
                          ](const std::string& username,
                            const SessionId& session_id) mutable {
                            auto session = login_response.initSession();
                            session.setSessionId(capnp::Data::Reader(
                                session_id.data(), session_id.size()));
                            session.setUsername(username);
                            QueueMessage(std::move(socket_message));
                          });
                    } else {
                      if (login_result ==
                          CollabVmServerMessage::LoginResponse::LoginResult::
                              TWO_FACTOR_REQUIRED) {
                        totp_key_ = std::move(totp_key);
                      }
                      login_response.setResult(login_result);
                      QueueMessage(std::move(socket_message));
                    }
                  };
                  server_.login_strand_.post(
                      std::move(lambda), std::allocator<decltype(lambda)>());
                } else {
                  login_response.setResult(
                      CollabVmServerMessage::LoginResponse::LoginResult::
                          INVALID_CAPTCHA_TOKEN);
                  QueueMessage(std::move(socket_message));
                }
              },
              TSocket::GetIpAddress().AsString());
          break;
        }
        case CollabVmClientMessage::Message::TWO_FACTOR_RESPONSE: {
          Totp::ValidateTotp(message.getTwoFactorResponse(),
                             gsl::as_bytes(gsl::make_span(&totp_key_.front(),
                                                          totp_key_.size())));
          break;
        }
        case CollabVmClientMessage::Message::ACCOUNT_REGISTRATION_REQUEST: {
          server_.settings_.dispatch([
            this, self = TSocket::shared_from_this(),
            buffer = std::move(buffer), message
          ](auto& settings) mutable {
            if (!settings
                     .GetServerSetting(
                         ServerSetting::Setting::ALLOW_ACCOUNT_REGISTRATION)
                     .getAllowAccountRegistration()) {
              return;
            }
            auto socket_message = CreateSharedSocketMessage();
            auto& message_builder = socket_message->GetMessageBuilder();
            auto server_message =
                message_builder.initRoot<CollabVmServerMessage::Message>();
            auto response = server_message.initAccountRegistrationResponse();
            auto registration_result = response.initResult();

            auto register_request = message.getAccountRegistrationRequest();
            auto username = register_request.getUsername();
            if (!Shared::ValidateUsername({username.begin(), username.size()})) {
              registration_result.setErrorStatus(
                  CollabVmServerMessage::RegisterAccountResponse::
                      RegisterAccountError::USERNAME_INVALID);
              QueueMessage(std::move(socket_message));
              return;
            }
            if (register_request.getPassword().size() >
                Database::max_password_len) {
              registration_result.setErrorStatus(
                  CollabVmServerMessage::RegisterAccountResponse::
                      RegisterAccountError::PASSWORD_INVALID);
              QueueMessage(std::move(socket_message));
              return;
            }
            std::optional<gsl::span<const std::byte, TOTP_KEY_LEN>> totp_key;
            if (register_request.hasTwoFactorToken()) {
              auto two_factor_token = register_request.getTwoFactorToken();
              if (two_factor_token.size() != Database::totp_token_len) {
                registration_result.setErrorStatus(
                    CollabVmServerMessage::RegisterAccountResponse::
                        RegisterAccountError::TOTP_ERROR);
                QueueMessage(std::move(socket_message));
                return;
              }
              totp_key =
                  gsl::as_bytes(gsl::make_span<const capnp::byte, TOTP_KEY_LEN>(
                      reinterpret_cast<const capnp::byte(&)[TOTP_KEY_LEN]>(
                          *two_factor_token.begin())));
            }
            const auto register_result = server_.db_.CreateAccount(
                username, register_request.getPassword(), totp_key,
                TSocket::GetIpAddress().AsBytes());
            if (register_result !=
                CollabVmServerMessage::RegisterAccountResponse::
                    RegisterAccountError::SUCCESS) {
              registration_result.setErrorStatus(register_result);
              QueueMessage(std::move(socket_message));
              return;
            }
            server_.CreateSession(
                shared_from_this(), username,
                [
                  this, self = shared_from_this(), buffer = std::move(buffer),
                  socket_message, registration_result
                ](const std::string& username,
                  const SessionId& session_id) mutable {
                  auto session = registration_result.initSession();
                  session.setSessionId(capnp::Data::Reader(session_id.data(),
                                                           session_id.size()));
                  session.setUsername(username);
                  QueueMessage(std::move(socket_message));
                });
          });
          break;
        }
        case CollabVmClientMessage::Message::SERVER_CONFIG_REQUEST: {
          if (is_admin_) {
            server_.settings_.dispatch(
                [ this, self = shared_from_this() ](auto& settings) {
                  QueueMessage(CreateCopiedSocketMessage(
                      settings.GetServerSettingsMessageBuilder()));
                });
            if (!is_viewing_server_config) {
              is_viewing_server_config = true;
              server_.viewing_admins_.dispatch([self = shared_from_this()](
                  auto& viewing_admins) {
                viewing_admins.emplace_back(std::move(self));
              });
            }
          }
          break;
        }
        case CollabVmClientMessage::Message::SERVER_CONFIG_MODIFICATIONS:
          if (is_admin_) {
            auto changed_settings = message.getServerConfigModifications();
            for (const auto& setting_message : changed_settings) {
              // TODO: Validate setttings
            }
            server_.settings_.dispatch([
              this, self = shared_from_this(), buffer = std::move(buffer),
              changed_settings
            ](auto& settings) mutable {
              settings.UpdateServerSettings<ServerSetting>(changed_settings);
              auto config_message = CreateCopiedSocketMessage(
                  settings.GetServerSettingsMessageBuilder());
              // Broadcast the config changes to all other admins viewing the
              // admin panel
              server_.viewing_admins_.dispatch([
                self = std::move(self),
                config_message = std::move(config_message)
              ](auto& viewing_admins) {
                if (viewing_admins.empty() ||
                    viewing_admins.size() == 1 &&
                        viewing_admins.front() == self) {
                  return;
                }
                // TODO: Is moving this safe?
                for (auto& admin : viewing_admins) {
                  if (admin != self) {
                    admin->QueueMessage(config_message);
                  }
                }
              });
            });
          }
          break;
        case CollabVmClientMessage::Message::SERVER_CONFIG_HIDDEN:
          LeaveServerConfig();
          break;
        case CollabVmClientMessage::Message::CREATE_VM:
          if (is_admin_) {
            const auto settings = message.getCreateVm();
            const auto vm_id =
                server_.db_.CreateVm(settings.begin(), settings.end());
            server_.vms_.dispatch([
              this, self = shared_from_this(), buffer = std::move(buffer),
              settings, vm_id
            ](auto& vms) {
              auto[it, inserted_new] = vms.emplace(vm_id, settings);
              assert(inserted_new);
              auto socket_message = CreateSharedSocketMessage();
							socket_message->GetMessageBuilder().initRoot<CollabVmServerMessage>().initMessage().
															setCreateVmResponse(vm_id);
              QueueMessage(socket_message);
              if (it->second.GetSetting(VmSetting::Setting::ENABLED)
                      .getEnabled()) {
              }
            });
          }
          break;
        case CollabVmClientMessage::Message::READ_VMS:
          if (is_admin_) {
            server_.vms_.dispatch([ this,
                                    self = shared_from_this() ](auto& vms) {
              auto socket_message = CreateSharedSocketMessage();
              auto list = socket_message->GetMessageBuilder()
                              .initRoot<CollabVmServerMessage>()
                              .initMessage()
                              .initReadVmsResponse(vms.size());
              auto it = vms.begin();
              for (std::size_t i = 0; i < vms.size(); i++, it++) {
                auto vm_info = list[i];
                auto& vm = it->second;
                vm_info.setId(it->first);
                vm_info.setName(
                    vm.GetSetting(VmSetting::Setting::NAME).getName());
                vm_info.setEnabled(
                    vm.GetSetting(VmSetting::Setting::ENABLED).getEnabled());
              }
              QueueMessage(socket_message);
            });
          }
          break;
        case CollabVmClientMessage::Message::READ_VM_CONFIG:
          if (is_admin_) {
            const auto vm_id = message.getReadVmConfig();
            server_.vms_.dispatch([ this, self = shared_from_this(),
                                    vm_id ](auto& vms) {
              const auto it = vms.find(vm_id);
              if (it == vms.end()) {
                return;
              }
              QueueMessage(
                  CreateCopiedSocketMessage(it->second.GetMessageBuilder()));
            });
          }
          break;
        case CollabVmClientMessage::Message::UPDATE_VM_CONFIG:
          if (is_admin_) {
            /*
                        std::vector<VmSetting::Setting::Which> settings;
                        const auto modified_vm = message.getUpdateVmConfig();
                        const auto modified_settings =
               modified_vm.getModifications();
                        settings.reserve(modified_settings.size());
                        for (const auto& setting_message : modified_settings) {
                          const auto setting = setting_message.getSetting();
                          const auto setting_id = setting.which();
                          if (!ValidateVmSetting(setting_id, setting)) {
                            break;
                          }
                          settings.push_back(setting_id);
                          capnp::DynamicStruct::Builder current_setting =
                              server_.db_.settings_[setting_id].getSetting();
                          const capnp::DynamicStruct::Reader reader = setting;
                          KJ_IF_MAYBE(field, reader.which()) {
                            current_setting.set(*field, reader.get(*field));
                          }
                        }
                        server_.db_.UpdateServerSettings(settings.begin(),
               settings.end());
            */
          }
          break;
        case CollabVmClientMessage::Message::CREATE_INVITE:
          if (is_admin_) {
            auto invite = message.getCreateInvite();
            auto socket_message = CreateSharedSocketMessage();
            auto invite_result = socket_message->GetMessageBuilder()
                                     .initRoot<CollabVmServerMessage>()
                                     .initMessage();
            if (const auto id = server_.db_.CreateInvite(
                    invite.getInviteName(), invite.getUsername(),
                    invite.getUsernameReserved(), invite.getAdmin())) {
              invite_result.setCreateInviteResult(
                  capnp::Data::Reader(id->data(), id->size()));
            } else {
              invite_result.initCreateInviteResult(0);
            }
            QueueMessage(std::move(socket_message));
          }
          break;
        case CollabVmClientMessage::Message::READ_INVITES:
          if (is_admin_) {
            auto socket_message = CreateSharedSocketMessage();
            auto response = socket_message->GetMessageBuilder()
                                .initRoot<CollabVmServerMessage>()
                                .initMessage();
            server_.db_.ReadInvites([&response](auto& invites) {
              auto invites_list =
                  response.initReadInvitesResponse(invites.size());
              auto invites_list_it = invites_list.begin();
              auto invites_it = invites.begin();
              for (; invites_list_it != invites_list.end();
                   invites_list_it++, invites_it++) {
                invites_list_it->setId(capnp::Data::Reader(
                    reinterpret_cast<const capnp::byte*>(invites_it->Id.data()),
                    invites_it->Id.length()));
                invites_list_it->setInviteName(invites_it->InviteName);
              }
            });
            QueueMessage(std::move(socket_message));
          }
          break;
        case CollabVmClientMessage::Message::UPDATE_INVITE:
          if (is_admin_) {
            const auto invite = message.getUpdateInvite();
            auto socket_message = CreateSharedSocketMessage();
            const auto invite_id = message.getDeleteInvite().asChars();
            const auto result = server_.db_.UpdateInvite(
                std::string(invite_id.begin(), invite_id.size()),
                invite.getUsername(), invite.getAdmin());
            socket_message->GetMessageBuilder()
                .initRoot<CollabVmServerMessage>()
                .initMessage()
                .setUpdateInviteResult(result);
            QueueMessage(std::move(socket_message));
          }
          break;
        case CollabVmClientMessage::Message::DELETE_INVITE:
          if (is_admin_) {
            const auto invite_id = message.getDeleteInvite().asChars();
            server_.db_.DeleteInvite(
                std::string(invite_id.begin(), invite_id.size()));
          }
          break;
        case CollabVmClientMessage::Message::CREATE_RESERVED_USERNAME:
          if (is_admin_) {
            server_.db_.CreateReservedUsername(
                message.getCreateReservedUsername());
          }
          break;
        case CollabVmClientMessage::Message::READ_RESERVED_USERNAMES:
          if (is_admin_) {
            auto socket_message = CreateSharedSocketMessage();
            auto response = socket_message->GetMessageBuilder()
                                .initRoot<CollabVmServerMessage>()
                                .initMessage();
            server_.db_.ReadReservedUsernames([&response](auto& usernames) {
              auto usernames_list =
                  response.initReadReservedUsernamesResponse(usernames.size());
              auto usernames_list_it = usernames_list.begin();
              auto usernames_it = usernames.begin();
              for (; usernames_list_it != usernames_list.end();
                   usernames_list_it++, usernames_it++) {
                auto username = usernames_it->Username;
                *usernames_list_it = {&*username.begin(), username.length()};
              }
            });
            QueueMessage(std::move(socket_message));
          }
          break;
        case CollabVmClientMessage::Message::DELETE_RESERVED_USERNAME:
          if (is_admin_) {
            server_.db_.DeleteReservedUsername(
                message.getDeleteReservedUsername());
          }
          break;
        default:
          TSocket::Close();
      }
    }

   private:
    void SendChatChannelId(const std::uint32_t id) {
      auto socket_message = CreateSharedSocketMessage();
      auto& message_builder = socket_message->GetMessageBuilder();
      auto message = message_builder.initRoot<CollabVmServerMessage>()
                         .initMessage()
                         .initChatMessage();
      message.setChannel(id);
      QueueMessage(std::move(socket_message));
    }

    void SendChatMessageResponse(
        CollabVmServerMessage::ChatMessageResponse result) {
      auto socket_message = CreateSharedSocketMessage();
      socket_message->GetMessageBuilder()
          .initRoot<CollabVmServerMessage>()
          .initMessage()
          .setChatMessageResponse(result);
      QueueMessage(std::move(socket_message));
    }

    bool ValidateVmSetting(std::uint16_t setting_id,
                           const VmSetting::Setting::Reader& setting) {
      switch (setting_id) {
        case VmSetting::Setting::TURN_TIME:
          return setting.getTurnTime() > 0;
        case VmSetting::Setting::DESCRIPTION:
          return setting.getDescription().size() <= 200;
        default:
          return true;
      }
    }

    SessionId SetSessionId(SessionMap& sessions, SessionId&& session_id) {
      const auto[session_id_pair, inserted_new] =
          sessions.emplace(std::move(session_id), shared_from_this());
      assert(inserted_new);
      return session_id_pair->first;
    }

    std::shared_ptr<CollabVmSocket> shared_from_this() {
      return std::static_pointer_cast<
          CollabVmSocket<typename TServer::TSocket>>(
          TSocket::shared_from_this());
    }

    struct SocketMessage : std::enable_shared_from_this<SocketMessage> {
     protected:
      ~SocketMessage() = default;

     public:
      virtual std::vector<boost::asio::const_buffer>& GetBuffers(
          CapnpMessageFrameBuilder<>&) = 0;
    };

    struct CopiedSocketMessage : SocketMessage {
      CopiedSocketMessage(capnp::MallocMessageBuilder& message_builder)
          : buffer_(kj::Array<capnp::word>(
                capnp::messageToFlatArray(message_builder))),
            framed_buffers_(
                {boost::asio::const_buffer(buffer_.asBytes().begin(),
                                           buffer_.asBytes().size())}) {}
      std::vector<boost::asio::const_buffer>& GetBuffers(
          CapnpMessageFrameBuilder<>&) override {
        return framed_buffers_;
      }

     private:
      kj::Array<capnp::word> buffer_;
      std::vector<boost::asio::const_buffer> framed_buffers_;
    };

    static std::shared_ptr<CopiedSocketMessage> CreateCopiedSocketMessage(
        capnp::MallocMessageBuilder& message_builder) {
      return std::make_shared<CopiedSocketMessage>(message_builder);
    }

    struct SharedSocketMessage : SocketMessage {
      std::vector<boost::asio::const_buffer>& GetBuffers(
          CapnpMessageFrameBuilder<>& frame_builder) override {
        auto segments = shared_message_builder.getSegmentsForOutput();
        const auto segment_count = segments.size();
        framed_buffers_.resize(segment_count + 1);
        const auto it = framed_buffers_.begin();
        frame_builder.Init(segment_count);
        const auto& frame = frame_builder.GetFrame();
        *it = boost::asio::const_buffer(frame.data(),
                                        frame.size() * sizeof(frame.front()));
        std::transform(
            segments.begin(), segments.end(), std::next(it),
            [&frame_builder](const kj::ArrayPtr<const capnp::word> a) {
              frame_builder.AddSegment(a.size());
              return boost::asio::const_buffer(a.begin(),
                                               a.size() * sizeof(a[0]));
            });
        frame_builder.Finalize(segment_count);
        return framed_buffers_;
      }

      capnp::MallocMessageBuilder& GetMessageBuilder() {
        return shared_message_builder;
      }

     private:
      capnp::MallocMessageBuilder shared_message_builder;
      std::vector<boost::asio::const_buffer> framed_buffers_;
    };

    static std::shared_ptr<SharedSocketMessage> CreateSharedSocketMessage() {
      return std::make_shared<SharedSocketMessage>();
    }

    void SendMessage(std::shared_ptr<CollabVmSocket>&& self,
                     std::shared_ptr<SocketMessage>&& socket_message) {
      const auto& segment_buffers = socket_message->GetBuffers(frame_builder_);
      TSocket::WriteMessage(
          segment_buffers,
          send_queue_.wrap([ this, self = std::move(self), socket_message ](
              auto& send_queue, const beast::error_code& ec,
              std::size_t bytes_transferred) mutable {
            if (ec) {
              TSocket::Close();
              return;
            }
            if (send_queue.empty()) {
              sending_ = false;
              return;
            }
            SendMessage(std::move(self), std::move(send_queue.front()));
            send_queue.pop();
          }));
    }

    void QueueMessage(std::shared_ptr<SocketMessage>&& socket_message) {
      send_queue_.dispatch([
        this, self = shared_from_this(),
        socket_message =
            std::forward<std::shared_ptr<SocketMessage>>(socket_message)
      ](auto& send_queue) mutable {
        if (sending_) {
          send_queue.push(socket_message);
        } else {
          sending_ = true;
          SendMessage(std::move(self), std::move(socket_message));
        }
      });
    }

    void OnDisconnect() { LeaveServerConfig(); }

    void LeaveServerConfig() {
      if (is_viewing_server_config) {
        is_viewing_server_config = false;
        server_.viewing_admins_.dispatch(
            [ this, self = shared_from_this() ](auto& viewing_admins) {
              const auto it = std::find(viewing_admins.cbegin(),
                                        viewing_admins.cend(), self);
              if (it != viewing_admins.cend()) {
                viewing_admins.erase(it);
              }
            });
      }
    }

    void InvalidateSession() {
      // TODO:
    }

    static std::shared_ptr<SharedSocketMessage> CreateChatMessage(
        const std::uint32_t channel_id,
        const capnp::Text::Reader sender,
        const capnp::Text::Reader message) {
      const auto timestamp =
          std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count();
      auto socket_message = CreateSharedSocketMessage();
      auto& message_builder = socket_message->GetMessageBuilder();
      auto channel_chat_message =
          message_builder.initRoot<CollabVmServerMessage>()
              .initMessage()
              .initChatMessage();
      channel_chat_message.setChannel(channel_id);
      auto chat_message = channel_chat_message.initMessage();
      chat_message.setMessage(message);
      chat_message.setSender(sender);
      chat_message.setTimestamp(timestamp);
      return socket_message;
    }

    CollabVmServer& server_;
    boost::asio::io_context& io_context_;
    CapnpMessageFrameBuilder<> frame_builder_;
    StrandGuard<std::queue<std::shared_ptr<SocketMessage>>> send_queue_;
    bool sending_;
    //    StrandGuard<
    //        std::unordered_map<std::uint32_t,
    //                           std::shared_ptr<CollabVmChatRoom<CollabVmSocket>>>>
    StrandGuard<std::unordered_map<
        std::uint32_t,
        std::pair<std::shared_ptr<CollabVmSocket>, std::uint32_t>>>
        chat_rooms_;
    std::uint32_t chat_rooms_id_;

    std::vector<std::uint8_t> totp_key_;
    bool is_logged_in_;
    bool is_admin_;
    bool is_viewing_server_config;
    std::string username_;
    friend class CollabVmServer;
  };

  CollabVmServer(const std::string& doc_root, const std::uint8_t threads)
      : TServer(doc_root, threads),
        settings_(TServer::io_context_, db_),
        vm_settings_(TServer::io_context_),
        sessions_(TServer::io_context_),
        guests_(TServer::io_context_),
        ssl_ctx_(boost::asio::ssl::context::sslv23),
        recaptcha_(TServer::io_context_, ssl_ctx_, ""),
        channels_(TServer::io_context_),
        vms_(TServer::io_context_),
        login_strand_(TServer::io_context_),
        viewing_admins_(TServer::io_context_),
        guest_rng_(1'000, 99'999) {
    /*
                    vm_settings_.dispatch([](auto& vm_settings)
                    {
                            db_.LoadServerSettings()
                    });
    */
    settings_.dispatch([this](auto& settings) {
      settings.LoadServerSettings();
      settings.GetServerSetting(ServerSetting::Setting::RECAPTCHA_KEY)
          .getRecaptchaKey();
    });
  }

 protected:
  std::shared_ptr<typename TServer::TSocket> CreateSocket(
      asio::io_context& io_context,
      const boost::filesystem::path& doc_root) {
    return std::make_shared<CollabVmSocket<typename TServer::TSocket>>(
        io_context, doc_root, *this);
  }

 private:
  template <typename TCallback>
  void CreateSession(
      std::shared_ptr<CollabVmSocket<typename TServer::TSocket>>&& socket,
      const std::string& username,
      TCallback&& callback) {
    sessions_.dispatch([
      this, socket = std::move(socket), username, callback = std::move(callback)
    ](auto& sessions) mutable {
      auto[correct_username, is_admin, old_session_id, new_session_id] =
          db_.CreateSession(username, socket->GetIpAddress().AsBytes());
      if (correct_username.empty()) {
        // TODO: Handle error
        return;
      }
      socket->username_ = std::move(correct_username);
      socket->is_logged_in_ = true;
      socket->is_admin_ = is_admin;
      // TODO: Can SetSessionId return a reference?
      new_session_id =
          socket->SetSessionId(sessions, std::move(new_session_id));
      if (old_session_id.size()) {
        auto it = sessions.find(old_session_id);
        if (it != sessions.end()) {
          it->second->InvalidateSession();
        }
      }
      callback(socket->username_, new_session_id);
    });
  }

  void SendChannelList(CollabVmSocket<typename TServer::TSocket>& socket) {
    auto message_builder = new capnp::MallocMessageBuilder();
    auto vm_info_list =
        message_builder->initRoot<CollabVmServerMessage::Message>()
            .initVmListResponse(1);
    vm_info_list[0].setName("asdf");
    vm_info_list[0].setOperatingSystem("Windows XP");
    vm_info_list[0].setHost("collabvm.com");
    //    socket.QueueMessage(std::move(socket_message));
  }

  std::string GenerateUsername() {
    auto num = guest_rng_(rng_);
    auto username = "guest" + std::to_string(num);
    // Increment the number until a username is found that is not taken
    while (usernames_.find(username) != usernames_.end()) {
      username = "guest" + std::to_string(++num);
    }
    return username;
  }

  using Socket = CollabVmSocket<typename TServer::TSocket>;

  Database db_;
  struct ServerSettingsList {
    ServerSettingsList(Database& db)
        : db_(db),
          settings_(std::make_unique<capnp::MallocMessageBuilder>()),
          settings_list_(InitSettings(*settings_)) {}
    ServerSetting::Setting::Reader GetServerSetting(
        ServerSetting::Setting::Which setting) {
      return settings_list_[setting].getSetting().asReader();
    }
    capnp::MallocMessageBuilder& GetServerSettingsMessageBuilder() const {
      return *settings_;
    }

    void LoadServerSettings() { db_.LoadServerSettings(settings_list_); }

    template <typename TList>
    void UpdateList(const typename capnp::List<TList>::Reader old_list,
                    typename capnp::List<TList>::Builder new_list,
                    const typename capnp::List<TList>::Reader list_updates) {
      for (auto i = 0; i < new_list.size(); i++) {
        auto changed = false;
        // TODO: Make this more generic so it doesn't depend on getSetting()
        capnp::DynamicStruct::Builder current_setting =
            new_list[i].getSetting();
        for (auto updates_it = list_updates.begin();
             updates_it != list_updates.end(); updates_it++) {
          const auto updated_setting = updates_it->getSetting();
          if (updated_setting.which() == i) {
            const capnp::DynamicStruct::Reader reader = updated_setting;
            KJ_IF_MAYBE(field, reader.which()) {
              current_setting.set(*field, reader.get(*field));
              changed = true;
              break;
            }
          }
        }
        if (!changed) {
          const capnp::DynamicStruct::Reader reader = old_list[i].getSetting();
          KJ_IF_MAYBE(field, reader.which()) {
            current_setting.set(*field, reader.get(*field));
          }
        }
      }
    }
    template <typename TList>
    void UpdateServerSettings(
        const typename capnp::List<TList>::Reader updates) {
      auto message_builder = std::make_unique<capnp::MallocMessageBuilder>();
      auto list = InitSettings(*message_builder);
      UpdateList<TList>(settings_list_, list, updates);
      settings_ = std::move(message_builder);
      settings_list_ = list;
      db_.SaveServerSettings(updates);
    }
    static capnp::List<ServerSetting>::Builder InitSettings(
        capnp::MallocMessageBuilder& message_builder) {
      const auto fields_count =
          capnp::Schema::from<ServerSetting::Setting>().getUnionFields().size();
      return message_builder.initRoot<CollabVmServerMessage>()
          .initMessage()
          .initServerSettings(fields_count);
    }

   private:
    Database& db_;
    std::unique_ptr<capnp::MallocMessageBuilder> settings_;
    capnp::List<ServerSetting>::Builder settings_list_;
  };
  struct VirtualMachine {
    VirtualMachine(const VirtualMachine&) = delete;
    VirtualMachine(capnp::List<VmSetting>::Reader initial_settings) {
      auto fields =
          capnp::Schema::from<VmSetting::Setting>().getUnionFields();
      const auto fields_count = fields.size();
      auto new_list = message_builder_.initRoot<CollabVmServerMessage>()
                          .initMessage()
                          .initReadVmConfigResponse(fields_count);
      for (auto i = 0; i < fields_count; i++) {
        auto changed = false;
        capnp::DynamicStruct::Builder current_setting =
            new_list[i].getSetting();
        for (auto it = initial_settings.begin(); it != initial_settings.end();
             it++) {
          const auto updated_setting = it->getSetting();
          if (updated_setting.which() == i) {
            const capnp::DynamicStruct::Reader reader = updated_setting;
            KJ_IF_MAYBE(field, reader.which()) {
              current_setting.set(*field, reader.get(*field));
              changed = true;
              break;
            }
          }
        }
        if (!changed) {
          current_setting.clear(fields[i]);
        }
      }
      settings_ = new_list;
    }

    VmSetting::Setting::Reader GetSetting(
        const VmSetting::Setting::Which setting) {
      return settings_[setting].getSetting();
    }

    capnp::MallocMessageBuilder& GetMessageBuilder() {
      return message_builder_;
    }

   private:
    capnp::MallocMessageBuilder message_builder_;
    capnp::List<VmSetting>::Builder settings_;
  };

  StrandGuard<ServerSettingsList> settings_;
  StrandGuard<std::unordered_map<std::uint32_t,
                                 std::unique_ptr<capnp::MallocMessageBuilder>>>
      vm_settings_;
  using SessionMap = std::unordered_map<Database::SessionId,
                                        std::shared_ptr<Socket>,
                                        Database::SessionIdHasher>;
  StrandGuard<SessionMap> sessions_;
  StrandGuard<std::unordered_map<std::string, std::shared_ptr<Socket>>> guests_;
  boost::asio::ssl::context ssl_ctx_;
  RecaptchaVerifier recaptcha_;
  StrandGuard<std::unordered_map<
      std::uint32_t,
      std::shared_ptr<CollabVmChannel<
          Socket,
          CollabVmChatRoom<Socket, CollabVm::Shared::max_username_len, max_chat_message_len>>>>>
      channels_;
  StrandGuard<std::unordered_map<std::uint32_t, VirtualMachine>> vms_;
  //  StrandGuard<std::unordered_map<std::uint32_t,
  //  std::shared_ptr<VirtualMachine>>> vms_;
  strand login_strand_;
  StrandGuard<std::vector<std::shared_ptr<Socket>>> viewing_admins_;
  std::uniform_int_distribution<std::uint32_t> guest_rng_;
  std::default_random_engine rng_;
};

}  // namespace CollabVmServer
