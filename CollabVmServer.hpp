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
#include "Database/Database.h"
#include "Recaptcha.hpp"
#include "StrandGuard.hpp"
#include "Totp.hpp"
#include "VmListProvider.hpp"

namespace CollabVm::Server
{
  template <typename TServer, typename TVmListProvider=VmListProvider>
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
            auto socket_message = CreateSharedSocketMessage();
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
                auto socket_message = CreateSharedSocketMessage();
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
                auto socket_message = CreateSharedSocketMessage();
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
                                  CreateSharedSocketMessage();
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
                  auto new_chat_message = CreateSharedSocketMessage();
                  auto chat_room_message =
                    new_chat_message->GetMessageBuilder()
                                    .initRoot<CollabVmServerMessage>()
                                    .initMessage()
                                    .initChatMessage();
                  chat_room.AddUserMessage(chat_room_message, username_,
                                           chat_message.getMessage());
                  for (auto&& client_ptr : channel.GetClients()
                  )
                  {
                    client_ptr->QueueMessage(new_chat_message);
                  }
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
              [this, self = shared_from_this()](auto& virtual_machines)
              {
                QueueMessage(
                  CreateCopiedSocketMessage(
                    virtual_machines.GetMessageBuilder()));
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
                auto socket_message = CreateSharedSocketMessage();
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
                auto response = CreateSharedSocketMessage();
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
                QueueMessage(CreateCopiedSocketMessage(
                  settings.GetServerSettingsMessageBuilder()));
              });
            if (!is_viewing_server_config)
            {
              is_viewing_server_config = true;
              server_.viewing_admins_.dispatch([self = shared_from_this()](
                auto& viewing_admins)
                {
                  viewing_admins.emplace_back(std::move(self));
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
              auto config_message = CreateCopiedSocketMessage(
                settings.GetServerSettingsMessageBuilder());
              // Broadcast the config changes to all other admins viewing the
              // admin panel
              server_.viewing_admins_.dispatch([
                self = std::move(self),
                  config_message = std::move(config_message)
              ](auto& viewing_admins)
                {
                  if (viewing_admins.empty() ||
                    viewing_admins.size() == 1 &&
                    viewing_admins.front() == self)
                  {
                    return;
                  }
                  // TODO: Is moving this safe?
                  for (auto& admin : viewing_admins)
                  {
                    if (admin != self)
                    {
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
              auto virtual_machine =
                virtual_machines.AddAdminVirtualMachine(vm_id, initial_settings);
              server_.db_.CreateVm(vm_id, virtual_machine->GetSettings());

              auto socket_message = CreateSharedSocketMessage();
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
                auto& message_builder = admin_virtual_machine->
                                       GetMessageBuilder();
                QueueMessage(CreateCopiedSocketMessage(message_builder));
              });
          }
          break;
        case CollabVmClientMessage::Message::UPDATE_VM_CONFIG:
          if (!is_admin_)
          {
            break;
          }
          server_.virtual_machines_.dispatch(
            [this, self = shared_from_this(), buffer = std::move(buffer)
              , message](auto& virtual_machines)
            {
              const auto modified_vm = message.getUpdateVmConfig();
              const auto vm_id = modified_vm.getId();
              const auto virtual_machine =
                virtual_machines.GetAdminVirtualMachine(vm_id);
              const auto modified_settings = modified_vm.
                getModifications();
              /*
              const auto guac_params = std::find_if(
                modified_settings.begin(), modified_settings.end(), [](auto x)
                {
                  return x.which() == VmSetting::Setting::GUACAMOLE_PARAMETERS;
                });
              const auto num_guac_params = (*guac_params).getSetting();// .getGuacamoleParameters().size();
              */
              virtual_machine->UpdateSettings(server_.db_, modified_settings);
              virtual_machine->UpdateAdminVmInfo(virtual_machines.admin_vm_info_list_);
              SendAdminVmList(virtual_machines);
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
        case CollabVmClientMessage::Message::CREATE_INVITE:
          if (is_admin_)
          {
            auto invite = message.getCreateInvite();
            auto socket_message = CreateSharedSocketMessage();
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
            auto socket_message = CreateSharedSocketMessage();
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
            auto socket_message = CreateSharedSocketMessage();
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
            auto socket_message = CreateSharedSocketMessage();
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
        auto socket_message = CreateSharedSocketMessage();
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
        auto socket_message = CreateSharedSocketMessage();
        socket_message->GetMessageBuilder()
                      .initRoot<CollabVmServerMessage>()
                      .initMessage()
                      .setChatMessageResponse(result);
        QueueMessage(std::move(socket_message));
      }

      template<typename TVmList>
      void SendAdminVmList(TVmList& virtual_machines)
      {
        auto& message_builder = virtual_machines.GetAdminVirtualMachineInfo();
        auto admin_vms = message_builder.getRoot<CollabVmServerMessage>().getMessage().getReadVmsResponse();
        const auto count = admin_vms.size();
        auto socket_message = CreateCopiedSocketMessage(
          message_builder);
        QueueMessage(socket_message);
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

      struct SocketMessage : std::enable_shared_from_this<SocketMessage>
      {
      protected:
        ~SocketMessage() = default;

      public:
        virtual std::vector<boost::asio::const_buffer>& GetBuffers(
          CapnpMessageFrameBuilder<>&) = 0;
      };

    struct CopiedSocketMessage final : SocketMessage {
      virtual ~CopiedSocketMessage() = default;

      CopiedSocketMessage(capnp::MallocMessageBuilder& message_builder)
          : buffer_(capnp::messageToFlatArray(message_builder)),
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
        capnp::MallocMessageBuilder& message_builder)
      {
        return std::make_shared<CopiedSocketMessage>(message_builder);
      }

      struct SharedSocketMessage final : SocketMessage
      {
        std::vector<boost::asio::const_buffer>& GetBuffers(
          CapnpMessageFrameBuilder<>& frame_builder) override
        {
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
            [&frame_builder](const kj::ArrayPtr<const capnp::word> a)
            {
              frame_builder.AddSegment(a.size());
              return boost::asio::const_buffer(a.begin(),
                                               a.size() * sizeof(a[0]));
            });
          frame_builder.Finalize(segment_count);
          return framed_buffers_;
        }

        capnp::MallocMessageBuilder& GetMessageBuilder()
        {
          return shared_message_builder;
        }

        ~SharedSocketMessage() = default;
      private:
        capnp::MallocMessageBuilder shared_message_builder;
        std::vector<boost::asio::const_buffer> framed_buffers_;
      };

      static std::shared_ptr<SharedSocketMessage> CreateSharedSocketMessage()
      {
        return std::make_shared<SharedSocketMessage>();
      }

      void SendMessage(std::shared_ptr<CollabVmSocket>&& self,
                       std::shared_ptr<SocketMessage>&& socket_message)
      {
        const auto& segment_buffers = socket_message->
          GetBuffers(frame_builder_);
        TSocket::WriteMessage(
          segment_buffers,
          send_queue_.wrap([ this, self = std::move(self), socket_message ](
            auto& send_queue, const boost::beast::error_code& ec,
            std::size_t bytes_transferred) mutable
            {
              if (ec)
              {
                TSocket::Close();
                return;
              }
              if (send_queue.empty())
              {
                sending_ = false;
                return;
              }
              SendMessage(std::move(self), std::move(send_queue.front()));
              send_queue.pop();
            }));
      }

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

      void OnDisconnect() override { LeaveServerConfig(); }

      void LeaveServerConfig()
      {
        if (is_viewing_server_config)
        {
          is_viewing_server_config = false;
          server_.viewing_admins_.dispatch(
            [ this, self = shared_from_this() ](auto& viewing_admins)
            {
              const auto it = std::find(viewing_admins.cbegin(),
                                        viewing_admins.cend(), self);
              if (it != viewing_admins.cend())
              {
                viewing_admins.erase(it);
              }
            });
        }
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
        virtual_machines_(TServer::io_context_,
                          VirtualMachinesList<CollabVmSocket<typename TServer::
                            TSocket>>::CreateVirtualMachinesList(db_)),
        login_strand_(TServer::io_context_),
        viewing_admins_(TServer::io_context_),
        global_chat_room_(TServer::io_context_, global_channel_id),
        guest_rng_(1'000, 99'999)
    {
      /*
                      vm_settings_.dispatch([](auto& vm_settings)
                      {
                              db_.LoadServerSettings()
                      });
      */
      settings_.dispatch([](auto& settings)
      {
        settings.LoadServerSettings();
        settings.GetServerSetting(ServerSetting::Setting::RECAPTCHA_KEY)
                .getRecaptchaKey();
      });
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

    Database db_;

    struct ServerSettingsList
    {
      ServerSettingsList(Database& db)
        : db_(db),
          settings_(std::make_unique<capnp::MallocMessageBuilder>()),
          settings_list_(InitSettings(*settings_))
      {
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

      void LoadServerSettings() { db_.LoadServerSettings(settings_list_); }

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
      explicit UserChannel(const std::uint32_t id) : chat_room_(id)
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

      std::vector<std::shared_ptr<TClient>>& GetClients()
      {
        return clients_;
      }

      void AddClient(const std::shared_ptr<TClient>& client)
      {
        clients_.emplace_back(client);
      }

      std::uint32_t GetId() const
      {
        return chat_room_.GetId();
      }

    private:
      std::vector<std::shared_ptr<TClient>> clients_;
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
    struct AdminVirtualMachine final : UserChannel<TClient>
    {
      AdminVirtualMachine(const std::uint32_t id,
                          const CollabVmServerMessage::VmInfo::Builder
                          vm_info) : UserChannel<TClient>(id),
                                     vm_info_(vm_info),
                                     message_builder_(
                                       std::make_unique<capnp::
                                         MallocMessageBuilder>()),
                                     settings_(
                                       message_builder_
                                       ->initRoot<CollabVmServerMessage>()
                                       .initMessage()
                                       .initReadVmConfigResponse(
                                         capnp::Schema::from<VmSetting::Setting
                                         >().getUnionFields().size()))
      {
      }

      capnp::List<VmSetting>::Builder GetSettings()
      {
        return settings_;
      }

      VmSetting::Setting::Reader GetSetting(
        const VmSetting::Setting::Which setting)
      {
        return settings_[setting].getSetting();
      }

      void SetSetting(
        const VmSetting::Setting::Which setting,
        const capnp::StructSchema::Field field,
        const capnp::DynamicValue::Reader value)
      {
        capnp::DynamicStruct::Builder dynamic_server_setting = settings_[setting
        ].getSetting();
        dynamic_server_setting.set(field, value);
      }

      void SetInitialSettings(capnp::List<VmSetting>::Reader initial_settings)
      {
        message_builder_ = std::make_unique<capnp::MallocMessageBuilder>();
        auto fields = capnp::Schema::from<VmSetting::Setting>().getUnionFields();
        if (initial_settings.size() == fields.size())
        {
          auto message = message_builder_->initRoot<CollabVmServerMessage>().initMessage();
          message.setReadVmConfigResponse(initial_settings);
          settings_ = message.getReadVmConfigResponse();
          return;
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
        settings_ = settings;
      }

      void SetVmInfo(CollabVmServerMessage::VmInfo::Builder vm_info)
      {
        vm_info.setId(UserChannel<TClient>::GetId());
        vm_info.setName(GetSetting(VmSetting::Setting::NAME).getName());
        // vm_info.setHost();
        // vm_info.setAddress();
        vm_info.setOperatingSystem(GetSetting(VmSetting::Setting::OPERATING_SYSTEM).getOperatingSystem());
        vm_info.setUploads(GetSetting(VmSetting::Setting::UPLOADS_ENABLED).getUploadsEnabled());
        vm_info.setInput(GetSetting(VmSetting::Setting::TURNS_ENABLED).getTurnsEnabled());
        vm_info.setRam(GetSetting(VmSetting::Setting::RAM).getRam());
        vm_info.setDiskSpace(GetSetting(VmSetting::Setting::DISK_SPACE).getDiskSpace());
        vm_info.setSafeForWork(GetSetting(VmSetting::Setting::SAFE_FOR_WORK).getSafeForWork());
      }

      void SetAdminVmInfo(CollabVmServerMessage::AdminVmInfo::Builder vm_info) {
        vm_info.setId(UserChannel<TClient>::GetId());
        vm_info.setName(GetSetting(VmSetting::Setting::NAME).getName());
        vm_info.setStatus(CollabVmServerMessage::VmStatus::STOPPED);
      }

      template<typename TAdminVmList>
      void UpdateAdminVmInfo(TAdminVmList& admin_vm_info_list)
      {
        admin_vm_info_list.Transform(
          [this, id = UserChannel<TClient>::GetId()](auto source, auto destination)
          {
            if (source.getId() == id)
            {
              destination.setId(id);
              destination.setName(GetSetting(VmSetting::Setting::NAME).getName());
              destination.setStatus(CollabVmServerMessage::VmStatus::STOPPED);
            }
            else
            {
              destination = source;
            }
          });
      }

      capnp::MallocMessageBuilder& GetMessageBuilder()
      {
        return *message_builder_;
      }

      bool UpdateSettings(Database& db,
                          capnp::List<VmSetting>::Reader modified_settings)
      {
        auto message_builder = std::make_unique<capnp::MallocMessageBuilder>();
        const auto settings = message_builder->initRoot<CollabVmServerMessage>()
                                             .initMessage()
                                             .initReadVmConfigResponse(
                                               capnp::Schema::from<VmSetting::
                                                 Setting>()
                                               .getUnionFields().size());
        Database::UpdateList<VmSetting>(settings_.asReader(), settings,
                                        modified_settings);
        const auto valid = ValidateSettings(settings);
        if (valid)
        {
          db.UpdateVmSettings(UserChannel<TClient>::GetId(), modified_settings);
          ApplySettings(settings, settings_);
          settings_ = settings;
          message_builder_ = std::move(message_builder);
        }
        return valid;
      }

    private:
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

      CollabVmServerMessage::VmInfo::Builder vm_info_;
      std::unique_ptr<capnp::MallocMessageBuilder> message_builder_;
      capnp::List<VmSetting>::Builder settings_;
    };

    template <typename TClient>
    struct VirtualMachinesList
    {
      static VirtualMachinesList<TClient> CreateVirtualMachinesList(
        Database& db)
      {
        auto vm_list_message_builder = std::make_unique<capnp::
          MallocMessageBuilder>();
        auto admin_vm_list_message_builder = std::make_unique<capnp::MallocMessageBuilder>();
        std::unordered_map<std::uint32_t, std::shared_ptr<VirtualMachine<TClient
                           >>> virtual_machines;
        std::unordered_map<std::uint32_t, std::shared_ptr<AdminVirtualMachine<TClient
                           >>> admin_virtual_machines;
        db.ReadVirtualMachines([&virtual_machines, &admin_virtual_machines,
            &vm_list_message_builder=*vm_list_message_builder,
            &admin_vm_list_message_builder=*admin_vm_list_message_builder](
          const auto total_virtual_machines,
          auto& vm_settings)
          {
            auto admin_virtual_machines_list = admin_vm_list_message_builder
                                          .initRoot<CollabVmServerMessage>()
                                          .initMessage()
                                          .initReadVmsResponse(
                                            total_virtual_machines);
            auto virtual_machine_list = vm_list_message_builder
                                        .initRoot<CollabVmServerMessage>()
                                        .initMessage()
                                        .initVmListResponse(
                                          total_virtual_machines);
            if (vm_settings.begin() == vm_settings.end())
            {
              return;
            }
            auto fields = capnp::Schema::from<VmSetting::Setting>().
              getUnionFields();
            capnp::List<VmSetting>::Builder vm_config;
            auto vm_index = 0u;
            auto previous_vm_id = vm_settings.begin()->IDs.VmId;
            std::shared_ptr<AdminVirtualMachine<CollabVmSocket<typename TServer
              ::TSocket>>> admin_vm;
            for (auto& vm_setting : vm_settings)
            {
              if (!admin_vm)
              {
                admin_vm = std::make_shared<AdminVirtualMachine<CollabVmSocket<
                  typename TServer::TSocket>>>(vm_setting.IDs.VmId,
                                               virtual_machine_list[vm_index]);
                vm_config = admin_vm->GetSettings();
              }
              vm_index = vm_setting.IDs.VmId == previous_vm_id ? vm_index : vm_index + 1;
              previous_vm_id = vm_index;
              const auto db_setting = capnp::readMessageUnchecked<VmSetting::
                Setting>(
                reinterpret_cast<const capnp::word*>(vm_setting.Setting.data()));
              const auto field = fields[vm_setting.IDs.SettingID];
              admin_vm->SetSetting(db_setting.which(), field,
                                   static_cast<const capnp::DynamicStruct::Reader>(
                                     db_setting).get(field));

              if (vm_setting.IDs.SettingID == vm_config.size() - 1)
              {
                // Last setting has been set, set all admin and VM info
                auto admin_vm_info = admin_virtual_machines_list[vm_index];
                admin_vm->SetAdminVmInfo(admin_vm_info);

                if (admin_vm->GetSetting(VmSetting::Setting::AUTO_START).getAutoStart())
                {
                  auto vm_info = virtual_machine_list[vm_index];
                  admin_vm->SetVmInfo(vm_info);

                  virtual_machines.emplace(vm_setting.IDs.VmId,
                    std::make_shared<VirtualMachine<CollabVmSocket<
                      typename TServer::TSocket>>>(vm_setting.IDs.VmId, vm_info));
                }
                admin_virtual_machines.emplace(vm_setting.IDs.VmId, std::move(admin_vm));
              }
            }
          });
        return {
          std::move(virtual_machines),
          std::move(admin_virtual_machines),
          std::move(vm_list_message_builder),
          std::move(admin_vm_list_message_builder)
        };
      }

      /*
      VirtualMachinesList(std::unique_ptr<capnp::MallocMessageBuilder> vm_list_message_builder)
        : vm_info_list_(std::move(vm_list_message_builder))
      {
      }
      */
      capnp::MallocMessageBuilder& GetMessageBuilder()
      {
        return vm_info_list_.GetMessageBuilder();
      }

      std::shared_ptr<AdminVirtualMachine<TClient>> GetAdminVirtualMachine(
        const std::uint32_t id)
      {
        auto vm = admin_virtual_machines_.find(id);
        if (vm == admin_virtual_machines_.end())
        {
          return {};
        }
        return vm->second;
      }

      std::shared_ptr<AdminVirtualMachine<TClient>> RemoveAdminVirtualMachine(
        const std::uint32_t id)
      {
        auto vm = admin_virtual_machines_.find(id);
        if (vm == admin_virtual_machines_.end())
        {
          return {};
        }
        auto vm_ptr = std::move(vm->second);
        admin_virtual_machines_.erase(vm);
        admin_vm_info_list_.RemoveFirst([id](auto info)
        {
          return info.getId() == id;
        });
        return vm_ptr;
      }

      capnp::MallocMessageBuilder& GetAdminVirtualMachineInfo() const
      {
        return admin_vm_info_list_.GetMessageBuilder();
      }

      const auto& GetVirtualMachinesMap() const
      {
        return virtual_machines_;
      }

      /*
      auto AddVirtualMachine(const std::uint32_t id)
      {
        return std::make_shared<VirtualMachine<TClient>>(
          id, vm_info_list_.Add());
      }
      */

      auto AddAdminVirtualMachine(const std::uint32_t id,
                                  capnp::List<VmSetting>::Reader
                                  initial_settings)
      {
        auto vm = std::make_shared<AdminVirtualMachine<TClient>>(id,
          vm_info_list_.Add());
        vm->SetInitialSettings(initial_settings);
        auto admin_vm_info = admin_vm_info_list_.Add();
        vm->SetAdminVmInfo(admin_vm_info);
        auto [it, inserted_new] = admin_virtual_machines_.emplace(id, vm);
        assert(inserted_new);
        return vm;
      }

      template <typename TFunction>
      struct ResizableList
      {
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

        template<typename TTransformer>
        void Transform(TTransformer&& transformer)
        {
          auto message_builder =
            std::make_unique<capnp::MallocMessageBuilder>();
          const auto size = list_.size();
          auto new_vm_list = TFunction::InitList(*message_builder, size);
          for (auto i = 0u; i < size; i++)
          {
            transformer(list_[i], new_vm_list[i]);
          }
          list_ = new_vm_list;
          message_builder_ = std::move(message_builder);
        }

        capnp::MallocMessageBuilder& GetMessageBuilder() const
        {
          return *message_builder_;
        }

      private:
        std::unique_ptr<capnp::MallocMessageBuilder> message_builder_;
        /*
        using ListType = std::invoke_result_t<typename TFunction::GetList,
          capnp::MallocMessageBuilder&>;
          */
        typename TFunction::ListType list_;
      };

    private:
      struct InitVmInfo
      {
        using ListType = capnp::List<CollabVmServerMessage::VmInfo>::Builder;

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
        using ListType = capnp::List<CollabVmServerMessage::AdminVmInfo>::Builder;

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
    public:
      ResizableList<InitAdminVmInfo> admin_vm_info_list_;
    private:
      std::unordered_map<std::uint32_t, std::shared_ptr<VirtualMachine<TClient>>
      > virtual_machines_;

      std::unordered_map<std::uint32_t, std::shared_ptr<AdminVirtualMachine<TClient>>
      > admin_virtual_machines_;

      VirtualMachinesList(
        std::unordered_map<std::uint32_t,
                           std::shared_ptr<VirtualMachine<TClient>>>&&
        virtual_machines,
        std::unordered_map<std::uint32_t,
                           std::shared_ptr<AdminVirtualMachine<TClient>>>&&
        admin_virtual_machines,
        std::unique_ptr<capnp::MallocMessageBuilder>&&
        vm_list_message_builder,
        std::unique_ptr<capnp::MallocMessageBuilder>&&
        admin_vm_list_message_builder) :
        vm_info_list_(std::move(vm_list_message_builder)),
        admin_vm_info_list_(std::move(admin_vm_list_message_builder)),
        virtual_machines_(std::move(virtual_machines)),
        admin_virtual_machines_(std::move(admin_virtual_machines))
      {
      }
    };

    StrandGuard<ServerSettingsList> settings_;
    StrandGuard<std::unordered_map<std::uint32_t,
                                   std::unique_ptr<capnp::MallocMessageBuilder>>
                                  > vm_settings_;
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
    TVmListProvider vm_list_provider_;
    StrandGuard<VirtualMachinesList<CollabVmSocket<typename TServer::TSocket>>>
    virtual_machines_;
    Strand login_strand_;
    StrandGuard<std::vector<std::shared_ptr<Socket>>> viewing_admins_;
    std::uniform_int_distribution<std::uint32_t> guest_rng_;
    std::default_random_engine rng_;
    StrandGuard<UserChannel<Socket>> global_chat_room_;
  };
} // namespace CollabVm::Server
