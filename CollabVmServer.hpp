#pragma once
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/functional/hash.hpp>
#include <filesystem>
#include <gsl/span>
#include <memory>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <capnp/blob.h>
#include <capnp/dynamic.h>
#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/io.h>
#include <stdio.h>

#include "capnp-list.hpp"
#include "CaseInsensitiveUtils.hpp"
#include "CollabVm.capnp.h"
#include "CollabVmCommon.hpp"
#include "CollabVmChatRoom.hpp"
#include "CollabVmGuacamoleClient.hpp"
#include "SocketMessage.hpp"
#include "Database/Database.h"
#include "GuacamoleClient.hpp"
#include "Recaptcha.hpp"
#include "StrandGuard.hpp"
#include "Totp.hpp"
#include "TurnController.hpp"

namespace CollabVm::Server
{
  template <typename TServer>
  class CollabVmServer final : public TServer
  {
    using Strand = typename TServer::Strand;
    template <typename T>
    using StrandGuard = StrandGuard<Strand, T>;
    using SessionId = Database::SessionId;

  public:
    constexpr static auto max_chat_message_len = 100;
    constexpr static auto global_channel_id = 0;

    template <typename TSocket>
    class CollabVmSocket final
      : public TSocket,
        public TurnController<
          std::shared_ptr<CollabVmSocket<TSocket>>>::UserTurnData
    {
      using SessionMap = std::unordered_map<
        SessionId,
        std::shared_ptr<CollabVmSocket>
        >;

    public:
      struct UserData
      {
        std::string username;
        CollabVmServerMessage::UserType user_type;
        typename TSocket::IpAddress::IpBytes ip_address;

        bool IsAdmin() const {
          return user_type == CollabVmServerMessage::UserType::ADMIN;
        }
      };

      CollabVmSocket(boost::asio::io_context& io_context,
                     const std::filesystem::path& doc_root,
                     CollabVmServer& server)
        : TSocket(io_context, doc_root),
          server_(server),
          send_queue_(io_context),
          chat_rooms_(io_context),
          username_(io_context)
      {
      }

      ~CollabVmSocket() noexcept override { }

      class CollabVmMessageBuffer : public TSocket::MessageBuffer
      {
        capnp::FlatArrayMessageReader reader;
      public:
        CollabVmMessageBuffer() : reader(nullptr) {}
	~CollabVmMessageBuffer() noexcept override { }
        virtual capnp::FlatArrayMessageReader& CreateReader() = 0;

        template<typename TBuffer>
        capnp::FlatArrayMessageReader& CreateReader(TBuffer& buffer) {
          const auto buffer_data = buffer.data();
          const auto array_ptr = kj::ArrayPtr<const capnp::word>(
            static_cast<const capnp::word*>(buffer_data.data()),
            buffer_data.size() / sizeof(capnp::word));
          // TODO: Considering using capnp::ReaderOptions with lower limits
          reader = capnp::FlatArrayMessageReader(array_ptr);
          return reader;
        }
      };

      class CollabVmStaticMessageBuffer final : public CollabVmMessageBuffer
      {
        boost::beast::flat_static_buffer<1024> buffer;
      public:
	~CollabVmStaticMessageBuffer() noexcept override { }
        void StartRead(std::shared_ptr<TSocket>&& socket) override
        {
          socket->ReadWebSocketMessage(std::move(socket),
                                       std::static_pointer_cast<
                                         CollabVmStaticMessageBuffer>(
                                         CollabVmMessageBuffer::
                                         shared_from_this()));
        }
        auto& GetBuffer()
        {
          return buffer;
        }
        capnp::FlatArrayMessageReader& CreateReader() override
        {
          return CollabVmMessageBuffer::CreateReader(buffer);
        }
      };

      class CollabVmDynamicMessageBuffer final : public CollabVmMessageBuffer
      {
        boost::beast::flat_buffer buffer;
      public:
	~CollabVmDynamicMessageBuffer() noexcept override = default;

        void StartRead(std::shared_ptr<TSocket>&& socket) override
        {
          socket->ReadWebSocketMessage(std::move(socket),
                                       std::static_pointer_cast<
                                         CollabVmDynamicMessageBuffer>(
                                         CollabVmMessageBuffer::
                                         shared_from_this()));
        }
        auto& GetBuffer()
        {
          return buffer;
        }
        capnp::FlatArrayMessageReader& CreateReader() override
        {
          return CollabVmMessageBuffer::CreateReader(buffer);
        }
      };

      std::shared_ptr<typename TSocket::MessageBuffer> CreateMessageBuffer() override {
        return is_admin_
                 ? std::static_pointer_cast<typename TSocket::MessageBuffer>(
                   std::make_shared<CollabVmDynamicMessageBuffer>())
                 : std::static_pointer_cast<typename TSocket::MessageBuffer>(
                   std::make_shared<CollabVmStaticMessageBuffer>());
      }

      void OnConnect() override
      {
      }

      void OnMessage(
        std::shared_ptr<typename TSocket::MessageBuffer>&& buffer) override
      {
        try
        {
          HandleMessage(std::move(std::static_pointer_cast<CollabVmMessageBuffer>(buffer)));
        }
        catch (...)
        {
          TSocket::Close();
        }
      }

      void HandleMessage(std::shared_ptr<CollabVmMessageBuffer>&& buffer)
      {
        auto& reader = buffer->CreateReader();
        auto message = reader.template getRoot<CollabVmClientMessage>().getMessage();

        switch (message.which())
        {
        case CollabVmClientMessage::Message::CONNECT_TO_CHANNEL:
        {
          const auto channel_id = message.getConnectToChannel();
          username_.dispatch([
            this, self = shared_from_this(), channel_id]
            (auto& username) {
            auto connect_to_channel = [this, self = shared_from_this(), channel_id](auto& username) {
              auto connect_to_channel =
                [this, self = shared_from_this(), username]
              (auto& channel) mutable
              {
                auto socket_message = SocketMessage::CreateShared();
                auto& message_builder = socket_message->GetMessageBuilder();
                auto connect_result =
                  message_builder.initRoot<CollabVmServerMessage>()
                  .initMessage()
                  .initConnectResponse()
                  .initResult();
                auto connectSuccess = connect_result.initSuccess();
                channel.GetChatRoom().GetChatHistory(connectSuccess);
                connectSuccess.setUsername(username);
                QueueMessage(std::move(socket_message));
                UserData user_data{ username, GetUserType(), TSocket::GetIpAddress().AsBytes() };
                channel.AddUser(user_data, std::move(self));
              };
              if (channel_id == global_channel_id)
              {
                if (is_in_global_chat_)
                {
                  return;
                }
                is_in_global_chat_ = true;
                server_.global_chat_room_.dispatch(std::move(connect_to_channel));
              }
              else
              {
                if (connected_vm_id_)
                {
                  return;
                }
                connected_vm_id_ = channel_id;
                LeaveVmList();
                server_.virtual_machines_.dispatch([
                  this, self = shared_from_this(), channel_id,
                    connect_to_channel = std::move(connect_to_channel)
                ](auto& virtual_machines) mutable
                  { 
                    if (is_viewing_vm_list_) {
                      virtual_machines.RemoveVmListViewer(self);
                    }
                    const auto virtual_machine = virtual_machines.
                      GetAdminVirtualMachine(channel_id);
                    if (!virtual_machine)
                    {
                      auto socket_message = SocketMessage::CreateShared();
                      auto& message_builder = socket_message->GetMessageBuilder();
                      auto connect_result =
                        message_builder.initRoot<CollabVmServerMessage>()
                        .initMessage()
                        .initConnectResponse()
                        .initResult();
                      connect_result.setFail();
                      QueueMessage(std::move(socket_message));
                      return;
                    }
                    virtual_machine->GetUserChannel(
                      std::move(connect_to_channel));
                  });
              }
            };
            if (username.empty())
            {
              GenerateUsername(std::move(connect_to_channel));
            }
            else
            {
              connect_to_channel(username);
            }
          });
          break;
        }
        case CollabVmClientMessage::Message::TURN_REQUEST:
        {
          if (!connected_vm_id_)
          {
            break;
          }
          server_.virtual_machines_.dispatch([
            this, self = shared_from_this()]
            (auto& virtual_machines) mutable
            {
              const auto virtual_machine = virtual_machines.
                GetAdminVirtualMachine(connected_vm_id_);
              if (!virtual_machine)
              {
                return;
              }
              virtual_machine->RequestTurn(std::move(self));
            });
          break;
        }
        case CollabVmClientMessage::Message::GUAC_INSTR:
        {
          if (!connected_vm_id_)
          {
            break;
          }
          server_.virtual_machines_.dispatch([
            this, self = shared_from_this(),
            channel_id = connected_vm_id_, message, buffer = std::move(buffer)]
            (auto& virtual_machines) mutable
            {
              const auto virtual_machine = virtual_machines.
                GetAdminVirtualMachine(channel_id);
              if (!virtual_machine)
              {
                return;
              }
              virtual_machine->ReadInstruction(
                std::move(self),
                [this, self, buffer = std::move(buffer), message]() {
                  return message.getGuacInstr();
                });
            });
          break;
        }
        case CollabVmClientMessage::Message::CHANGE_USERNAME:
          {
            if (is_logged_in_)
            {
              // Registered users can't change their usernames
              break;
            }
            const auto new_username = message.getChangeUsername();
            if (!CollabVm::Shared::ValidateUsername({
              new_username.begin(), new_username.size()
            }))
            {
              break;
            }
            server_.guests_.dispatch([
                this, self = shared_from_this(), buffer = std::move(
                  buffer),
                new_username
              ](auto& guests)
              {
                auto guests_it = guests.find(new_username);
                auto socket_message = SocketMessage::CreateShared();
                auto message = socket_message->GetMessageBuilder()
                                             .initRoot<
                                               CollabVmServerMessage
                                             >()
                                             .initMessage();
                if (guests_it != guests.end())
                {
                  message.setUsernameTaken();
                  QueueMessage(std::move(socket_message));
                  return;
                }
                SetUsername(std::string(new_username));
              });
            break;
          }
        case CollabVmClientMessage::Message::CHANGE_PASSWORD_REQUEST:
          if (!is_logged_in_)
          {
            break;
          }
          username_.dispatch(
            [this, self = shared_from_this(),
            buffer = std::move(buffer), message]
            (auto& username) {
              auto lambda = [this, self = shared_from_this(),
                buffer = std::move(buffer), message, username]()
              {
                const auto change_password_request =
                  message.getChangePasswordRequest();
                const auto success = server_.db_.ChangePassword(
                  username,
                  change_password_request.getOldPassword(),
                  change_password_request.getNewPassword());
                auto socket_message = SocketMessage::CreateShared();
                socket_message->GetMessageBuilder()
                  .initRoot<CollabVmServerMessage>()
                  .initMessage().setChangePasswordResponse(success);
                QueueMessage(std::move(socket_message));
              };
              server_.login_strand_.post(
                std::move(lambda),
                std::allocator<decltype(lambda)>());
            });
          break;
        case CollabVmClientMessage::Message::CHAT_MESSAGE:
        {
          const auto chat_message = message.getChatMessage();
          const auto message_len = chat_message.getMessage().size();
          if (!message_len || message_len > max_chat_message_len)
          {
            break;
          }
          username_.dispatch(
            [this, self = shared_from_this(),
            buffer = std::move(buffer), chat_message]
          (auto& username)
          {
            if (username.empty())
            {
              return;
            }
            const auto destination =
              chat_message.getDestination().getDestination();
            switch (destination.which())
            {
            case CollabVmClientMessage::ChatMessageDestination::Destination::
            NEW_DIRECT:
              server_.guests_.dispatch([
                  this, self = shared_from_this(), buffer = std::move(buffer),
                  chat_message, username = destination.getNewDirect()
                ](auto& guests) mutable
                {
                  auto guests_it = guests.find(username);
                  if (guests_it == guests.end())
                  {
                    SendChatMessageResponse(
                      CollabVmServerMessage::ChatMessageResponse::
                      USER_NOT_FOUND);
                    return;
                  }
                  chat_rooms_.dispatch([
                      this, self = shared_from_this(), buffer = std::
                      move(buffer),
                      chat_message, recipient = guests_it->second
                    ](auto& chat_rooms)
                    {
                      if (chat_rooms.size() >= 10)
                      {
                        SendChatMessageResponse(
                          CollabVmServerMessage::ChatMessageResponse::
                          USER_CHAT_LIMIT);
                        return;
                      }
                      auto existing_chat_room =
                        std::find_if(chat_rooms.begin(),
                                     chat_rooms.end(),
                                     [&recipient](const auto& room)
                                     {
                                       return room.second.first ==
                                         recipient;
                                     });
                      if (existing_chat_room != chat_rooms.end())
                      {
                        SendChatChannelId(
                          existing_chat_room->second.second);
                        return;
                      }
                      const auto id = chat_rooms_id_++;
                      chat_rooms.emplace(
                        id, std::make_pair(recipient, 0));
                      recipient->chat_rooms_.dispatch([
                          this, self = shared_from_this(),
                          buffer = std::move(buffer),
                          chat_message, recipient, sender_id
                          = id
                        ](auto& recipient_chat_rooms)
                        {
                          auto existing_chat_room = std::
                            find_if(
                              recipient_chat_rooms.begin(),
                              recipient_chat_rooms.end(),
                              [&self](const auto& room)
                              {
                                return room.second.first ==
                                  self;
                              });
                          if (existing_chat_room !=
                            recipient_chat_rooms.end())
                          {
                            if (!existing_chat_room
                                 ->second.second)
                            {
                              existing_chat_room
                                ->second.second = sender_id;
                              return;
                            }
                            SendChatChannelId(sender_id);
                            return;
                          }
                          if (recipient_chat_rooms.size() >=
                            10)
                          {
                            chat_rooms_.dispatch([
                                this, self =
                                shared_from_this(), sender_id
                              ](auto& chat_rooms)
                              {
                                chat_rooms.erase(sender_id);
                                SendChatMessageResponse(
                                  CollabVmServerMessage::
                                  ChatMessageResponse::
                                  RECIPIENT_CHAT_LIMIT);
                              });
                            return;
                          }
                          const auto recipient_id = recipient
                            ->chat_rooms_id_++;
                          recipient_chat_rooms.emplace(
                            recipient_id,
                            std::make_pair(
                              recipient, sender_id));
                          chat_rooms_.dispatch([
                              this, self = shared_from_this()
                              ,
                              buffer = std::move(buffer),
                              chat_message, recipient,
                              sender_id, recipient_id
                            ](auto& chat_rooms)
                            {
                              auto chat_rooms_it = chat_rooms
                                .find(sender_id);
                              if (chat_rooms_it != chat_rooms
                                .end() &&
                                !chat_rooms_it->second.second
                              )
                              {
                                chat_rooms_it->second.second
                                  = recipient_id;
                                SendChatChannelId(sender_id);

                                auto socket_message =
                                  SocketMessage::CreateShared();
                                auto channel_message =
                                  socket_message
                                  ->GetMessageBuilder()
                                  .initRoot<
                                    CollabVmServerMessage>()
                                  .initMessage()
                                  .initNewChatChannel();
                                channel_message.setChannel(
                                  recipient_id);
                                auto message =
                                  channel_message.
                                  initMessage();
                                message.setMessage(
                                  chat_message.getMessage());
                                //                        message.setSender(username);
                                //    message.setTimestamp(timestamp);
                                recipient->QueueMessage(socket_message);
                                QueueMessage(std::move(socket_message));
                              }
                            });
                        });
                    });
                });
              break;
            case CollabVmClientMessage::ChatMessageDestination::Destination::
            DIRECT:
              {
                chat_rooms_.dispatch([
                    this, self = shared_from_this(), username,
                    buffer = std::move(buffer), chat_message, destination
                  ](const auto& chat_rooms) mutable
                  {
                    const auto id = destination.getDirect();
                    const auto chat_rooms_it = chat_rooms.find(id);
                    if (chat_rooms_it == chat_rooms.end())
                    {
                      // TODO: Tell the client the message could not be sent
                      return;
                    }
                    const auto recipient = chat_rooms_it->second.first;
                    recipient->QueueMessage(CreateChatMessage(
                      id, username, chat_message.getMessage()));
                  });
                break;
              }
            case CollabVmClientMessage::ChatMessageDestination::Destination::VM:
              {
                const auto id = destination.getVm();
                auto send_message = [
                    this, self = shared_from_this(), username,
                    buffer = std::move(buffer), chat_message
                  ](auto& channel)
                {
                  auto& chat_room = channel.GetChatRoom();
                  auto new_chat_message = SocketMessage::CreateShared();
                  auto chat_room_message =
                    new_chat_message->GetMessageBuilder()
                                    .initRoot<CollabVmServerMessage>()
                                    .initMessage()
                                    .initChatMessage();
                  chat_room.AddUserMessage(chat_room_message, username,
                                           chat_message.getMessage());
                  channel.BroadcastMessage(std::move(new_chat_message));
                };
                if (id == global_channel_id)
                {
                  server_.global_chat_room_.dispatch(std::move(send_message));
                  break;
                }
              server_.virtual_machines_.dispatch([
                  id, send_message = std::move(send_message)
              ](auto& virtual_machines)
                {
                  const auto virtual_machine = virtual_machines.
                    GetAdminVirtualMachine(id);
                  if (!virtual_machine)
                  {
                    return;
                  }
                  virtual_machine->GetUserChannel(
                    std::move(send_message));
                });
              break;
            }
            default:
              break;
            }
          });
        }
        break;
        case CollabVmClientMessage::Message::VM_LIST_REQUEST:
          {
            server_.virtual_machines_.dispatch(
              [this, self = shared_from_this()](auto& virtual_machines) mutable
              {
                if (!is_viewing_vm_list_)
                {
                  is_viewing_vm_list_ = true;
                  virtual_machines.AddVmListViewer(std::move(self));
                }
              });
            break;
          }
        case CollabVmClientMessage::Message::LOGIN_REQUEST:
          {
            auto login_request = message.getLoginRequest();
            const auto username = login_request.getUsername();
            const auto password = login_request.getPassword();
            const auto captcha_token = login_request.getCaptchaToken();
            server_.recaptcha_.Verify(
              captcha_token,
              [
                this, self = shared_from_this(),
                buffer = std::move(buffer), username, password
              ](bool is_valid) mutable
              {
                auto socket_message = SocketMessage::CreateShared();
                auto& message_builder = socket_message->
                  GetMessageBuilder();
                auto login_response =
                  message_builder
                  .initRoot<CollabVmServerMessage::Message>()
                  .initLoginResponse()
                  .initResult();
                if (is_valid)
                {
                  auto lambda = [
                      this, self = std::move(self), socket_message,
                      buffer = std::move(buffer), login_response,
                      username,
                      password
                    ]() mutable
                  {
                    const auto [login_result, totp_key] =
                      server_.db_.Login(username, password);
                    if (login_result == CollabVmServerMessage::
                      LoginResponse::
                      LoginResult::SUCCESS)
                    {
                      server_.CreateSession(
                        shared_from_this(), username,
                        [
                          this, self = std::move(self), socket_message,
                          login_response
                        ](const std::string& username,
                          const SessionId& session_id) mutable
                        {
                          auto session = login_response.initSession();
                          session.setSessionId(capnp::Data::Reader(
                            reinterpret_cast<const kj::byte*>(session_id.data()),
                            session_id.size()));
                          session.setUsername(username);
                          QueueMessage(std::move(socket_message));
                        });
                    }
                    else
                    {
                      if (login_result ==
                        CollabVmServerMessage::LoginResponse::
                        LoginResult::
                        TWO_FACTOR_REQUIRED)
                      {
                        totp_key_ = std::move(totp_key);
                      }
                      login_response.setResult(login_result);
                      QueueMessage(std::move(socket_message));
                    }
                  };
                  server_.login_strand_.post(
                    std::move(lambda),
                    std::allocator<decltype(lambda)>());
                }
                else
                {
                  login_response.setResult(
                    CollabVmServerMessage::LoginResponse::LoginResult::
                    INVALID_CAPTCHA_TOKEN);
                  QueueMessage(std::move(socket_message));
                }
              },
              TSocket::GetIpAddress().AsString());
            break;
          }
        case CollabVmClientMessage::Message::TWO_FACTOR_RESPONSE:
          {
            Totp::ValidateTotp(message.getTwoFactorResponse(),
                               gsl::as_bytes(gsl::make_span(&totp_key_.front(),
                                                            totp_key_.size())));
            break;
          }
        case CollabVmClientMessage::Message::ACCOUNT_REGISTRATION_REQUEST:
          {
            server_.settings_.dispatch([
                this, self = TSocket::shared_from_this(),
                buffer = std::move(buffer), message
              ](auto& settings) mutable
              {
                if (!settings
                     .GetServerSetting(
                       ServerSetting::Setting::
                       ALLOW_ACCOUNT_REGISTRATION)
                     .getAllowAccountRegistration())
                {
                  return;
                }
                auto response = SocketMessage::CreateShared();
                auto& message_builder = response->GetMessageBuilder();
                auto registration_result = message_builder.initRoot<
                  CollabVmServerMessage::Message>()
                  .initAccountRegistrationResponse().initResult();

                auto register_request = message.
                  getAccountRegistrationRequest();
                auto username = register_request.getUsername();
                if (!Shared::ValidateUsername({
                  username.begin(), username.size()
                }))
                {
                  registration_result.setErrorStatus(
                    CollabVmServerMessage::RegisterAccountResponse::
                    RegisterAccountError::USERNAME_INVALID);
                  QueueMessage(std::move(response));
                  return;
                }
                if (register_request.getPassword().size() >
                  Database::max_password_len)
                {
                  registration_result.setErrorStatus(
                    CollabVmServerMessage::RegisterAccountResponse::
                    RegisterAccountError::PASSWORD_INVALID);
                  QueueMessage(std::move(response));
                  return;
                }
                std::optional<
                  gsl::span<const std::byte, Database::User::totp_key_len>>
                  totp_key;
                if (register_request.hasTwoFactorToken())
                {
                  auto two_factor_token = register_request.
                    getTwoFactorToken();
                  if (two_factor_token.size() !=
                    Database::User::totp_key_len)
                  {
                    registration_result.setErrorStatus(
                      CollabVmServerMessage::RegisterAccountResponse::
                      RegisterAccountError::TOTP_ERROR);
                    QueueMessage(std::move(response));
                    return;
                  }
                  totp_key =
                    gsl::as_bytes(gsl::make_span(
                      reinterpret_cast<const capnp::byte(&)
                      [Database::User::totp_key_len]>(
                        *two_factor_token.begin())));
                }
                const auto register_result = server_.db_.CreateAccount(
                  username,
                  register_request.
                  getPassword(),
                  totp_key,
                  TSocket::
                  GetIpAddress().
                  AsVector());
                if (register_result !=
                  CollabVmServerMessage::RegisterAccountResponse::
                  RegisterAccountError::SUCCESS)
                {
                  registration_result.setErrorStatus(register_result);
                  QueueMessage(std::move(response));
                  return;
                }
                server_.CreateSession(
                  shared_from_this(), username,
                  [
                    this, self = shared_from_this(), buffer = std::move(
                      buffer),
                    response, registration_result
                  ](const std::string& username,
                    const SessionId& session_id) mutable
                  {
                    auto session = registration_result.initSession();
                    session.setSessionId(capnp::Data::Reader(
                      reinterpret_cast<const kj::byte*>(session_id.data()),
                      session_id.size()));
                    session.setUsername(username);
                    QueueMessage(std::move(response));
                  });
              });
            break;
          }
        case CollabVmClientMessage::Message::SERVER_CONFIG_REQUEST:
          {
            if (!is_admin_)
            {
              break;
            }
            server_.settings_.dispatch(
              [ this, self = shared_from_this() ](auto& settings)
              {
                QueueMessage(SocketMessage::CopyFromMessageBuilder(
                  settings.GetServerSettingsMessageBuilder()));
              });
            if (!is_viewing_server_config)
            {
              is_viewing_server_config = true;
              server_.virtual_machines_.dispatch([self = shared_from_this()]
                (auto& virtual_machines) mutable
                {
                  virtual_machines.AddAdminVmListViewer(std::move(self));
                });
            }
            break;
          }
        case CollabVmClientMessage::Message::SERVER_CONFIG_MODIFICATIONS:
        {
          if (!is_admin_)
          {
            break;
          }
          auto changed_settings = message.getServerConfigModifications();
          for (const auto& setting_message : changed_settings)
          {
            // TODO: Validate setttings
          }
          server_.settings_.dispatch([
            this, self = shared_from_this(), buffer = std::move(
              buffer),
              changed_settings
          ](auto& settings) mutable
            {
              settings.UpdateServerSettings(
                changed_settings);
              auto config_message = SocketMessage::CopyFromMessageBuilder(
                settings.GetServerSettingsMessageBuilder());
              // Broadcast the config changes to all other admins viewing the
              // admin panel
              server_.virtual_machines_.dispatch([
                self = std::move(self),
                config_message = std::move(config_message)
                ]
                (auto& virtual_machines)
                {
                  virtual_machines
                    .BroadcastToViewingAdminsExcluding(config_message, self);
                });
            });
        }
          break;
        case CollabVmClientMessage::Message::SERVER_CONFIG_HIDDEN:
          LeaveServerConfig();
          break;
        case CollabVmClientMessage::Message::CREATE_VM:
          if (!is_admin_)
          {
            break;
          }
          server_.virtual_machines_.dispatch([
              this, self = shared_from_this(),
                buffer = std::move(buffer), message](auto& virtual_machines)
            {
              const auto vm_id = server_.db_.GetNewVmId();
              const auto initial_settings = message.getCreateVm();
              auto& virtual_machine =
                virtual_machines.AddAdminVirtualMachine(
                  server_.GetContext(), vm_id, initial_settings);
              virtual_machine.GetSettings(
                [this, self = std::move(self), vm_id](auto& settings)
                {
                  server_.db_.CreateVm(vm_id, settings.settings_);
                });

              auto socket_message = SocketMessage::CreateShared();
              socket_message->GetMessageBuilder()
                .initRoot<CollabVmServerMessage>().initMessage()
                .setCreateVmResponse(vm_id);
              QueueMessage(socket_message);
              virtual_machines.SendAdminVmList(*this);
            });
          break;
        case CollabVmClientMessage::Message::READ_VMS:
          if (is_admin_)
          {
            server_.virtual_machines_.dispatch([ this,
                self = shared_from_this() ](auto& virtual_machines)
              {
                virtual_machines.SendAdminVmList(*this);
              });
          }
          break;
        case CollabVmClientMessage::Message::READ_VM_CONFIG:
          if (is_admin_)
          {
            const auto vm_id = message.getReadVmConfig();
            server_.virtual_machines_.dispatch([this, self = shared_from_this(),
                vm_id](auto& virtual_machines)
              {
                const auto admin_virtual_machine = virtual_machines
                  .GetAdminVirtualMachine(vm_id);
                if (!admin_virtual_machine)
                {
                  // TODO: Indicate error
                  return;
                }
                admin_virtual_machine->GetSettingsMessage(
                  [this, self = std::move(self)](auto& settings)
                  {
                    QueueMessage(
                      SocketMessage::CopyFromMessageBuilder(settings));
                  });
              });
          }
          break;
        case CollabVmClientMessage::Message::UPDATE_VM_CONFIG:
          if (!is_admin_)
          {
            break;
          }
          server_.virtual_machines_.dispatch(
            [this, self = shared_from_this(), buffer = std::move(buffer),
              message](auto& virtual_machines)
            {
              const auto modified_vm = message.getUpdateVmConfig();
              const auto vm_id = modified_vm.getId();
              const auto virtual_machine =
                virtual_machines.GetAdminVirtualMachine(vm_id);
              if (!virtual_machine)
              {
                return;
              }
              const auto modified_settings = modified_vm.
                getModifications();
              virtual_machine->UpdateSettings(
                server_.db_,
                [buffer = std::move(buffer), modified_settings]() {
                  return modified_settings;
                },
                server_.virtual_machines_.wrap(
                  [self = std::move(self), vm_id]
                  (auto& virtual_machines, auto is_valid_settings) mutable {
                    if (!is_valid_settings) {
                      // TODO: Indicate error
                      return;
                    }
                    const auto virtual_machine =
                      virtual_machines.GetAdminVirtualMachine(vm_id);
                    if (!virtual_machine)
                    {
                      return;
                    }
                    virtual_machines.UpdateVirtualMachineInfo(*virtual_machine);
                  }));
            });
          break;
        case CollabVmClientMessage::Message::DELETE_VM:
          if (!is_admin_)
          {
            break;
          }
          server_.virtual_machines_.dispatch([this, self = shared_from_this(),
              vm_id = message.getDeleteVm()](auto& virtual_machines)
            {
              const auto admin_virtual_machine = virtual_machines
                .RemoveAdminVirtualMachine(vm_id);
              if (!admin_virtual_machine)
              {
                // TODO: Indicate error
                return;
              }
              server_.db_.DeleteVm(vm_id);
              virtual_machines.SendAdminVmList(*this);
            });
          break;
        case CollabVmClientMessage::Message::START_VMS:
          if (!is_admin_)
          {
            break;
          }
          server_.virtual_machines_.dispatch(
            [this, self = shared_from_this(), buffer = std::move(buffer),
              message](auto& virtual_machines)
          {
            for (auto vm_id : message.getStartVms())
            {
              const auto virtual_machine =
                virtual_machines.GetAdminVirtualMachine(vm_id);
              if (!virtual_machine)
              {
                // TODO: Indicate error
                return;
              }
              virtual_machine->Start();
            }
          });
          break;
        case CollabVmClientMessage::Message::STOP_VMS:
          if (!is_admin_)
          {
            break;
          }
          server_.virtual_machines_.dispatch(
            [this, self = shared_from_this(), buffer = std::move(buffer),
              message](auto& virtual_machines)
          {
            for (auto vm_id : message.getStopVms())
            {
              const auto virtual_machine =
                virtual_machines.GetAdminVirtualMachine(vm_id);
              if (!virtual_machine)
              {
                // TODO: Indicate error
                return;
              }
              virtual_machine->Stop();
            }
          });
          break;
        case CollabVmClientMessage::Message::RESTART_VMS:
          if (!is_admin_)
          {
            break;
          }
          server_.virtual_machines_.dispatch(
            [this, self = shared_from_this(), buffer = std::move(buffer),
              message](auto& virtual_machines)
          {
            for (auto vm_id : message.getRestartVms())
            {
              const auto virtual_machine =
                virtual_machines.GetAdminVirtualMachine(vm_id);
              if (!virtual_machine)
              {
                // TODO: Indicate error
                return;
              }
              virtual_machine->Restart();
            }
          });
          break;
        case CollabVmClientMessage::Message::CREATE_INVITE:
          if (is_admin_)
          {
            auto invite = message.getCreateInvite();
            auto socket_message = SocketMessage::CreateShared();
            auto invite_result = socket_message->GetMessageBuilder()
                                               .initRoot<CollabVmServerMessage
                                               >()
                                               .initMessage();
            if (const auto id = server_.db_.CreateInvite(
              invite.getInviteName(),
              invite.getUsername(),
              invite.getUsernameReserved(),
              invite.getAdmin()))
            {
              invite_result.setCreateInviteResult(
                capnp::Data::Reader(
                  reinterpret_cast<const kj::byte*>(id->data()), id->size()));
            }
            else
            {
              invite_result.initCreateInviteResult(0);
            }
            QueueMessage(std::move(socket_message));
          }
          break;
        case CollabVmClientMessage::Message::READ_INVITES:
          if (is_admin_)
          {
            auto socket_message = SocketMessage::CreateShared();
            auto response = socket_message->GetMessageBuilder()
                                          .initRoot<CollabVmServerMessage>()
                                          .initMessage();
            auto invites_list_it =
              response.initReadInvitesResponse(
                server_.db_.GetInvitesCount()).begin();
            server_.db_.ReadInvites([&invites_list_it](auto&& invite)
            {
              invites_list_it->setId(capnp::Data::Reader(
                reinterpret_cast<const capnp::byte*>(invite.id.data()),
                invite.id.size()));
              invites_list_it->setInviteName(invite.name);
              ++invites_list_it;
            });
            QueueMessage(std::move(socket_message));
          }
          break;
        case CollabVmClientMessage::Message::UPDATE_INVITE:
          if (is_admin_)
          {
            const auto invite = message.getUpdateInvite();
            auto socket_message = SocketMessage::CreateShared();
            const auto invite_id = message.getDeleteInvite().asChars();
            const auto result = server_.db_.UpdateInvite(
              std::string(
                invite_id.begin(),
                invite_id.size()),
              invite.getUsername(),
              invite.getAdmin());
            socket_message->GetMessageBuilder()
                          .initRoot<CollabVmServerMessage>()
                          .initMessage()
                          .setUpdateInviteResult(result);
            QueueMessage(std::move(socket_message));
          }
          break;
        case CollabVmClientMessage::Message::DELETE_INVITE:
          if (is_admin_)
          {
            const auto invite_id = message.getDeleteInvite().asChars();
            server_.db_.DeleteInvite(
              std::string(invite_id.begin(), invite_id.size()));
          }
          break;
        case CollabVmClientMessage::Message::CREATE_RESERVED_USERNAME:
          if (is_admin_)
          {
            server_.db_.CreateReservedUsername(
              message.getCreateReservedUsername());
          }
          break;
        case CollabVmClientMessage::Message::READ_RESERVED_USERNAMES:
          if (is_admin_)
          {
            auto socket_message = SocketMessage::CreateShared();
            auto response = socket_message->GetMessageBuilder()
                                          .initRoot<CollabVmServerMessage>()
                                          .initMessage();
            auto usernames_list_it =
              response.initReadReservedUsernamesResponse(
                server_.db_.GetReservedUsernamesCount()).begin();
            server_.db_.ReadReservedUsernames(
              [&usernames_list_it](auto username)
              {
                *usernames_list_it = username.data();
              });
            QueueMessage(std::move(socket_message));
          }
          break;
        case CollabVmClientMessage::Message::DELETE_RESERVED_USERNAME:
          if (is_admin_)
          {
            server_.db_.DeleteReservedUsername(
              message.getDeleteReservedUsername());
          }
          break;
        case CollabVmClientMessage::Message::BAN_IP:
        {
          if (!is_admin_)
          {
            break;
          }
          auto ip_bytes = boost::asio::ip::address_v6::bytes_type();
          *reinterpret_cast<std::uint64_t*>(&ip_bytes[0]) =
            boost::endian::big_to_native(message.getBanIp().getFirst());
          *reinterpret_cast<std::uint64_t*>(&ip_bytes[8]) =
            boost::endian::big_to_native(message.getBanIp().getSecond());
          server_.settings_.dispatch([
            ip_address = boost::asio::ip::address_v6(ip_bytes).to_string()]
            (auto& settings)
            {
              const auto ban_ip_command = 
                settings.GetServerSetting(ServerSetting::Setting::BAN_IP_COMMAND)
                        .getBanIpCommand();
              if (ban_ip_command.size()) {
#ifdef _WIN32
  #define putenv _putenv
#endif
                putenv(("IP_ADDRESS=" + ip_address).data());
#ifdef _WIN32
  #undef putenv
#endif
                ExecuteCommandAsync(ban_ip_command.cStr());
              }
            });
          break;
        }
        case CollabVmClientMessage::Message::PAUSE_TURN_TIMER:
        {
          if (is_admin_ && connected_vm_id_)
          {
            server_.virtual_machines_.dispatch(
              [vm_id = connected_vm_id_](auto& virtual_machines)
              {
                if (const auto virtual_machine =
                      virtual_machines.GetAdminVirtualMachine(vm_id);
                    virtual_machine)
                {
                  virtual_machine->PauseTurnTimer();
                }
              });
          }
          break;
        }
        case CollabVmClientMessage::Message::RESUME_TURN_TIMER:
        {
          if (is_admin_ && connected_vm_id_)
          {
            server_.virtual_machines_.dispatch(
              [vm_id = connected_vm_id_](auto& virtual_machines)
              {
                if (const auto virtual_machine =
                      virtual_machines.GetAdminVirtualMachine(vm_id);
                    virtual_machine)
                {
                  virtual_machine->ResumeTurnTimer();
                }
              });
          }
          break;
        }
        case CollabVmClientMessage::Message::END_TURN:
        {
          if (connected_vm_id_)
          {
            server_.virtual_machines_.dispatch(
              [this, self = shared_from_this(),
                vm_id = connected_vm_id_](auto& virtual_machines) mutable
              {
                if (const auto virtual_machine =
                      virtual_machines.GetAdminVirtualMachine(vm_id);
                    virtual_machine)
                {
                  virtual_machine->EndCurrentTurn(std::move(self));
                }
              });
          }
          break;
        }
        default:
          TSocket::Close();
        }
      }

    private:
      CollabVmServerMessage::UserType GetUserType()
      {
        if (is_admin_) {
          return CollabVmServerMessage::UserType::ADMIN;
        }
        if (is_logged_in_) {
          return CollabVmServerMessage::UserType::REGULAR;
        }
        return CollabVmServerMessage::UserType::GUEST;
      }

      void SendChatChannelId(const std::uint32_t id)
      {
        auto socket_message = SocketMessage::CreateShared();
        auto& message_builder = socket_message->GetMessageBuilder();
        auto message = message_builder.initRoot<CollabVmServerMessage>()
                                      .initMessage()
                                      .initChatMessage();
        message.setChannel(id);
        QueueMessage(std::move(socket_message));
      }

      void SendChatMessageResponse(
        CollabVmServerMessage::ChatMessageResponse result)
      {
        auto socket_message = SocketMessage::CreateShared();
        socket_message->GetMessageBuilder()
                      .initRoot<CollabVmServerMessage>()
                      .initMessage()
                      .setChatMessageResponse(result);
        QueueMessage(std::move(socket_message));
      }

      bool ValidateVmSetting(std::uint16_t setting_id,
                             const VmSetting::Setting::Reader& setting)
      {
        switch (setting_id)
        {
        case VmSetting::Setting::TURN_TIME:
          return setting.getTurnTime() > 0;
        case VmSetting::Setting::DESCRIPTION:
          return setting.getDescription().size() <= 200;
        default:
          return true;
        }
      }

      SessionId SetSessionId(SessionMap& sessions, SessionId&& session_id)
      {
        const auto [session_id_pair, inserted_new] =
          sessions.emplace(std::move(session_id), shared_from_this());
        assert(inserted_new);
        return session_id_pair->first;
      }

      std::shared_ptr<CollabVmSocket> shared_from_this()
      {
        return std::static_pointer_cast<
          CollabVmSocket<typename TServer::TSocket>>(
          TSocket::shared_from_this());
      }

      void SendMessage(std::shared_ptr<CollabVmSocket>&& self,
                       std::shared_ptr<SocketMessage>&& socket_message)
      {
        const auto& segment_buffers = socket_message->
          GetBuffers();
        TSocket::WriteMessage(
          segment_buffers,
          send_queue_.wrap([ this, self = std::move(self), socket_message ](
            auto& send_queue, const auto error_code,
            std::size_t bytes_transferred) mutable
            {
              SendMessageCallback(
                std::move(self), send_queue, error_code, bytes_transferred);
            }));
      }

      void SendMessageBatch(std::shared_ptr<CollabVmSocket>&& self,
                       std::queue<std::shared_ptr<SocketMessage>>& queue)
      {
        auto socket_messages = std::vector<std::shared_ptr<SocketMessage>>();
        socket_messages.reserve(queue.size());
        auto segment_buffers = std::vector<boost::asio::const_buffer>();
        segment_buffers.reserve(queue.size());
        do
        {
          auto& socket_message = *socket_messages.emplace_back(std::move(queue.front()));
          const auto& buffers = socket_message.GetBuffers();
          std::copy(buffers.begin(), buffers.end(), std::back_inserter(segment_buffers));
          queue.pop();
        } while (!queue.empty());

        TSocket::WriteMessage(
          std::move(segment_buffers),
          send_queue_.wrap(
            [ this, self = std::move(self),
            socket_messages = std::move(socket_messages) ](
            auto& send_queue, const auto error_code,
            std::size_t bytes_transferred) mutable
            {
              SendMessageCallback(
                std::move(self), send_queue, error_code, bytes_transferred);
            }));
      }

      void SendMessageCallback(
        std::shared_ptr<CollabVmSocket>&& self,
        std::queue<std::shared_ptr<SocketMessage>>& send_queue,
        const boost::system::error_code error_code,
        std::size_t bytes_transferred)
      {
        if (error_code)
        {
          TSocket::Close();
          return;
        }
        switch (send_queue.size())
        {
        case 0:
          sending_ = false;
          return;
        case 1:
          SendMessage(std::move(self), std::move(send_queue.front()));
          send_queue.pop();
          return;
        default:
          SendMessageBatch(std::move(self), send_queue);
        }
      }
    public:
      template<typename TMessage>
      void QueueMessage(TMessage&& socket_message)
      {
        static_assert(std::is_convertible_v<TMessage, std::shared_ptr<SocketMessage>>);
        socket_message->CreateFrame();
        send_queue_.dispatch([
            this, self = shared_from_this(),
            socket_message =
              std::forward<TMessage>(socket_message)
          ](auto& send_queue) mutable
          {
            if (sending_)
            {
              send_queue.push(std::move(socket_message));
            }
            else
            {
              sending_ = true;
              SendMessage(std::move(self), std::move(socket_message));
            }
          });
      }
      template<typename TCallback>
      void QueueMessageBatch(TCallback&& callback)
      {
        send_queue_.dispatch([
            this, self = shared_from_this(),
            callback = std::forward<TCallback>(callback)
          ](auto& send_queue) mutable
          {
            callback([&send_queue](auto&& socket_message)
            {
              socket_message->CreateFrame();
              send_queue.push(std::forward<decltype(socket_message)>(socket_message));
            });
            if (!send_queue.empty() && !sending_)
            {
              sending_ = true;
              SendMessageBatch(std::move(self), send_queue);
            }
          });
      }
    private:
      void OnDisconnect() override {
        LeaveServerConfig();
        LeaveVmList();
        auto leave_channel =
          [self = shared_from_this()]
          (auto& channel) {
            channel.RemoveUser(std::move(self));
          };
        if (connected_vm_id_) {
          server_.virtual_machines_.dispatch([
            id = connected_vm_id_, leave_channel]
            (auto& virtual_machines)
            {
              const auto virtual_machine = virtual_machines.
                GetAdminVirtualMachine(id);
              if (!virtual_machine)
              {
                return;
              }
              virtual_machine->GetUserChannel(std::move(leave_channel));
            });
        }
        if (is_in_global_chat_) {
          server_.global_chat_room_.dispatch(std::move(leave_channel));
        }
      }

      void LeaveServerConfig()
      {
        if (!is_viewing_server_config)
        {
          return;
        }
        is_viewing_server_config = false;
        server_.virtual_machines_.dispatch([self = shared_from_this()]
          (auto& virtual_machines)
          {
            virtual_machines.RemoveAdminVmListViewer(std::move(self));
          });
      }

      void LeaveVmList()
      {
        if (!is_viewing_vm_list_)
        {
          return;
        }
        is_viewing_vm_list_ = false;
        server_.virtual_machines_.dispatch([self = shared_from_this()]
          (auto& virtual_machines)
          {
            virtual_machines.RemoveVmListViewer(std::move(self));
          });
      }

      void InvalidateSession()
      {
        // TODO:
      }

      static std::shared_ptr<SharedSocketMessage> CreateChatMessage(
        const std::uint32_t channel_id,
        const capnp::Text::Reader sender,
        const capnp::Text::Reader message)
      {
        const auto timestamp =
          std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch())
          .count();
        auto socket_message = SocketMessage::CreateShared();
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

      template<typename TContinuation>
      void GenerateUsername(TContinuation&& continuation)
      {
        server_.guests_.dispatch([
            this, continuation = std::forward<TContinuation>(continuation)
        ](auto& guests)
        {
          auto num = server_.guest_rng_(server_.rng_);
          auto username = std::string();
          // Increment the number until a username is found that is not taken
          auto is_username_taken = false;
          do
          {
            if (is_username_taken) {
              num++;
            }
            username = "guest" + std::to_string(num);
            is_username_taken =
              !std::get<bool>(guests.insert({ username, shared_from_this() }));
          } while (is_username_taken);
          SetUsername(username);
          continuation(username);
        });
      }

      template<typename TString>
      void SetUsername(TString&& username) {
        static_assert(std::is_convertible_v<TString, std::string>);
        username_.dispatch(
          [this, self = shared_from_this(), username = std::forward<TString>(username)]
          (auto& current_username) mutable {
            std::swap(current_username, username);
            if (!username.empty() && (connected_vm_id_ || is_in_global_chat_))
            {
              auto update_username = 
                [this, self = shared_from_this(), new_username=current_username]
                (auto& channel) mutable {
                  auto user_data = channel.GetUserData(self);
                  if (!user_data.has_value()) {
                    return;
                  }
                  auto& current_username = user_data.value().get().username;
                  auto message = SocketMessage::CreateShared();
                  auto username_change = message->GetMessageBuilder()
                                                .initRoot<
                                                  CollabVmServerMessage>()
                                                .initMessage()
                                                .initChangeUsername();
                  username_change.setOldUsername(current_username);
                  username_change.setNewUsername(new_username);

                  current_username = std::move(new_username);

                  channel.BroadcastMessage(std::move(message));
                };
              if (connected_vm_id_) {
                server_.virtual_machines_.dispatch([
                  id = connected_vm_id_, update_username]
                  (auto& virtual_machines)
                  {
                    const auto virtual_machine = virtual_machines.
                      GetAdminVirtualMachine(id);
                    if (!virtual_machine)
                    {
                      return;
                    }
                    virtual_machine->GetUserChannel(std::move(update_username));
                  });
              }
              if (is_in_global_chat_) {
                server_.global_chat_room_.dispatch(std::move(update_username));
              }
            }
          });
      }

      CollabVmServer& server_;
      StrandGuard<std::queue<std::shared_ptr<SocketMessage>>> send_queue_;
      bool sending_ = false;
      StrandGuard<std::unordered_map<
        std::uint32_t,
        std::pair<std::shared_ptr<CollabVmSocket>, std::uint32_t>>>
        chat_rooms_;
      std::uint32_t chat_rooms_id_ = 1;

      std::vector<std::byte> totp_key_;
      bool is_logged_in_ = false;
      bool is_admin_ = false;
      bool is_viewing_server_config = false;
      bool is_viewing_vm_list_ = false;
      bool is_in_global_chat_ = false;
      std::uint32_t connected_vm_id_ = 0;
      StrandGuard<std::string> username_;
      friend class CollabVmServer;
    };

