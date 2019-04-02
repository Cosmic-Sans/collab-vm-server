#pragma once
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/filesystem.hpp>
#include <boost/range/adaptors.hpp>
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

#include "capnp-list.hpp"
#include "CapnpMessageFrameBuilder.hpp"
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

namespace CollabVm::Server
{
  template <typename TServer>
  class CollabVmServer final : public TServer
  {
    using Strand = typename TServer::Strand;
    template <typename T>
    using StrandGuard = StrandGuard<Strand, T>;

  public:
    //constexpr static auto min_username_len = 3;
    //constexpr static auto max_username_len = 20;
    constexpr static auto max_chat_message_len = 100;
    constexpr static auto global_channel_id = 0;

    template <typename TSocket>
    class CollabVmSocket final : public TSocket
    {
      using SessionMap = std::unordered_map<
        Database::SessionId,
        std::shared_ptr<CollabVmSocket>,
        Database::SessionIdHasher>;

    public:
      CollabVmSocket(boost::asio::io_context& io_context,
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
          // is_admin_(false),
            is_admin_(true), // TESTING ONLY
          is_viewing_server_config(false)
      {
      }

      class CollabVmMessageBuffer : public TSocket::MessageBuffer
      {
        capnp::FlatArrayMessageReader reader;
      public:
        CollabVmMessageBuffer() : reader(nullptr) {}
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
        auto message = reader.getRoot<CollabVmClientMessage>().getMessage();

        switch (message.which())
        {
        case CollabVmClientMessage::Message::CONNECT_TO_CHANNEL:
        {
          const auto channel_id = message.getConnectToChannel();
          auto connect_to_channel = [this, self = shared_from_this(), channel_id](){
            auto socket_message = SocketMessage::CreateShared();
            auto& message_builder = socket_message->GetMessageBuilder();
            auto connect_result =
              message_builder.initRoot<CollabVmServerMessage>()
              .initMessage()
              .initConnectResponse()
              .initResult();
            auto connect_to_channel = [this, self = shared_from_this(),
              socket_message, connect_result](auto& channel) mutable
            {
              channel.AddClient(shared_from_this());
              auto connect_success = connect_result.initSuccess();
              connect_success.setChatMessages(
                channel.GetChatRoom().GetChatHistory());
              QueueMessage(std::move(socket_message));
            };
            if (channel_id == global_channel_id)
            {
              server_.global_chat_room_.dispatch(std::move(connect_to_channel));
            }
            else
            {
              server_.virtual_machines_.dispatch([
                this, self = shared_from_this(),
                  channel_id, socket_message,
                  connect_result, connect_to_channel = std::move(
                    connect_to_channel)
              ](auto& virtual_machines) mutable
                {
                  const auto virtual_machine = virtual_machines.
                    GetAdminVirtualMachine(channel_id);
                  if (!virtual_machine)
                  {
                    connect_result.setFail();
                    QueueMessage(std::move(socket_message));
                    return;
                  }
                  connect_to_channel(*virtual_machine);
                });
            }
          };
          if (username_.empty())
          {
            GenerateUsername(std::move(connect_to_channel));
          }
          else
          {
            connect_to_channel();
          }
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
                auto username_change = message.initChangeUsername();
                username_change.setOldUsername(username_);
                username_change.setNewUsername(new_username);
                username_ = new_username;
                guests[username_] = self;
                QueueMessage(std::move(socket_message));
              });
            break;
          }
        case CollabVmClientMessage::Message::CHANGE_PASSWORD_REQUEST:
            if (!is_logged_in_)
            {
              break;
            }
            {
              const auto change_password_request =
                message.getChangePasswordRequest();
              auto lambda = [this, self = shared_from_this(),
                buffer = std::move(buffer), change_password_request]()
              {
                const auto success = server_.db_.ChangePassword(
                  username_,
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
            }
            break;
        case CollabVmClientMessage::Message::CHAT_MESSAGE:
          {
            if (username_.empty())
            {
              break;
            }
            const auto chat_message = message.getChatMessage();
            const auto message_len = chat_message.getMessage().size();
            if (!message_len || message_len > max_chat_message_len)
            {
              break;
            }
            const auto destination =
              chat_message.getDestination().getDestination();
            switch (destination.which())
            {
            case CollabVmClientMessage::ChatMessageDestination::Destination::
            NEW_DIRECT:
              server_.guests_.dispatch([
                  this, self = shared_from_this(), buffer = std::move(
                    buffer),
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
                    this, self = shared_from_this(), buffer = std::move(buffer),
                    chat_message, destination
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
                      id, username_, chat_message.getMessage()));
                  });
                break;
              }
            case CollabVmClientMessage::ChatMessageDestination::Destination::VM:
              {
                const auto id = destination.getVm();
                auto send_message = [
                    this, self = shared_from_this(),
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
                  chat_room.AddUserMessage(chat_room_message, username_,
                                           chat_message.getMessage());
                  channel.GetClients(
                    [self = std::move(self), new_chat_message](auto& clients)
                    {
                      for (auto&& client_ptr : clients)
                      {
                        client_ptr->QueueMessage(new_chat_message);
                      }
                    });
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
                  send_message(*virtual_machine);
                });
              break;
            }
            default:
              break;
            }
            break;
          }
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
                this, self = TSocket::shared_from_this(),
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
                            session_id.data(), session_id.size()));
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
                std::optional<gsl::span<const std::byte, TOTP_KEY_LEN>>
                  totp_key;
                if (register_request.hasTwoFactorToken())
                {
                  auto two_factor_token = register_request.
                    getTwoFactorToken();
                  if (two_factor_token.size() != Database::
                    totp_token_len)
                  {
                    registration_result.setErrorStatus(
                      CollabVmServerMessage::RegisterAccountResponse::
                      RegisterAccountError::TOTP_ERROR);
                    QueueMessage(std::move(response));
                    return;
                  }
                  totp_key =
                    gsl::as_bytes(gsl::make_span(
                      reinterpret_cast<const capnp::byte(&)[TOTP_KEY_LEN
                      ]>(
                        *two_factor_token.begin())));
                }
                const auto register_result = server_.db_.CreateAccount(
                  username,
                  register_request.
                  getPassword(),
                  totp_key,
                  TSocket::
                  GetIpAddress().
                  AsBytes());
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
                      session_id.data(),
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
              settings.UpdateServerSettings<ServerSetting>(
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
                  io_context_, vm_id, initial_settings);
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
              SendAdminVmList(virtual_machines);
            });
          break;
        case CollabVmClientMessage::Message::READ_VMS:
          if (is_admin_)
          {
            server_.virtual_machines_.dispatch([ this,
                self = shared_from_this() ](auto& virtual_machines)
              {
                SendAdminVmList(virtual_machines);
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
              server_.db_.RemoveVm(vm_id);
              SendAdminVmList(virtual_machines);
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
                capnp::Data::Reader(id->data(), id->size()));
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
            server_.db_.ReadInvites([&response](auto& invites)
            {
              auto invites_list =
                response.initReadInvitesResponse(invites.size());
              auto invites_list_it = invites_list.begin();
              auto invites_it = invites.begin();
              for (; invites_list_it != invites_list.end();
                     invites_list_it++, invites_it++)
              {
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
            server_.db_.ReadReservedUsernames([&response](auto& usernames)
            {
              auto usernames_list =
                response.initReadReservedUsernamesResponse(usernames.size());
              auto usernames_list_it = usernames_list.begin();
              auto usernames_it = usernames.begin();
              for (; usernames_list_it != usernames_list.end();
                     usernames_list_it++, usernames_it++)
              {
                auto username = usernames_it->Username;
                *usernames_list_it = {&*username.begin(), username.length()};
              }
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
        default:
          TSocket::Close();
        }
      }

    private:
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

      // TODO: Remove template
      template<typename TVmList>
      void SendAdminVmList(TVmList& virtual_machines)
      {
        auto& message_builder = virtual_machines.GetAdminVirtualMachineInfo();
        auto socket_message =
          SocketMessage::CopyFromMessageBuilder(message_builder);
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
          GetBuffers(frame_builder_);
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
          const auto& buffers = socket_message.GetBuffers(frame_builder_);
          std::copy(buffers.begin(), buffers.end(), std::back_inserter(segment_buffers));
          queue.pop();
        } while (!queue.empty());

        TSocket::WriteMessage(
          segment_buffers,
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
      void QueueMessage(std::shared_ptr<SocketMessage>&& socket_message)
      {
        send_queue_.dispatch([
            this, self = shared_from_this(),
            socket_message =
            std::forward<std::shared_ptr<SocketMessage>>(socket_message)
          ](auto& send_queue) mutable
          {
            if (sending_)
            {
              send_queue.push(socket_message);
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
              send_queue.push(socket_message);
            });
            if (!sending_)
            {
              sending_ = true;
              SendMessageBatch(std::move(self), send_queue);
            }
          });
      }
    private:
      void OnDisconnect() override { LeaveServerConfig(); }

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
            this, continuation = std::move(continuation)
        ](auto& guests)
        {
          auto num = server_.guest_rng_(server_.rng_);
          auto username = "guest" + std::to_string(num);
          // Increment the number until a username is found that is not taken
          auto this_ptr = shared_from_this();
          while (!std::get<bool>(guests.insert({ username, this_ptr })))
          {
            username = "guest" + std::to_string(++num);
          }
          username_ = std::move(username);
          continuation();
        });
      }

      CollabVmServer& server_;
      boost::asio::io_context& io_context_;
      CapnpMessageFrameBuilder<> frame_builder_;
      StrandGuard<std::queue<std::shared_ptr<SocketMessage>>> send_queue_;
      bool sending_;
      StrandGuard<std::unordered_map<
        std::uint32_t,
        std::pair<std::shared_ptr<CollabVmSocket>, std::uint32_t>>>
      chat_rooms_;
      std::uint32_t chat_rooms_id_;

      std::vector<std::uint8_t> totp_key_;
      bool is_logged_in_ = false;
      bool is_admin_ = false;
      bool is_viewing_server_config = false;
      bool is_viewing_vm_list_ = false;
      std::string username_;
      friend class CollabVmServer;
    };

    CollabVmServer(const std::string& doc_root, const std::uint8_t threads)
      : TServer(doc_root, threads),
        settings_(TServer::io_context_, db_),
        sessions_(TServer::io_context_),
        guests_(TServer::io_context_),
        ssl_ctx_(boost::asio::ssl::context::sslv23),
        recaptcha_(TServer::io_context_, ssl_ctx_),
        virtual_machines_(TServer::io_context_,
                          TServer::io_context_,
                          db_, *this),
        login_strand_(TServer::io_context_),
        global_chat_room_(
          TServer::io_context_,
          TServer::io_context_,
          global_channel_id),
        guest_rng_(1'000, 99'999)
    {
      settings_.dispatch([this](auto& settings)
      {
        const auto recaptcha_key = 
          settings.GetServerSetting(ServerSetting::Setting::RECAPTCHA_KEY)
                .getRecaptchaKey();
        recaptcha_.SetRecaptchaKey(recaptcha_key);
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

          //testing
          virtual_machines.UpdateVirtualMachineInfoList();
        });
      }
      TServer::Start(host, port);
    }

  protected:
    std::shared_ptr<typename TServer::TSocket> CreateSocket(
      boost::asio::io_context& io_context,
      const boost::filesystem::path& doc_root) override
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
            db_.CreateSession(username, socket->GetIpAddress().AsBytes());
          if (correct_username.empty())
          {
            // TODO: Handle error
            return;
          }
          socket->username_ = std::move(correct_username);
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
          callback(socket->username_, new_session_id);
        });
    }

    void SendVmList(CollabVmSocket<typename TServer::TSocket>& socket)
    {
      auto message_builder = new capnp::MallocMessageBuilder();
      auto vm_info_list =
        message_builder->initRoot<CollabVmServerMessage::Message>()
                       .initVmListResponse(1);
      vm_info_list[0].setName("asdf");
      vm_info_list[0].setOperatingSystem("Windows XP");
      vm_info_list[0].setHost("collabvm.com");
      //    auto vm_list = CreateCopiedSocketMessage()
      //    socket.QueueMessage(std::move(socket_message));
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

      template <typename TList>
      void UpdateServerSettings(
        const typename capnp::List<TList>::Reader updates)
      {
        auto message_builder = std::make_unique<capnp::MallocMessageBuilder>();
        auto list = InitSettings(*message_builder);
        Database::UpdateList<TList>(settings_list_, list, updates);
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

    template <typename TClient>
    struct UserChannel
    {
      explicit UserChannel(boost::asio::io_context& io_context,
                           const std::uint32_t id) :
        clients_(io_context),
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

      template<typename TCallback>
      void GetClients(TCallback&& callback)
      {
        clients_.dispatch(std::move(callback));
      }

      void AddClient(const std::shared_ptr<TClient> client)
      {
        clients_.dispatch([client = std::move(client)](auto& clients)
        {
          clients.emplace_back(client);
        });
      }

      std::uint32_t GetId() const
      {
        return chat_room_.GetId();
      }

    private:
      StrandGuard<std::vector<std::shared_ptr<TClient>>> clients_;
      CollabVmChatRoom<Socket, CollabVm::Shared::max_username_len,
                       max_chat_message_len> chat_room_;
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

      // std::shared_ptr<AdminVirtualMachine<TClient>> admin_vm_info;
    };

    template <typename TClient>
    struct VirtualMachinesList;

    template <typename TClient>
    struct AdminVirtualMachine final : UserChannel<TClient>
    {
      AdminVirtualMachine(boost::asio::io_context& io_context,
                          const std::uint32_t id,
                          CollabVmServer& server,
                          capnp::List<VmSetting>::Reader initial_settings,
                          CollabVmServerMessage::AdminVmInfo::Builder admin_vm_info)
                         : UserChannel<TClient>(io_context, id),
                           state_(io_context,
                             InitVmSettingsData(
                               GetInitialSettings(initial_settings),
                               admin_vm_info)),
                           guacamole_client_(io_context, *this),
                           server_(server)
      {
      }

      template<typename TSettingProducer>
      AdminVirtualMachine(boost::asio::io_context& io_context,
                          const std::uint32_t id,
                          CollabVmServer& server,
                          TSettingProducer&& get_setting,
                          CollabVmServerMessage::AdminVmInfo::Builder admin_vm_info)
                         : UserChannel<TClient>(io_context, id),
                           state_(io_context,
                             InitVmSettingsData(
                               GetInitialSettings(get_setting),
                               admin_vm_info)),
                           guacamole_client_(io_context, *this),
                           server_(server)
      {
      }

      struct VmSettingsData
      {
        VmSettingsData(
          std::unique_ptr<capnp::MallocMessageBuilder>&& message_builder,
          capnp::List<VmSetting>::Builder settings)
          : message_builder_(std::move(message_builder)),
            settings_(settings)
        {
        }

        VmSetting::Setting::Reader GetSetting(
          const VmSetting::Setting::Which setting)
        {
          return settings_[setting].getSetting();
        }

        bool active_ = false;
        bool connected_ = false;
        std::size_t viewer_count_ = 0;
        std::unique_ptr<capnp::MallocMessageBuilder> message_builder_;
        capnp::List<VmSetting>::Builder settings_;
      };

      VmSettingsData InitVmSettingsData(
        VmSettingsData vm_settings_data,
        CollabVmServerMessage::AdminVmInfo::Builder admin_vm_info)
      {
        SetAdminVmInfo(vm_settings_data, admin_vm_info);
        return vm_settings_data;
      }

      template<typename TSettingProducer>
      static VmSettingsData GetInitialSettings(TSettingProducer&& get_setting)
      {
        auto message_builder = std::make_unique<capnp::MallocMessageBuilder>();
        const auto fields =
          capnp::Schema::from<VmSetting::Setting>().getUnionFields();
        auto settings = message_builder->initRoot<CollabVmServerMessage>()
                                       .initMessage()
                                       .initReadVmConfigResponse(fields.size());
        for (auto i = 0u; i < fields.size(); i++) {
          auto dynamic_setting =
            capnp::DynamicStruct::Builder(settings[i].getSetting());
          dynamic_setting.clear(fields[i]);
        }
        auto setting = std::invoke_result_t<TSettingProducer>();
        while (setting = get_setting())
        {
          const auto which = setting->which();
          const auto field = fields[which];
          const auto value = capnp::DynamicStruct::Reader(*setting).get(field);
          auto dynamic_setting =
            capnp::DynamicStruct::Builder(settings[which].getSetting());
          dynamic_setting.set(field, value);
        }
        return VmSettingsData(std::move(message_builder), settings);
      }

      static VmSettingsData GetInitialSettings(
        capnp::List<VmSetting>::Reader initial_settings)
      {
        auto message_builder = std::make_unique<capnp::MallocMessageBuilder>();
        auto fields = capnp::Schema::from<VmSetting::Setting>().getUnionFields();
        if (initial_settings.size() == fields.size())
        {
          auto message =
            message_builder->initRoot<CollabVmServerMessage>().initMessage();
          message.setReadVmConfigResponse(initial_settings);
          return VmSettingsData(
            std::move(message_builder), message.getReadVmConfigResponse());
        }
        auto settings = message_builder->initRoot<CollabVmServerMessage>()
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
        return VmSettingsData(std::move(message_builder), settings);
      }

      void Start()
      {
        state_.dispatch([this](auto& settings)
        {
          settings.active_ = true;
          const auto params = settings.GetSetting(VmSetting::Setting::GUACAMOLE_PARAMETERS)
                        .getGuacamoleParameters();
          auto params_map = std::unordered_map<std::string_view, std::string_view>(params.size());
          std::transform(params.begin(), params.end(),
                         std::inserter(params_map, params_map.end()),
            [](auto param)
            {
              return std::pair(param.getName().cStr(), param.getValue().cStr());
            });
          const auto protocol =
            settings.GetSetting(VmSetting::Setting::PROTOCOL).getProtocol();
          if (protocol == VmSetting::Protocol::RDP)
          {
            guacamole_client_.StartRDP(params_map);
          }
          else if (protocol == VmSetting::Protocol::VNC)
          {
            guacamole_client_.StartVNC(params_map);
          }
        });
      }

      void Stop()
      {
        state_.dispatch([this](auto& settings)
        {
          settings.active_ = false;
          guacamole_client_.Stop();
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
      void SetVmInfo(
        CollabVmServerMessage::AdminVmInfo::Builder admin_vm_info,
        CollabVmServerMessage::VmInfo::Builder vm_info,
        TSetVmInfo&& set_vm_info)
      {
        state_.dispatch([this, admin_vm_info, vm_info,
          set_vm_info = std::forward<TSetVmInfo>(set_vm_info)](auto& state) mutable
        {
          SetAdminVmInfo(state, admin_vm_info);
          if (!state.active_)
          {
            set_vm_info(false, std::optional<std::vector<std::byte>>());
            return;
          }

          vm_info.setId(UserChannel<TClient>::GetId());
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
          const auto created_screenshot =
            guacamole_client_.CreateScreenshot([&png](auto png_bytes)
            {
              png.insert(png.end(), png_bytes.begin(), png_bytes.end());
            });
          set_vm_info(true,
            created_screenshot ? std::optional(std::move(png)) : std::nullopt);
        });
        UserChannel<TClient>::GetClients(
          [this](auto& clients)
          {
            state_.dispatch(
              [this, clients_count = clients.size()](
                auto& data)
              {
                data.viewer_count_ = clients_count;
              });
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
          continuation = std::forward<TContinuation>(continuation)](auto& data) mutable
        {
          auto current_settings = data.settings_;
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
            db.UpdateVmSettings(UserChannel<TClient>::GetId(), modified_settings);
            ApplySettings(new_settings, current_settings);
            data.settings_ = current_settings = new_settings;
            data.message_builder_ = std::move(message_builder);
          }

          continuation(valid);
        });
      }

    private:
      friend struct CollabVmGuacamoleClient<AdminVirtualMachine>;

      template<typename TCallback>
      void GetAdminVmInfo(TCallback&& callback)
      {
        server_.virtual_machines_.dispatch(
          [this, callback = std::move(callback)](auto& virtual_machines)
          {
            const auto id = UserChannel<TClient>::GetId();
            auto& admin_virtual_machine_info = virtual_machines
              .GetAdminVirtualMachineInfo();
            auto admin_vm_info_list = admin_virtual_machine_info
              .getRoot<CollabVmServerMessage>()
              .getMessage()
              .getReadVmsResponse();
            for (auto vm_info : admin_vm_info_list)
            {
              if (vm_info.getId() != id) {
                continue;
              }
              const auto update = callback(vm_info);
              if (update)
              {
                auto admin_vm_list_message =
                  SocketMessage::CopyFromMessageBuilder(
                    admin_virtual_machine_info);
                virtual_machines.BroadcastToViewingAdmins(
                  admin_vm_list_message);
              }
              return;
            }
        });
      }

      void OnStart()
      {
        state_.dispatch([this](auto& settings)
        {
          if (!settings.active_)
          {
            guacamole_client_.Stop();
            return;
          }
          settings.connected_ = true;
          GetAdminVmInfo([this](auto vm_info)
          {
            vm_info.setStatus(CollabVmServerMessage::VmStatus::RUNNING);
            return true;
          });
          UserChannel<TClient>::GetClients([this](auto& clients)
          {
            guacamole_client_.AddUser(
              [&clients](capnp::MallocMessageBuilder&& message_builder)
              {
                auto socket_message =
                  SocketMessage::CopyFromMessageBuilder(message_builder);
                for (auto& client : clients)
                {
                  client->QueueMessage(socket_message);
                }
              });
          });
        });
      }

      void OnStop()
      {
        state_.dispatch([this](auto& settings)
          {
            settings.connected_ = false;
            if (!settings.active_)
            {
              return;
            }
            GetAdminVmInfo([this](auto vm_info)
            {
              vm_info.setStatus(CollabVmServerMessage::VmStatus::STARTING);
              return true;
            });
            const auto protocol =
              settings.GetSetting(VmSetting::Setting::PROTOCOL).getProtocol();
            if (protocol == VmSetting::Protocol::RDP)
            {
              guacamole_client_.StartRDP();
            }
            else if (protocol == VmSetting::Protocol::VNC)
            {
              guacamole_client_.StartVNC();
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

      void ApplySettings(capnp::List<VmSetting>::Reader settings,
                         std::optional<capnp::List<VmSetting>::Reader>
                         previous_settings = {})
      {
        if (settings[VmSetting::Setting::Which::TURNS_ENABLED]
            .getSetting().getTurnsEnabled() && true)
        {
        }
      }

      void SetAdminVmInfo(
        VmSettingsData& settings,
        CollabVmServerMessage::AdminVmInfo::Builder admin_vm_info)
      {
        admin_vm_info.setId(UserChannel<TClient>::GetId());
        admin_vm_info.setName(settings.GetSetting(VmSetting::Setting::NAME).getName());
        admin_vm_info.setStatus(CollabVmServerMessage::VmStatus::STOPPED);
      }

      StrandGuard<VmSettingsData> state_;
      CollabVmGuacamoleClient<AdminVirtualMachine> guacamole_client_;
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
        auto admin_vm_list_message_builder = std::make_unique<capnp::MallocMessageBuilder>();
        std::unordered_map<std::uint32_t, std::shared_ptr<VirtualMachine<TClient
                           >>> virtual_machines;
        auto admin_virtual_machines =
          std::unordered_map<std::uint32_t, std::shared_ptr<AdminVm>>();
        db.ReadVirtualMachines(
          [this, &io_context, &virtual_machines, &admin_virtual_machines,
            &admin_vm_list_message_builder=*admin_vm_list_message_builder](
          const auto total_virtual_machines,
          auto& vm_settings)
          {
            auto admin_virtual_machines_list = admin_vm_list_message_builder
                                          .initRoot<CollabVmServerMessage>()
                                          .initMessage()
                                          .initReadVmsResponse(
                                            total_virtual_machines);
            auto it = vm_settings.begin();
            auto admin_vm_info_it = admin_virtual_machines_list.begin();
            auto setting_data = std::vector<std::uint8_t>();
            while (it != vm_settings.end())
            {
              const auto vm_id = it->IDs.VmId;
              auto admin_vm = std::make_shared<AdminVm>(
                io_context, vm_id, server_,
                [&vm_settings, it, &setting_data, vm_id]() mutable
                {
                  if (it == vm_settings.end() || it->IDs.VmId != vm_id)
                  {
                    return std::optional<VmSetting::Setting::Reader>();
                  }
                  // The setting data must be kept alive by
                  // storing it outside of this lambda
                  setting_data = std::move(it->Setting);
                  it++;
                  const auto db_setting =
                    capnp::readMessageUnchecked<VmSetting::Setting>(
                      reinterpret_cast<const capnp::word*>(setting_data.data()));
                  return std::optional(db_setting);
                },
                *admin_vm_info_it++);
              admin_virtual_machines.emplace(vm_id, std::move(admin_vm));
            }
          });
			  admin_vm_info_list_ = ResizableList<InitAdminVmInfo>(std::move(admin_vm_list_message_builder));
			  virtual_machines_ = std::move(virtual_machines);
			  admin_virtual_machines_ = std::move(admin_virtual_machines);
      }

      capnp::MallocMessageBuilder& GetMessageBuilder()
      {
        return vm_info_list_.GetMessageBuilder();
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

      capnp::MallocMessageBuilder& GetAdminVirtualMachineInfo() const
      {
        return admin_vm_info_list_.GetMessageBuilder();
      }

      const auto& GetVirtualMachinesMap() const
      {
        return virtual_machines_;
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

      void AddVmListViewer(std::shared_ptr<TClient>&& viewer_ptr)
      {
        auto& viewer = *viewer_ptr;
        vm_list_viewers_.emplace_back(
          std::forward<std::shared_ptr<TClient>>(viewer_ptr));
        auto vm_list_message = SocketMessage::CopyFromMessageBuilder(
          vm_info_list_.GetMessageBuilder());
        viewer.QueueMessage(std::move(vm_list_message));
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
        auto admin_vm_list_message = SocketMessage::CopyFromMessageBuilder(
          admin_vm_info_list_.GetMessageBuilder());
        viewer.QueueMessage(std::move(admin_vm_list_message));
      }

      void BroadcastToViewingAdminsExcluding(
        const std::shared_ptr<CopiedSocketMessage>& message,
        const std::shared_ptr<TClient>& exclude)
      {
        if (admin_vm_list_viewers_.empty() ||
            admin_vm_list_viewers_.size() == 1 &&
            admin_vm_list_viewers_.front() == exclude)
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

      void BroadcastToViewingAdmins(
        const std::shared_ptr<CopiedSocketMessage>& message) {
        std::for_each(
          admin_vm_list_viewers_.begin(), admin_vm_list_viewers_.end(),
          [&message](auto& viewer)
          {
            viewer->QueueMessage(message);
          });
      }

      void RemoveAdminVmListViewer(const std::shared_ptr<TClient> viewer)
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

      std::size_t pending_vm_info_requests_ = 0;
      std::size_t pending_vm_info_updates_ = 0;

      void UpdateVirtualMachineInfoList()
      {
        if (pending_vm_info_requests_)
        {
          // An update is already pending
          return;
        }
        pending_vm_info_requests_ = admin_virtual_machines_.size();
        pending_vm_info_updates_ = 0;
        for (auto& [id, vm] : admin_virtual_machines_)
        {
          auto message_builder =
            std::make_unique<capnp::MallocMessageBuilder>();
          auto admin_vm_info = message_builder->getOrphanage()
            .newOrphan<CollabVmServerMessage::AdminVmInfo>();
          auto vm_info = message_builder->getOrphanage()
            .newOrphan<CollabVmServerMessage::VmInfo>();
          vm->vm.SetVmInfo(admin_vm_info.get(), vm_info.get(),
            server_.virtual_machines_.wrap(
              [this, vm, message_builder = std::move(message_builder),
                admin_vm_info = std::move(admin_vm_info),
                vm_info = std::move(vm_info)]
              (auto&, auto set_vm_info, auto thumbnail) mutable
              {
                if (thumbnail.has_value())
                {
                  return;
                }
                vm->pending_vm_info_message_builder = std::move(message_builder);
                vm->pending_admin_vm_info = std::move(admin_vm_info);
                if (set_vm_info)
                {
                  vm->pending_vm_info = std::move(vm_info);
                  pending_vm_info_updates_++;
                }
                if (--pending_vm_info_requests_)
                {
                  return;
                }
                admin_vm_info_list_.Reset(admin_virtual_machines_.size());
                auto admin_vm_info_list =
                  typename decltype(admin_vm_info_list_)::List::GetList(
                    admin_vm_info_list_.GetMessageBuilder());
                vm_info_list_.Reset(pending_vm_info_updates_);
                auto vm_info_list =
                  typename decltype(vm_info_list_)::List::GetList(
                    vm_info_list_.GetMessageBuilder());
                auto admin_vm_info_list_index = 0u;
                auto vm_info_list_index       = 0u;
                for (auto& [id, admin_vm] : admin_virtual_machines_)
                {
                  if (!admin_vm->pending_vm_info_message_builder)
                  {
                    continue;
                  }
                  admin_vm_info_list.setWithCaveats(
                    admin_vm_info_list_index++,
                    admin_vm->pending_admin_vm_info.get());
                  if (admin_vm->pending_vm_info != nullptr)
                  {
                    vm_info_list.setWithCaveats(
                      vm_info_list_index++, admin_vm->pending_vm_info.get());
                  }
                  admin_vm->pending_vm_info_message_builder.reset();
                }

                auto vm_list_message = SocketMessage::CopyFromMessageBuilder(
                  vm_info_list_.GetMessageBuilder());
                std::for_each(vm_list_viewers_.begin(), vm_list_viewers_.end(),
                  [&vm_list_message](auto& viewer)
                  {
                    viewer->QueueMessage(vm_list_message);
                  });
                auto admin_vm_list_message = SocketMessage::CopyFromMessageBuilder(
                  admin_vm_info_list_.GetMessageBuilder());
                BroadcastToViewingAdmins(admin_vm_list_message);
              }));
        }
      }

      void UpdateVirtualMachineInfo(AdminVirtualMachine<TClient>& vm) {
        const auto vm_id = vm.GetId();
        auto message_builder =
          std::make_unique<capnp::MallocMessageBuilder>();
        auto admin_vm_info = message_builder->getOrphanage()
          .newOrphan<CollabVmServerMessage::AdminVmInfo>();
        auto vm_info = message_builder->getOrphanage()
          .newOrphan<CollabVmServerMessage::VmInfo>();
        vm.SetVmInfo(admin_vm_info.get(), vm_info.get(),
          server_.virtual_machines_.wrap(
            [this, vm_id, message_builder = std::move(message_builder),
              admin_vm_info = std::move(admin_vm_info),
              vm_info = std::move(vm_info)]
            (auto&, auto set_vm_info, auto thumbnail) mutable
          {
            auto vm_data = admin_virtual_machines_[vm_id];
            if (vm_data->pending_vm_info_message_builder)
            {
              // A bulk update is already in progress
              vm_data->pending_vm_info_message_builder =
                std::move(message_builder);
              vm_data->pending_admin_vm_info = std::move(admin_vm_info);
              vm_data->pending_vm_info = std::move(vm_info);
              return;
            }
            admin_vm_info_list_.UpdateElement(
              [vm_id](auto vm_info)
              {
                return vm_info.getId() == vm_id;
              }, admin_vm_info.get());
            auto admin_vm_list_message = SocketMessage::CopyFromMessageBuilder(
              admin_vm_info_list_.GetMessageBuilder());
            BroadcastToViewingAdmins(admin_vm_list_message);

            if (!set_vm_info)
            {
              return;
            }
            vm_info_list_.UpdateElement(
              [vm_id](auto vm_info)
              {
                return vm_info.getId() == vm_id;
              }, vm_info.get());
            auto vm_list_message = SocketMessage::CopyFromMessageBuilder(
              vm_info_list_.GetMessageBuilder());
            std::for_each(vm_list_viewers_.begin(), vm_list_viewers_.end(),
                          [&vm_list_message](auto& viewer)
                          {
                            viewer->QueueMessage(vm_list_message);
                          });
          }));
      }

      template <typename TFunction>
      struct ResizableList
      {
        using List = TFunction;
        explicit ResizableList(
          std::unique_ptr<capnp::MallocMessageBuilder> message_builder) :
          message_builder_(std::move(message_builder)),
          list_(TFunction::GetList(*message_builder_))
        {
        }

        ResizableList() :
          message_builder_(std::make_unique<capnp::MallocMessageBuilder>()),
          list_(TFunction::InitList(*message_builder_, 0))
        {
        }

        auto Add()
        {
          auto message_builder =
            std::make_unique<capnp::MallocMessageBuilder>();
          auto vm_list = TFunction::InitList(*message_builder, list_.size() + 1);
          std::copy(list_.begin(), list_.end(), vm_list.begin());
          list_ = vm_list;
          message_builder_ = std::move(message_builder);
          return vm_list[vm_list.size() - 1];
        }

        template<typename TPredicate>
        void RemoveFirst(TPredicate&& predicate)
        {
          auto message_builder =
            std::make_unique<capnp::MallocMessageBuilder>();
          auto vm_list = TFunction::InitList(*message_builder, list_.size() - 1);
          const auto copy_end =
            std::remove_copy_if(list_.begin(), list_.end(),
                                vm_list.begin(), std::move(predicate));
          assert(copy_end == vm_list.end());
          list_ = vm_list;
          message_builder_ = std::move(message_builder);
        }

        template<typename TPredicate, typename TNewElement>
        void UpdateElement(TPredicate&& predicate, TNewElement new_element)
        {
          auto message_builder =
            std::make_unique<capnp::MallocMessageBuilder>();
          const auto size = list_.size();
          auto new_vm_list = TFunction::InitList(*message_builder, size);
          for (auto i = 0u; i < size; i++)
          {
            new_vm_list.setWithCaveats(
              i, predicate(list_[i]) ? new_element : list_[i]);
          }
          list_ = new_vm_list;
          message_builder_ = std::move(message_builder);
        }

        void Reset(unsigned capacity)
        {
          list_ = TFunction::InitList(*message_builder_, capacity);
        }

        capnp::MallocMessageBuilder& GetMessageBuilder() const
        {
          return *message_builder_;
        }

      private:
        std::unique_ptr<capnp::MallocMessageBuilder> message_builder_;
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

      std::unordered_map<std::uint32_t,
        std::shared_ptr<VirtualMachine<TClient>>> virtual_machines_;

      struct AdminVm
      {
        template<typename... TArgs>
        explicit AdminVm(TArgs&&... args)
          : vm(std::forward<TArgs>(args)...)
        {
        }
        AdminVirtualMachine<TClient> vm;
        std::unique_ptr<capnp::MallocMessageBuilder> pending_vm_info_message_builder;
        capnp::Orphan<CollabVmServerMessage::AdminVmInfo> pending_admin_vm_info;
        capnp::Orphan<CollabVmServerMessage::VmInfo> pending_vm_info;
      };
      std::unordered_map<
        std::uint32_t, std::shared_ptr<AdminVm>> admin_virtual_machines_;
      CollabVmServer& server_;
      std::vector<std::shared_ptr<TClient>> vm_list_viewers_;
      std::vector<std::shared_ptr<TClient>> admin_vm_list_viewers_;
    };

    Database db_;
    StrandGuard<ServerSettingsList> settings_;
    using SessionMap = std::unordered_map<Database::SessionId,
                                          std::shared_ptr<Socket>,
                                          Database::SessionIdHasher>;
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
    StrandGuard<UserChannel<Socket>> global_chat_room_;
    std::uniform_int_distribution<std::uint32_t> guest_rng_;
    std::default_random_engine rng_;
  };
} // namespace CollabVm::Server