    using TServer::io_context_;

    CollabVmServer(const std::string& doc_root, const std::uint8_t threads)
      : TServer(doc_root, threads),
        settings_(io_context_, db_),
        sessions_(io_context_),
        guests_(io_context_),
        ssl_ctx_(boost::asio::ssl::context::sslv23),
        recaptcha_(io_context_, ssl_ctx_),
        virtual_machines_(io_context_,
                          io_context_,
                          db_, *this),
        login_strand_(io_context_),
        global_chat_room_(
          io_context_,
          global_channel_id),
        guest_rng_(1'000, 99'999),
        vm_info_timer_(io_context_)
    {
      settings_.dispatch([this](auto& settings)
      {
        const auto recaptcha_key = 
          settings.GetServerSetting(ServerSetting::Setting::RECAPTCHA_KEY)
                .getRecaptchaKey();
        recaptcha_.SetRecaptchaKey(recaptcha_key);
      });
      StartVmInfoUpdate();
    }

    void StartVmInfoUpdate() {
      vm_info_timer_.expires_after(vm_info_update_frequency_);
      vm_info_timer_.async_wait(
        [&](const auto error_code) {
          if (error_code) {
            return;
          }
          virtual_machines_.dispatch([](auto& virtual_machines)
          {
            virtual_machines.UpdateVirtualMachineInfoList();
          });
          StartVmInfoUpdate();
        });
    }

    void Start(const std::string& host,
               const std::uint16_t port,
               bool auto_start_vms) {
      if (auto_start_vms)
      {
        virtual_machines_.dispatch([](auto& virtual_machines)
        {
          virtual_machines.ForEachAdminVm([](auto& vm)
          {
            vm.GetSettings([&vm](auto& settings) {
              if (settings.GetSetting(
                    VmSetting::Setting::AUTO_START).getAutoStart())
              {
                vm.Start();
              }
            });
          });
        });
      }
      TServer::Start(host, port);
    }

    void Stop() override {
      vm_info_timer_.cancel();
      virtual_machines_.dispatch(
        [](auto& virtual_machines)
        {
          virtual_machines.ForEachAdminVm(
            [](auto& vm) {
              vm.Stop();
            });
        });
      TServer::Stop();
    }

  protected:
    std::shared_ptr<typename TServer::TSocket> CreateSocket(
      boost::asio::io_context& io_context,
      const std::filesystem::path& doc_root) override
    {
      return std::make_shared<CollabVmSocket<typename TServer::TSocket>>(
        io_context, doc_root, *this);
    }

  private:
    template <typename TCallback>
    void CreateSession(
      std::shared_ptr<CollabVmSocket<typename TServer::TSocket>>&& socket,
      const std::string& username,
      TCallback&& callback)
    {
      sessions_.dispatch([
          this, socket = std::move(socket), username, callback = std::move(
            callback)
        ](auto& sessions) mutable
        {
          auto [correct_username, is_admin, old_session_id, new_session_id] =
            db_.CreateSession(username, socket->GetIpAddress().AsVector());
          if (correct_username.empty())
          {
            // TODO: Handle error
            return;
          }
          socket->SetUsername(correct_username);
          socket->is_logged_in_ = true;
          socket->is_admin_ = is_admin;
          // TODO: Can SetSessionId return a reference?
          new_session_id =
            socket->SetSessionId(sessions, std::move(new_session_id));
          if (!old_session_id.empty())
          {
            if (auto it = sessions.find(old_session_id);
                it != sessions.end())
            {
              it->second->InvalidateSession();
            }
          }
          callback(correct_username, new_session_id);
        });
    }

    using Socket = CollabVmSocket<typename TServer::TSocket>;

    struct ServerSettingsList
    {
      ServerSettingsList(Database& db)
        : db_(db),
          settings_(std::make_unique<capnp::MallocMessageBuilder>()),
          settings_list_(InitSettings(*settings_))
      {
        db_.LoadServerSettings(settings_list_);
      }

      ServerSetting::Setting::Reader GetServerSetting(
        ServerSetting::Setting::Which setting)
      {
        return settings_list_[setting].getSetting().asReader();
      }

      capnp::MallocMessageBuilder& GetServerSettingsMessageBuilder() const
      {
        return *settings_;
      }

      void UpdateServerSettings(
        const capnp::List<ServerSetting>::Reader updates)
      {
        auto message_builder = std::make_unique<capnp::MallocMessageBuilder>();
        auto list = InitSettings(*message_builder);
        Database::UpdateList<ServerSetting>(settings_list_, list, updates);
        settings_ = std::move(message_builder);
        settings_list_ = list;
        db_.SaveServerSettings(updates);
      }

      static capnp::List<ServerSetting>::Builder InitSettings(
        capnp::MallocMessageBuilder& message_builder)
      {
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

    template<typename TClient,
             typename TUserData,
             typename TBase = std::nullptr_t>
    struct UserChannel
    {
      explicit UserChannel(const std::uint32_t id) :
        chat_room_(id)
      {
      }

      const auto& GetChatRoom() const
      {
        return chat_room_;
      }

      auto& GetChatRoom()
      {
        return chat_room_;
      }

      const auto& GetUsers() const
      {
        return users_;
      }

      template<typename TCallback>
      void ForEachUser(TCallback&& callback) {
        std::for_each(
          users_.begin(),
          users_.end(),
          [callback=std::forward<TCallback>(callback)]
          (auto& user) mutable {
            callback(user.second, *user.first);
          });
      }

      void AddUser(const TUserData& user_data, std::shared_ptr<TClient> user)
      {
        OnAddUser(user);
        users_.emplace(user, user_data);
        admins_count_ += !!(user_data.user_type == CollabVmServerMessage::UserType::ADMIN);
        user->QueueMessage(
          user_data.IsAdmin()
          ? CreateAdminUserListMessage()
          : CreateUserListMessage());

        if (users_.size() <= 1) {
          return;
        }

        auto user_message = SocketMessage::CreateShared();
        auto add_user = user_message->GetMessageBuilder()
          .initRoot<CollabVmServerMessage>()
          .initMessage()
          .initUserListAdd();
        add_user.setChannel(GetId());
        AddUserToList(user_data, add_user);

        auto admin_user_message = SocketMessage::CreateShared();
        auto add_admin_user = admin_user_message->GetMessageBuilder()
          .initRoot<CollabVmServerMessage>()
          .initMessage()
          .initAdminUserListAdd();
        add_admin_user.setChannel(GetId());
        AddUserToList(user_data, add_admin_user.initUser());

        ForEachUser([user_message=std::move(user_message),
                     admin_user_message=std::move(admin_user_message)]
          (const auto& user_data, auto& user)
          {
            user.QueueMessage(
              user_data.IsAdmin() ? admin_user_message : user_message);
          });
      }

      void OnAddUser(std::shared_ptr<TClient> user) {
        if constexpr (!std::is_same_v<TBase, std::nullptr_t>) {
          if constexpr (!std::is_same_v<
                            decltype(&UserChannel::OnAddUser),
                            decltype(&TBase::OnAddUser)>) {
            static_cast<TBase&>(*this).OnAddUser(user);
          }
        }
      }

      auto GetUserData(std::shared_ptr<TClient> user_ptr)
      {
        return GetUserData(*this, user_ptr);
      }

      auto GetUserData(std::shared_ptr<TClient> user_ptr) const
      {
        return GetUserData(*this, user_ptr);
      }

      void BroadcastMessage(std::shared_ptr<SocketMessage>&& message) {
        ForEachUser(
          [message =
            std::forward<std::shared_ptr<SocketMessage>>(message)]
          (const auto&, auto& user)
          {
            user.QueueMessage(message);
          });
      }
      
      auto CreateUserListMessage() {
        return CreateUserListMessages(
          &CollabVmServerMessage::Message::Builder::initUserList);
      }

      auto CreateAdminUserListMessage() {
        return CreateUserListMessages(
          &CollabVmServerMessage::Message::Builder::initAdminUserList);
      }

      void RemoveUser(std::shared_ptr<TClient> user)
      {
        auto user_it = users_.find(user);
        if (user_it == users_.end()) {
          return;
        }
        const auto& user_data = user_it->second;
        admins_count_ -= !!(user_data.user_type == CollabVmServerMessage::UserType::ADMIN);

        auto message = SocketMessage::CreateShared();
        auto user_list_remove = message->GetMessageBuilder()
          .initRoot<CollabVmServerMessage>()
          .initMessage()
          .initUserListRemove();
        user_list_remove.setChannel(GetId());
        user_list_remove.setUsername(user_data.username);

        OnRemoveUser(user);
        users_.erase(user_it);

        BroadcastMessage(std::move(message));
      }

      void OnRemoveUser(std::shared_ptr<TClient> user) {
        if constexpr (!std::is_same_v<TBase, std::nullptr_t>) {
          static_cast<TBase&>(*this).OnRemoveUser(user);
        }
      }

      std::uint32_t GetId() const
      {
        return chat_room_.GetId();
      }

    private:
      template<typename TUserChannel>
      static auto GetUserData(TUserChannel& user_channel, std::shared_ptr<TClient> user_ptr)
      {
        static_assert(std::is_same_v<
          std::remove_const_t<TUserChannel>, UserChannel>);
        using UserData = std::conditional_t<
          std::is_const_v<TUserChannel>, const TUserData, TUserData>;
        auto& users_ = user_channel.users_;
        auto user = users_.find(user_ptr);
        return user == users_.end()
          ? std::optional<std::reference_wrapper<UserData>>()
          : std::optional<std::reference_wrapper<UserData>>(user->second);
      }

      template<typename TInitFunction>
      auto CreateUserListMessages(TInitFunction init)
      {
        auto message = SocketMessage::CreateShared();
        auto user_list = (message->GetMessageBuilder()
          .initRoot<CollabVmServerMessage>()
          .initMessage()
          .*init)();
        user_list.setChannel(GetId());
        auto users = user_list.initUsers(users_.size());
        ForEachUser(
          [this, users_it = users.begin()](auto& user_data, auto&) mutable
          {
            AddUserToList(user_data, *users_it++);
          });
        return message;
      }

      template<typename TListElement>
      void AddUserToList(const TUserData& user, TListElement list_info)
      {
        auto& username = user.username;
        list_info.setUsername(
          kj::StringPtr(username.data(), username.length()));
        list_info.setUserType(user.user_type);

        if constexpr (
          std::is_same_v<TListElement, CollabVmServerMessage::UserAdmin::Builder>)
        {
          auto ip_address = list_info.initIpAddress();
          const auto& ip_address_bytes = user.ip_address;
          ip_address.setFirst(
            boost::endian::native_to_big(
              *reinterpret_cast<const std::uint64_t*>(&ip_address_bytes[0])));
          ip_address.setSecond(
            boost::endian::native_to_big(
              *reinterpret_cast<const std::uint64_t*>(&ip_address_bytes[8])));
        }
      }

      std::unordered_map<std::shared_ptr<TClient>, TUserData> users_;
      std::uint32_t admins_count_ = 0;
      CollabVmChatRoom<Socket, CollabVm::Shared::max_username_len,
                       max_chat_message_len> chat_room_;
      capnp::MallocMessageBuilder message_builder_;
    };

    template <typename TClient>
    struct AdminVirtualMachine;

    template <typename TClient>
    struct VirtualMachine
    {
      VirtualMachine(const std::uint32_t id,
                     const CollabVmServerMessage::VmInfo::Builder vm_info) :
        vm_info_(vm_info)
      {
      }

      CollabVmServerMessage::VmInfo::Builder vm_info_;
    };

    template <typename TClient>
    struct VirtualMachinesList;

    template <typename TClient>
    struct AdminVirtualMachine
    {
      AdminVirtualMachine(boost::asio::io_context& io_context,
                          const std::uint32_t id,
                          CollabVmServer& server,
                          capnp::List<VmSetting>::Reader initial_settings,
                          // TODO: constructors shouldn't have out parameters
                          CollabVmServerMessage::AdminVmInfo::Builder admin_vm_info)
                         : id_(id),
                           state_(
                             io_context,
                             *this,
                             id,
                             io_context,
                             initial_settings,
                             admin_vm_info),
                           server_(server)
      {
      }

      void RequestTurn(std::shared_ptr<TClient> user)
      {
        state_.dispatch([user=std::move(user)](auto& state)
          {
            if (state.GetSetting(VmSetting::Setting::Which::TURNS_ENABLED)
                     .getTurnsEnabled())
            {
              state.RequestTurn(std::move(user));
            }
          });
      }

      void PauseTurnTimer()
      {
        state_.dispatch([](auto& state)
          {
            if (state.GetSetting(VmSetting::Setting::Which::TURNS_ENABLED)
                     .getTurnsEnabled())
            {
              state.PauseTurnTimer();
            }
          });
      }

      void ResumeTurnTimer()
      {
        state_.dispatch([](auto& state)
          {
            if (state.GetSetting(VmSetting::Setting::Which::TURNS_ENABLED)
                     .getTurnsEnabled())
            {
              state.ResumeTurnTimer();
            }
          });
      }

      void EndCurrentTurn(std::shared_ptr<TClient>&& user)
      {
        state_.dispatch(
          [user = std::forward<std::shared_ptr<TClient>>(user)](auto& state)
          {
            if (state.GetSetting(VmSetting::Setting::Which::TURNS_ENABLED)
                     .getTurnsEnabled()
                && (state.HasCurrentTurn(user) || state.IsAdmin(user)))
            {
              state.EndCurrentTurn();
            }
          });
      }

      struct VmState final
        : TurnController<std::shared_ptr<TClient>>,
          UserChannel<TClient, typename TClient::UserData, VmState>
      {
        using VmTurnController = TurnController<std::shared_ptr<TClient>>;
        using VmUserChannel = UserChannel<TClient, typename TClient::UserData, VmState>;

        template<typename TSettingProducer>
        VmState(
          AdminVirtualMachine& admin_vm,
          const std::uint32_t id,
          boost::asio::io_context& io_context,
          TSettingProducer&& get_setting,
          CollabVmServerMessage::AdminVmInfo::Builder admin_vm_info)
          : VmTurnController(io_context),
            VmUserChannel(id),
            message_builder_(std::make_unique<capnp::MallocMessageBuilder>()),
            settings_(GetInitialSettings(std::forward<TSettingProducer>(get_setting))),
            guacamole_client_(io_context, admin_vm)
        {
          SetAdminVmInfo(admin_vm_info);

          VmTurnController::SetTurnTime(
            std::chrono::seconds(
              GetSetting(VmSetting::Setting::TURN_TIME).getTurnTime()));
        }

        VmState(
          AdminVirtualMachine& admin_vm,
          const std::uint32_t id,
          boost::asio::io_context& io_context,
          capnp::List<VmSetting>::Reader initial_settings,
          CollabVmServerMessage::AdminVmInfo::Builder admin_vm_info)
          : VmTurnController(io_context),
            VmUserChannel(id),
            message_builder_(std::make_unique<capnp::MallocMessageBuilder>()),
            settings_(GetInitialSettings(initial_settings)),
            guacamole_client_(io_context, admin_vm)
        {
          SetAdminVmInfo(admin_vm_info);

          VmTurnController::SetTurnTime(
            std::chrono::seconds(
              GetSetting(VmSetting::Setting::TURN_TIME).getTurnTime()));
        }

        capnp::List<VmSetting>::Builder GetInitialSettings(
            capnp::List<VmSetting>::Reader initial_settings) {
          auto fields = capnp::Schema::from<VmSetting::Setting>().getUnionFields();
          if (initial_settings.size() == fields.size())
          {
            auto message =
              message_builder_->initRoot<CollabVmServerMessage>().initMessage();
            message.setReadVmConfigResponse(initial_settings);
            return message.getReadVmConfigResponse();
          }
          auto settings = message_builder_->initRoot<CollabVmServerMessage>()
                                               .initMessage()
                                               .initReadVmConfigResponse(fields.size());
          auto current_setting = initial_settings.begin();
          const auto end = initial_settings.end();
          for (auto i = 0u; i < fields.size(); i++) {
            while (current_setting != end && current_setting->getSetting().which() < i)
            {
              current_setting++;
            }
            auto new_setting = capnp::DynamicStruct::Builder(settings[i].getSetting());
            if (current_setting != end && current_setting->getSetting().which() == i)
            {
              const capnp::DynamicStruct::Reader reader = current_setting->getSetting();
              KJ_IF_MAYBE(field, reader.which()) {
                new_setting.set(*field, reader.get(*field));
                continue;
              }
            }
            new_setting.clear(fields[i]);
          }
          return settings;
        }

        template<typename TSettingProducer>
        capnp::List<VmSetting>::Builder GetInitialSettings(
            TSettingProducer&& get_setting) {
          const auto fields =
            capnp::Schema::from<VmSetting::Setting>().getUnionFields();
          auto settings = message_builder_->initRoot<CollabVmServerMessage>()
                                         .initMessage()
                                         .initReadVmConfigResponse(fields.size());
          for (auto i = 0u; i < fields.size(); i++) {
            auto dynamic_setting =
              capnp::DynamicStruct::Builder(settings[i].getSetting());
            dynamic_setting.clear(fields[i]);
          }
          auto setting = get_setting();
          while (setting)
          {
            const auto which = setting->which();
            const auto field = fields[which];
            const auto value = capnp::DynamicStruct::Reader(*setting).get(field);
            auto dynamic_setting =
              capnp::DynamicStruct::Builder(settings[which].getSetting());
            dynamic_setting.set(field, value);
            setting = get_setting();
          }
          return settings;
        }

        void SetAdminVmInfo(
          CollabVmServerMessage::AdminVmInfo::Builder admin_vm_info)
        {
          admin_vm_info.setId(VmUserChannel::GetId());
          admin_vm_info.setName(GetSetting(VmSetting::Setting::NAME).getName());
          admin_vm_info.setStatus(connected_
            ? CollabVmServerMessage::VmStatus::RUNNING
            : active_
              ? CollabVmServerMessage::VmStatus::STARTING
              : CollabVmServerMessage::VmStatus::STOPPED);
        }

        VmSetting::Setting::Reader GetSetting(
          const VmSetting::Setting::Which setting)
        {
          return settings_[setting].getSetting();
        }

        std::shared_ptr<SocketMessage> GetVmDescriptionMessage() {
          auto socket_message = SocketMessage::CreateShared();
          socket_message->GetMessageBuilder()
            .initRoot<CollabVmServerMessage>()
            .initMessage()
            .setVmDescription(
              GetSetting(VmSetting::Setting::DESCRIPTION).
              getDescription());
          return socket_message;
        }

        void OnAddUser(std::shared_ptr<TClient> user) {
          user->QueueMessageBatch(
            [&guacamole_client=guacamole_client_, description_message=
              GetVmDescriptionMessage()](auto queue_message) mutable
            {
              queue_message(std::move(description_message));
              guacamole_client.AddUser(
                [queue_message=std::move(queue_message)]
                (capnp::MallocMessageBuilder&& message_builder)
                {
                  // TODO: Avoid copying
                  auto guac_instr =
                    message_builder.getRoot<Guacamole::GuacServerInstruction>();
                  auto socket_message = SocketMessage::CreateShared();
                  socket_message->GetMessageBuilder()
                                .initRoot<CollabVmServerMessage>()
                                .initMessage()
                                .setGuacInstr(guac_instr);
                  socket_message->CreateFrame();
                  queue_message(std::move(socket_message));
                });
          });
        }

        void OnRemoveUser(std::shared_ptr<TClient> user) {
        }

        void OnCurrentUserChanged(
            std::deque<std::shared_ptr<TClient>>& users_queue,
            std::chrono::milliseconds time_remaining) override {
          BroadcastTurnQueue(users_queue, time_remaining);
        }

        void OnUserAdded(
            std::deque<std::shared_ptr<TClient>>& users_queue,
            std::chrono::milliseconds time_remaining) override {
          BroadcastTurnQueue(users_queue, time_remaining);
        }

        void OnUserRemoved(
            std::deque<std::shared_ptr<TClient>>& users_queue,
            std::chrono::milliseconds time_remaining) override {
          BroadcastTurnQueue(users_queue, time_remaining);
        }

        void BroadcastTurnQueue(
            std::deque<std::shared_ptr<TClient>>& users_queue,
            std::chrono::milliseconds time_remaining) {
          auto message = SocketMessage::CreateShared();
          auto vm_turn_info =
            message->GetMessageBuilder().initRoot<CollabVmServerMessage>()
            .initMessage().initVmTurnInfo();
          vm_turn_info.setState(
            GetSetting(VmSetting::Setting::TURNS_ENABLED).getTurnsEnabled()
            ? VmTurnController::IsPaused()
              ? CollabVmServerMessage::TurnState::PAUSED
              : CollabVmServerMessage::TurnState::ENABLED
            : CollabVmServerMessage::TurnState::DISABLED);
          vm_turn_info.setTimeRemaining(time_remaining.count());
          auto users_list = vm_turn_info.initUsers(users_queue.size());
          auto i = 0u;
          const auto& channel_users = VmUserChannel::GetUsers();
          for (auto& user_in_queue : users_queue) {
            if (const auto channel_user = channel_users.find(user_in_queue);
                channel_user != channel_users.end()) {
              users_list.set(i++, channel_user->second.username);
            }
          }
          VmUserChannel::BroadcastMessage(std::move(message));
        }

        void ApplySettings(const capnp::List<VmSetting>::Reader settings,
                           const std::optional<capnp::List<VmSetting>::Reader>
                           previous_settings = {})
        {
          VmTurnController::SetTurnTime(
            std::chrono::seconds(
              settings[VmSetting::Setting::TURN_TIME]
              .getSetting().getTurnTime()));
          if (!settings[VmSetting::Setting::Which::TURNS_ENABLED]
               .getSetting().getTurnsEnabled()
            && previous_settings.has_value()
            && previous_settings.value()[VmSetting::Setting::Which::
                 TURNS_ENABLED]
               .getSetting().getTurnsEnabled())
          {
            VmTurnController::Clear();
          }
          const auto description =
            settings[VmSetting::Setting::Which::DESCRIPTION]
            .getSetting().getDescription();
          if (!previous_settings.has_value()
              || previous_settings.value()
                 [VmSetting::Setting::Which::DESCRIPTION]
                 .getSetting().getDescription() != description)
          {
            VmUserChannel::BroadcastMessage(GetVmDescriptionMessage());
          }

          SetGuacamoleArguments();
        }

        void SetGuacamoleArguments()
        {
          const auto params =
            GetSetting(VmSetting::Setting::GUACAMOLE_PARAMETERS)
            .getGuacamoleParameters();
          auto params_map =
            std::unordered_map<std::string_view, std::string_view>(
              params.size());
          std::transform(params.begin(), params.end(),
                         std::inserter(params_map, params_map.end()),
            [](auto param)
            {
              return std::pair(
                param.getName().cStr(), param.getValue().cStr());
            });
          guacamole_client_.SetArguments(std::move(params_map));
        }

        [[nodiscard]]
        bool HasCurrentTurn(const std::shared_ptr<TClient>& user) const
        {
          const auto current_user = VmTurnController::GetCurrentUser();
          return current_user.has_value() && current_user == user;
        }

        [[nodiscard]]
        bool IsAdmin(const std::shared_ptr<TClient>& user) const
        {
          const auto user_data = VmUserChannel::GetUserData(user);
          return user_data.has_value() && user_data.value().get().IsAdmin();
        }

        bool active_ = false;
        bool connected_ = false;
        std::size_t viewer_count_ = 0;
        std::unique_ptr<capnp::MallocMessageBuilder> message_builder_;
        capnp::List<VmSetting>::Builder settings_;
        CollabVmGuacamoleClient<AdminVirtualMachine> guacamole_client_;
      };

      template<typename TCallback>
      void GetUserChannel(TCallback&& callback) {
        state_.dispatch([callback = std::forward<TCallback>(callback)]
        (auto& state) mutable {
          callback(static_cast<UserChannel<TClient, typename TClient::UserData, VmState>&>(state));
        });
      }

      void Start()
      {
        state_.dispatch([this](auto& state)
        {
          if (state.active_) {
              return;
          }

          if (const auto start_command =
                state.GetSetting(VmSetting::Setting::START_COMMAND).getStartCommand();
              start_command.size()) {
            ExecuteCommandAsync(start_command.cStr());
          }

          state.active_ = true;
          UpdateVmInfo();

          state.SetGuacamoleArguments();
          const auto protocol =
            state.GetSetting(VmSetting::Setting::PROTOCOL).getProtocol();
          if (protocol == VmSetting::Protocol::RDP)
          {
            state.guacamole_client_.StartRDP();
          }
          else if (protocol == VmSetting::Protocol::VNC)
          {
            state.guacamole_client_.StartVNC();
          }
        });
      }

      void Stop()
      {
        state_.dispatch([this](auto& state)
        {
          if (!state.active_) {
              return;
          }

          if (const auto stop_command =
                state.GetSetting(VmSetting::Setting::STOP_COMMAND).getStopCommand();
              stop_command.size()) {
            ExecuteCommandAsync(stop_command.cStr());
          }

          state.active_ = false;
          state.guacamole_client_.Stop();
        });
      }

      void Restart()
      {
        state_.dispatch([this](auto& state)
        {
          if (!state.active_) {
              return;
          }

          if (const auto restart_command =
                state.GetSetting(VmSetting::Setting::RESTART_COMMAND).getRestartCommand();
              restart_command.size()) {
            ExecuteCommandAsync(restart_command.cStr());
          }

          state.active_ = true;
          state.guacamole_client_.Stop();
        });
      }

      template<typename TCallback>
      void GetSettings(TCallback&& callback)
      {
        state_.dispatch(
          [callback = std::forward<TCallback>(callback)](auto& settings)
          {
            callback(settings);
          });
      }

      template<typename TCallback>
      void GetSettingsMessage(TCallback&& callback)
      {
        state_.dispatch(
          [callback = std::forward<TCallback>(callback)](auto& settings)
          {
            callback(*settings.message_builder_);
          });
      }

      void SetSetting(
        const VmSetting::Setting::Which setting,
        const capnp::StructSchema::Field field,
        const capnp::DynamicValue::Reader value)
      {
        capnp::DynamicStruct::Builder dynamic_server_setting = state_[setting
        ].getSetting();
        dynamic_server_setting.set(field, value);
        server_.virtual_machines_.dispatch([this](auto& virtual_machines)
        {
          virtual_machines.GetVmListData(this);
        });
      }

      template<typename TSetVmInfo>
      void SetVmInfo(TSetVmInfo&& set_vm_info)
      {
        state_.dispatch([this,
          set_vm_info = std::forward<TSetVmInfo>(set_vm_info)](auto& state) mutable
        {
          auto admin_vm_info = set_vm_info.InitAdminVmInfo();
          state.SetAdminVmInfo(admin_vm_info);
          if (!state.active_)
          {
            return;
          }

          state.viewer_count_ = state.GetUsers().size();

          auto vm_info = set_vm_info.InitVmInfo();
          vm_info.setId(state.GetId());
          vm_info.setName(state.GetSetting(VmSetting::Setting::NAME).getName());
          // vm_info.setHost();
          // vm_info.setAddress();
          vm_info.setOperatingSystem(state.GetSetting(VmSetting::Setting::OPERATING_SYSTEM).getOperatingSystem());
          vm_info.setUploads(state.GetSetting(VmSetting::Setting::UPLOADS_ENABLED).getUploadsEnabled());
          vm_info.setInput(state.GetSetting(VmSetting::Setting::TURNS_ENABLED).getTurnsEnabled());
          vm_info.setRam(state.GetSetting(VmSetting::Setting::RAM).getRam());
          vm_info.setDiskSpace(state.GetSetting(VmSetting::Setting::DISK_SPACE).getDiskSpace());
          vm_info.setSafeForWork(state.GetSetting(VmSetting::Setting::SAFE_FOR_WORK).getSafeForWork());
          vm_info.setViewerCount(state.viewer_count_);

          auto png = std::vector<std::byte>();
          png.reserve(100 * 1'024);
          const auto created_screenshot =
            state.guacamole_client_.CreateScreenshot([&png](auto png_bytes)
            {
              png.insert(png.end(), png_bytes.begin(), png_bytes.end());
            });
          if (created_screenshot) {
            set_vm_info.SetThumbnail(std::move(png));
          }
        });
      }

      template<typename TGetModifiedSettings, typename TContinuation>
      void UpdateSettings(Database& db,
                          TGetModifiedSettings&& get_modified_settings,
                          TContinuation&& continuation)
      {
        state_.dispatch([this, &db,
          get_modified_settings =
            std::forward<TGetModifiedSettings>(get_modified_settings),
          continuation = std::forward<TContinuation>(continuation)](auto& state) mutable
        {
          auto current_settings = state.settings_;
          auto message_builder = std::make_unique<capnp::MallocMessageBuilder>();
          const auto new_settings = message_builder->initRoot<CollabVmServerMessage>()
                                               .initMessage()
                                               .initReadVmConfigResponse(
                                                 capnp::Schema::from<VmSetting::
                                                   Setting>()
                                                 .getUnionFields().size());
          auto modified_settings = get_modified_settings();
          Database::UpdateList<VmSetting>(current_settings.asReader(),
                                          new_settings,
                                          modified_settings);
          const auto valid = ValidateSettings(new_settings);
          if (valid)
          {
            db.UpdateVmSettings(state.GetId(), modified_settings);
            state.settings_ = new_settings;
            state.ApplySettings(new_settings, current_settings);
            state.message_builder_ = std::move(message_builder);
          }

          continuation(valid);
        });
      }

      template<typename TCallback>
      void ReadInstruction(std::shared_ptr<TClient> user, TCallback&& callback)
      {
        state_.dispatch(
          [user = std::move(user),
           callback=std::forward<TCallback>(callback)](auto& state)
          {
            if (state.connected_
                && (state.HasCurrentTurn(user) && !state.IsPaused()
                    || state.IsAdmin(user))) {
              state.guacamole_client_.ReadInstruction(callback());
            }
          });
      }

      std::uint32_t GetId() const {
        return id_;
      }

    private:
      friend struct CollabVmGuacamoleClient<AdminVirtualMachine>;

      void UpdateVmInfo()
      {
        server_.virtual_machines_.dispatch(
          [this](auto& virtual_machines)
          {
            virtual_machines.UpdateVirtualMachineInfo(*this);
          });
      }

      void OnStart()
      {
        state_.dispatch([this](auto& state)
        {
          if (!state.active_)
          {
            state.guacamole_client_.Stop();
            return;
          }
          state.connected_ = true;
          UpdateVmInfo();

          const auto& users = state.GetUsers();
          state.guacamole_client_.AddUser(
            [&users](capnp::MallocMessageBuilder&& message_builder)
            {
              auto socket_message =
                SocketMessage::CopyFromMessageBuilder(message_builder);
              for (auto& user : users)
              {
                user.first->QueueMessage(socket_message);
              }
            });
        });
      }

      void OnStop()
      {
        state_.dispatch([this](auto& state)
          {
            state.connected_ = false;
            UpdateVmInfo();
            if (!state.active_)
            {
              return;
            }
            const auto protocol =
              state.GetSetting(VmSetting::Setting::PROTOCOL).getProtocol();
            if (protocol == VmSetting::Protocol::RDP)
            {
              state.guacamole_client_.StartRDP();
            }
            else if (protocol == VmSetting::Protocol::VNC)
            {
              state.guacamole_client_.StartVNC();
            }
          });
      }

      static bool ValidateSettings(capnp::List<VmSetting>::Reader settings)
      {
        for (auto i = 0u; i < settings.size(); i++)
        {
          assert(settings[i].getSetting().which() == i);
        }

        return
          (!settings[VmSetting::Setting::Which::TURNS_ENABLED]
            .getSetting().getTurnsEnabled() ||
            settings[VmSetting::Setting::Which::TURN_TIME]
            .getSetting().getTurnTime() > 0) &&
          (!settings[VmSetting::Setting::Which::VOTES_ENABLED]
            .getSetting().getVotesEnabled() ||
            settings[VmSetting::Setting::Which::VOTE_TIME]
            .getSetting().getVoteTime() > 0);
      }

      const std::uint32_t id_;
      StrandGuard<VmState> state_;
      CollabVmServer& server_;
    };

    template <typename TClient>
    struct VirtualMachinesList
    {
      VirtualMachinesList(boost::asio::io_context& io_context,
                          Database& db,
                          CollabVmServer& server)
        : server_(server)
      {
        auto admin_vm_list_message_builder = SocketMessage::CreateShared();
        auto admin_virtual_machines =
          std::unordered_map<std::uint32_t, std::shared_ptr<AdminVm>>();
        auto admin_virtual_machines_list =
          admin_vm_list_message_builder->GetMessageBuilder()
                                      .initRoot<CollabVmServerMessage>()
                                      .initMessage()
                                      .initReadVmsResponse(
                                        db.GetVmCount());
        struct VmSettingsList
        {
          capnp::MallocMessageBuilder message_builder_;
          capnp::Orphan<capnp::List<VmSetting>> list = message_builder_.getOrphanage().template newOrphan<capnp::List<VmSetting>>(capnp::Schema::from<VmSetting::Setting>().getUnionFields().size());
          VmSettingsList operator=(VmSettingsList&&) noexcept { return VmSettingsList(); }
        } vm_settings;
        auto previous_vm_id = std::optional<std::size_t>();
        auto vm_setting_index = 0u;
        auto create_vm = [&, admin_vm_info_it = admin_virtual_machines_list.begin()]() mutable
          {
            vm_settings.list.truncate(vm_setting_index);
            vm_setting_index = 0;
            const auto vm_id = previous_vm_id.value();
            admin_virtual_machines.emplace(
              vm_id,
              std::make_shared<AdminVm>(
                io_context, vm_id, server_,
                vm_settings.list.get(), *admin_vm_info_it++)
            );
            vm_settings = VmSettingsList();
          };
        db.ReadVmSettings(
          [&](auto vm_id, auto setting_id, VmSetting::Reader setting) mutable
          {
            if (previous_vm_id.has_value() && previous_vm_id != vm_id)
            {
              create_vm();
            }
            vm_settings.list.get().setWithCaveats(vm_setting_index++, setting);
            previous_vm_id = vm_id;
          });
        if (previous_vm_id.has_value())
        {
          create_vm();
        }
			  admin_vm_info_list_ =
          ResizableList<InitAdminVmInfo>(
            std::move(admin_vm_list_message_builder));
			  admin_virtual_machines_ = std::move(admin_virtual_machines);
      }

      AdminVirtualMachine<TClient>* GetAdminVirtualMachine(
        const std::uint32_t id)
      {
        auto vm = admin_virtual_machines_.find(id);
        if (vm == admin_virtual_machines_.end())
        {
          return {};
        }
        return &vm->second->vm;
      }

      AdminVirtualMachine<TClient>* RemoveAdminVirtualMachine(
        const std::uint32_t id)
      {
        auto vm = admin_virtual_machines_.find(id);
        if (vm == admin_virtual_machines_.end())
        {
          return {};
        }
        auto& admin_vm = vm->second->vm;
        admin_virtual_machines_.erase(vm);
        admin_vm_info_list_.RemoveFirst([id](auto info)
        {
          return info.getId() == id;
        });
        return &admin_vm;
      }

      void SendAdminVmList(TClient& client) const
      {
        client.QueueMessage(admin_vm_info_list_.GetMessage());
      }

      template<typename TCallback>
      void ForEachAdminVm(TCallback&& callback)
      {
        for (auto& [id, admin_vm] : admin_virtual_machines_)
        {
          callback(admin_vm->vm);
        }
      }

      /*
      auto AddVirtualMachine(const std::uint32_t id)
      {
        return std::make_shared<VirtualMachine<TClient>>(
          id, vm_info_list_.Add());
      }
      */

      void AddVmListViewer(std::shared_ptr<TClient>&& viewer)
      {
        SendThumbnails(*viewer);
        vm_list_viewers_.emplace_back(
          std::forward<std::shared_ptr<TClient>>(viewer));
      }

      auto& AddAdminVirtualMachine(boost::asio::io_context& io_context,
                                   const std::uint32_t id,
                                   capnp::List<VmSetting>::Reader
                                   initial_settings)
      {
        auto admin_vm_info = admin_vm_info_list_.Add();
        auto vm = std::make_shared<AdminVm>(
          io_context, id, server_, initial_settings, admin_vm_info);
        auto [it, inserted_new] =
          admin_virtual_machines_.emplace(id, std::move(vm));
        assert(inserted_new);
        return it->second->vm;
      }

      void AddAdminVmListViewer(std::shared_ptr<TClient>&& viewer_ptr)
      {
        auto& viewer = *viewer_ptr;
        admin_vm_list_viewers_.emplace_back(
          std::forward<std::shared_ptr<TClient>>(viewer_ptr));
        viewer.QueueMessage(admin_vm_info_list_.GetMessage());
      }

      void BroadcastToViewingAdminsExcluding(
        const std::shared_ptr<CopiedSocketMessage>& message,
        const std::shared_ptr<TClient>& exclude)
      {
        if (admin_vm_list_viewers_.empty() ||
            (admin_vm_list_viewers_.size() == 1 &&
            admin_vm_list_viewers_.front() == exclude))
        {
          return;
        }
        std::for_each(
          admin_vm_list_viewers_.begin(), admin_vm_list_viewers_.end(),
          [&message, &exclude](auto& viewer)
          {
            if (viewer != exclude)
            {
              viewer->QueueMessage(message);
            }
          });
      }

      template<typename TMessage>
      void BroadcastToViewingAdmins(const TMessage& message) {
        std::for_each(
          admin_vm_list_viewers_.begin(), admin_vm_list_viewers_.end(),
          [&message](auto& viewer)
          {
            viewer->QueueMessage(message);
          });
      }

      void RemoveAdminVmListViewer(const std::shared_ptr<TClient>& viewer)
      {
        const auto it = std::find(admin_vm_list_viewers_.begin(),
                                  admin_vm_list_viewers_.end(),
                                  viewer);
        if (it == admin_vm_list_viewers_.end())
        {
          return;
        }
        admin_vm_list_viewers_.erase(it);
      }

      void RemoveVmListViewer(const std::shared_ptr<TClient>& viewer)
      {
        const auto it = std::find(vm_list_viewers_.begin(),
                                  vm_list_viewers_.end(),
                                  viewer);
        if (it == vm_list_viewers_.end())
        {
          return;
        }
        vm_list_viewers_.erase(it);
      }

      std::size_t pending_vm_info_requests_ = 0;
      std::size_t pending_vm_info_updates_  = 0;

      template<typename TFinalizer>
      struct VmInfoProducer {
        VmInfoProducer(TFinalizer&& finalizer)
          : finalizer(std::forward<TFinalizer>(finalizer)) {
        }
        VmInfoProducer(VmInfoProducer&& vm_info_producer) = default;
        TFinalizer finalizer;
        std::vector<std::byte> png_bytes;
        std::unique_ptr<capnp::MallocMessageBuilder> message_builder = std::make_unique<capnp::MallocMessageBuilder>();
        capnp::Orphan<CollabVmServerMessage::AdminVmInfo> admin_vm_info;
        capnp::Orphan<CollabVmServerMessage::VmInfo> vm_info;
        CollabVmServerMessage::AdminVmInfo::Builder InitAdminVmInfo() {
          admin_vm_info = message_builder->getOrphanage().newOrphan<CollabVmServerMessage::AdminVmInfo>();
          return admin_vm_info.get();
        }
        CollabVmServerMessage::VmInfo::Builder InitVmInfo() {
          vm_info = message_builder->getOrphanage().newOrphan<CollabVmServerMessage::VmInfo>();
          return vm_info.get();
        }
        void SetThumbnail(std::vector<std::byte>&& png_bytes) {
          VmInfoProducer::png_bytes =
            std::forward<std::vector<std::byte>>(png_bytes);
        }
        ~VmInfoProducer() {
          if (message_builder) {
            finalizer(*this);
          }
        }
      };

      void UpdateVirtualMachineInfoList()
      {
        if (pending_vm_info_requests_)
        {
          // An update is already pending
          return;
        }
        pending_vm_info_requests_ = admin_virtual_machines_.size();
        pending_vm_info_updates_ = 0;
        for (auto& [vm_id, vm] : admin_virtual_machines_)
        {
          auto callback = server_.virtual_machines_.wrap(
              [this, vm=vm, vm_id=vm_id](auto&, auto& vm_info_producer) mutable
              {
                if (auto& thumbnail_bytes = vm_info_producer.png_bytes;
                  thumbnail_bytes.empty()) {
                } else {
                  thumbnails_.erase(ThumbnailKey("", vm_id));
                  auto& thumbnail_message =
                    thumbnails_[ThumbnailKey("", vm_id)] =
                    SocketMessage::CreateShared();
                  auto& message_builder = thumbnail_message->GetMessageBuilder();
                  auto thumbnail =
                    message_builder.template initRoot<CollabVmServerMessage>()
                    .initMessage().initVmThumbnail();
                  thumbnail.setId(vm_id);
                  thumbnail.setPngBytes(kj::ArrayPtr(
                    reinterpret_cast<kj::byte*>(thumbnail_bytes.data()),
                    thumbnail_bytes.size()));
                }
                vm->has_vm_info = vm_info_producer.vm_info != nullptr;
                if (vm->has_vm_info)
                {
                  pending_vm_info_updates_++;
                }
                vm->SetPendingVmInfo(
                  std::move(vm_info_producer.message_builder),
                  std::move(vm_info_producer.admin_vm_info),
                  std::move(vm_info_producer.vm_info));
                if (--pending_vm_info_requests_)
                {
                  return;
                }
                /*
                // TODO: Sort on server-side
                auto orphanage = admin_vm_info_list_.GetMessageBuilder().getOrphanage();
                orphanage.newOrphan<CollabVmServerMessage::VmInfo>();
                */
                admin_vm_info_list_.Reset(admin_virtual_machines_.size());
                //using GetList = typename decltype(admin_vm_info_list_)::List::GetList;
                auto admin_vm_info_list =
                  decltype(admin_vm_info_list_)::List::GetList(
                    admin_vm_info_list_.GetMessageBuilder());
                vm_info_list_.Reset(pending_vm_info_updates_);
                auto vm_info_list =
                  decltype(vm_info_list_)::List::GetList(
                    vm_info_list_.GetMessageBuilder());
                auto admin_vm_info_list_index = 0u;
                auto vm_info_list_index = 0u;
                for (auto& [id, admin_vm] : admin_virtual_machines_)
                {
                  if (!admin_vm->HasPendingAdminVmInfo())
                  {
                    continue;
                  }
                  admin_vm_info_list.setWithCaveats(
                    admin_vm_info_list_index++,
                    admin_vm->GetPendingAdminVmInfo());
                  if (admin_vm->HasPendingVmInfo())
                  {
                    vm_info_list.setWithCaveats(
                      vm_info_list_index++, admin_vm->GetPendingVmInfo());
                  }
                  admin_vm->FreeVmInfo();
                }
                std::for_each(vm_list_viewers_.begin(), vm_list_viewers_.end(),
                  [vm_list_message=vm_info_list_.GetMessage(),
                   thumbnails=GetThumbnailMessages()](auto& viewer)
                  {
                    viewer->QueueMessageBatch(
                      [vm_list_message, thumbnails](auto queue_message)
                      {
                        queue_message(std::move(vm_list_message));

                        std::for_each(
                          thumbnails->begin(),
                          thumbnails->end(),
                          queue_message);
                      });
                  });
                BroadcastToViewingAdmins(admin_vm_info_list_.GetMessage());
              });
          vm->vm.SetVmInfo(
            VmInfoProducer<decltype(callback)>(std::move(callback)));
        }
      }

      void UpdateVirtualMachineInfo(AdminVirtualMachine<TClient>& vm) {
        const auto vm_id = vm.GetId();
	auto callback = server_.virtual_machines_.wrap(
            [this, vm_id](auto&, auto& vm_info_producer) mutable
            {
              auto& vm_data = *admin_virtual_machines_[vm_id];
              if (vm_data.HasPendingAdminVmInfo())
              {
                // A bulk update is already in progress
                vm_data.SetPendingVmInfo(
                  std::move(vm_info_producer.message_builder),
                  std::move(vm_info_producer.admin_vm_info),
                  std::move(vm_info_producer.vm_info));
                return;
              }
              admin_vm_info_list_.UpdateElement(
                [vm_id](auto vm_info)
                {
                  return vm_info.getId() == vm_id;
                }, vm_info_producer.admin_vm_info.get());
              BroadcastToViewingAdmins(admin_vm_info_list_.GetMessage());

              if (vm_data.has_vm_info) {
                auto predicate = [vm_id](auto vm_info)
                  {
                    return vm_info.getId() == vm_id
                      && !vm_info.getHost().size();
                  };
                if (vm_info_producer.vm_info == nullptr) {
                  vm_info_list_.RemoveFirst(std::move(predicate));
                  vm_data.has_vm_info = false;
                } else {
                  vm_info_list_.UpdateElement(
                    std::move(predicate), vm_info_producer.vm_info.get());
                }
              } else {
                if (vm_info_producer.vm_info == nullptr) {
                  return;
                }
                vm_info_list_.Add(vm_info_producer.vm_info.get());
                vm_data.has_vm_info = true;
              }
              std::for_each(vm_list_viewers_.begin(), vm_list_viewers_.end(),
                [vm_list_message=vm_info_list_.GetMessage()](auto& viewer)
                {
                  viewer->QueueMessage(vm_list_message);
                });
            });
        vm.SetVmInfo(
          VmInfoProducer<decltype(callback)>(std::move(callback)));
      }

      template <typename TFunction>
      struct ResizableList
      {
        using List = TFunction;
        explicit ResizableList(std::shared_ptr<SharedSocketMessage>&& message)
          : message_(
              std::forward<std::shared_ptr<SharedSocketMessage>>(message)),
            list_(TFunction::GetList(message_->GetMessageBuilder()))
        {
        }

        ResizableList() :
          message_(SocketMessage::CreateShared()),
          list_(TFunction::InitList(message_->GetMessageBuilder(), 0))
        {
        }

        auto Add()
        {
          auto message = SocketMessage::CreateShared();
          auto vm_list = TFunction::InitList(message->GetMessageBuilder(), list_.size() + 1);
          std::copy(list_.begin(), list_.end(), vm_list.begin());
          list_ = vm_list;
          message_ = std::move(message);
          return vm_list[vm_list.size() - 1];
        }

        template<typename TNewElement>
        void Add(TNewElement new_element)
        {
          auto message = SocketMessage::CreateShared();
          auto vm_list = TFunction::InitList(message->GetMessageBuilder(), list_.size() + 1);
          std::copy(list_.begin(), list_.end(), vm_list.begin());
          list_ = vm_list;
          message_ = std::move(message);
          vm_list.setWithCaveats(vm_list.size() - 1, new_element);
        }

        template<typename TPredicate>
        void RemoveFirst(TPredicate&& predicate)
        {
          auto message = SocketMessage::CreateShared();
          auto vm_list =
            TFunction::InitList(message->GetMessageBuilder(), list_.size() - 1);
          const auto copy_end =
            std::remove_copy_if(list_.begin(), list_.end(),
                                vm_list.begin(), std::move(predicate));
          assert(copy_end == vm_list.end());
          list_ = vm_list;
          message_ = std::move(message);
        }

        template<typename TPredicate, typename TNewElement>
        void UpdateElement(TPredicate&& predicate, TNewElement new_element)
        {
          auto message = SocketMessage::CreateShared();
          const auto size = list_.size();
          auto new_vm_list =
            TFunction::InitList(message->GetMessageBuilder(), size);
          for (auto i = 0u; i < size; i++)
          {
            new_vm_list.setWithCaveats(
              i, predicate(list_[i]) ? new_element : list_[i]);
          }
          list_ = new_vm_list;
          message_ = std::move(message);
        }

        void Reset(unsigned capacity)
        {
          message_ = SocketMessage::CreateShared();
          list_ =
            TFunction::InitList(message_->GetMessageBuilder(), capacity);
        }

        capnp::MallocMessageBuilder& GetMessageBuilder() const
        {
          return message_->GetMessageBuilder();
        }

        std::shared_ptr<SharedSocketMessage> GetMessage() const
        {
          return message_;
        }
      private:
        std::shared_ptr<SharedSocketMessage> message_;
        using ListType = std::invoke_result_t<decltype(TFunction::GetList),
          capnp::MallocMessageBuilder&>;
        ListType list_;
      };

    private:
      struct InitVmInfo
      {
        static capnp::List<CollabVmServerMessage::VmInfo>::Builder GetList(
          capnp::MallocMessageBuilder& message_builder)
        {
          return message_builder
                 .getRoot<CollabVmServerMessage>().getMessage().
                 getVmListResponse();
        }

        static capnp::List<CollabVmServerMessage::VmInfo>::Builder InitList(
          capnp::MallocMessageBuilder& message_builder, unsigned size)
        {
          return message_builder
                 .initRoot<CollabVmServerMessage>().initMessage().
                 initVmListResponse(size);
        }
      };

      struct InitAdminVmInfo
      {
        static capnp::List<CollabVmServerMessage::AdminVmInfo>::Builder GetList(
          capnp::MallocMessageBuilder& message_builder)
        {
          return message_builder
                 .getRoot<CollabVmServerMessage>().getMessage().
                 getReadVmsResponse();
        }

        static capnp::List<CollabVmServerMessage::AdminVmInfo>::Builder InitList(
          capnp::MallocMessageBuilder& message_builder, unsigned size)
        {
          return message_builder
                 .initRoot<CollabVmServerMessage>().initMessage().
                 initReadVmsResponse(size);
        }
      };

      ResizableList<InitVmInfo> vm_info_list_;
      ResizableList<InitAdminVmInfo> admin_vm_info_list_;

      struct AdminVm
      {
        template<typename... TArgs>
        explicit AdminVm(TArgs&& ... args)
          : vm(std::forward<TArgs>(args)...)
        {
        }
	~AdminVm() noexcept { }
        AdminVirtualMachine<TClient> vm;
        void SetPendingVmInfo(
            std::unique_ptr<capnp::MallocMessageBuilder>&& message_builder,
            capnp::Orphan<CollabVmServerMessage::AdminVmInfo>&& admin_vm_info,
            capnp::Orphan<CollabVmServerMessage::VmInfo>&& vm_info) {
          pending_admin_vm_info = std::move(admin_vm_info);
          pending_vm_info = std::move(vm_info);
          pending_vm_info_message_builder = std::move(message_builder);
        }
        CollabVmServerMessage::VmInfo::Reader GetPendingVmInfo() const {
          return pending_vm_info.getReader();
        }
        CollabVmServerMessage::AdminVmInfo::Reader GetPendingAdminVmInfo() const {
          return pending_admin_vm_info.getReader();
        }
        bool HasPendingAdminVmInfo() const {
          return pending_admin_vm_info != nullptr;
        }
        bool HasPendingVmInfo() const {
          return pending_vm_info != nullptr;
        }
        void FreeVmInfo() {
          pending_admin_vm_info = {};
          pending_vm_info       = {};
          pending_vm_info_message_builder.reset();
        }
        bool has_vm_info = false;
      private:
        std::unique_ptr<capnp::MallocMessageBuilder> pending_vm_info_message_builder;
        capnp::Orphan<CollabVmServerMessage::AdminVmInfo> pending_admin_vm_info;
        capnp::Orphan<CollabVmServerMessage::VmInfo> pending_vm_info;
      };

      std::shared_ptr<const std::vector<std::shared_ptr<SharedSocketMessage>>>
      GetThumbnailMessages() {
        auto thumbnail_messages =
          std::make_shared<std::vector<std::shared_ptr<SharedSocketMessage>>>();
        thumbnail_messages->reserve(thumbnails_.size());
        std::transform(thumbnails_.begin(), thumbnails_.end(),
          std::back_inserter(*thumbnail_messages),
          [](auto& thumbnail)
          {
              return thumbnail.second;
          });
        return thumbnail_messages;
      }

      void SendThumbnails(TClient& user) {
        user.QueueMessageBatch(
          [vm_list_message=vm_info_list_.GetMessage(),
           thumbnails=GetThumbnailMessages()](auto queue_message)
          {
            queue_message(std::move(vm_list_message));

            std::for_each(
              thumbnails->begin(),
              thumbnails->end(),
              queue_message);
          });
      }

      std::unordered_map<
        std::uint32_t, std::shared_ptr<AdminVm>> admin_virtual_machines_;
      CollabVmServer& server_;
      std::vector<std::shared_ptr<TClient>> vm_list_viewers_;
      std::vector<std::shared_ptr<TClient>> admin_vm_list_viewers_;
      using ThumbnailKey = std::pair<std::string, std::uint32_t>;
      std::unordered_map<ThumbnailKey,
        std::shared_ptr<SharedSocketMessage>,
        boost::hash<ThumbnailKey>> thumbnails_;
    };

    static void ExecuteCommandAsync(const std::string_view command) {
      // system() is used for simplicity but it is actually synchronous,
      // so the command is manipulated to make the shell return immediately
      const auto async_shell_command =
#ifdef _WIN32
        std::string("start ") + command.data();
#else
        command.data() + std::string(" &");
#endif
      system(async_shell_command.c_str());
    }

    const std::chrono::seconds vm_info_update_frequency_ =
      std::chrono::seconds(10);

    Database db_;
    StrandGuard<ServerSettingsList> settings_;
    using SessionMap = std::unordered_map<SessionId,
                                          std::shared_ptr<Socket>
                                          >;
    StrandGuard<SessionMap> sessions_;
    StrandGuard<
      std::unordered_map<
        std::string,
        std::shared_ptr<Socket>,
        CaseInsensitiveHasher,
        CaseInsensitiveComparator
      >
    >
    guests_;
    boost::asio::ssl::context ssl_ctx_;
    RecaptchaVerifier recaptcha_;
    StrandGuard<VirtualMachinesList<CollabVmSocket<typename TServer::TSocket>>>
    virtual_machines_;
    Strand login_strand_;
    StrandGuard<UserChannel<Socket, typename CollabVmSocket<typename TServer::TSocket>::UserData>> global_chat_room_;
    std::uniform_int_distribution<std::uint32_t> guest_rng_;
    std::default_random_engine rng_{std::random_device()()};
    boost::asio::steady_timer vm_info_timer_;
  };
} // namespace CollabVm::Server
