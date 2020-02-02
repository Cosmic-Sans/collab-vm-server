#pragma once

#include <chrono>
#include "Database/Database.h"
#include "CollabVm.capnp.h"
#include "IPData.hpp"
#include "TurnController.hpp"
#include "VoteController.hpp"
#include "RecordingController.hpp"

namespace CollabVm::Server
{
template<typename TServer, typename TClient>
struct AdminVirtualMachine
{
  AdminVirtualMachine(boost::asio::io_context& io_context,
                      const std::uint32_t id,
                      TServer& server,
                      capnp::List<VmSetting>::Reader initial_settings,
                      // TODO: constructors shouldn't have out parameters
                      CollabVmServerMessage::AdminVmInfo::Builder admin_vm_info)
                     : id_(id),
                       state_(
                         io_context,
                         decltype(state_)::ConstructWithStrand,
                         *this,
                         id,
                         io_context,
                         initial_settings,
                         admin_vm_info),
                       server_(server)
  {
  }

  void Vote(std::shared_ptr<TClient>&& user, bool voted_yes) {
    state_.dispatch(
      [user=std::forward<std::shared_ptr<TClient>>(user), voted_yes](auto& state) mutable
      {
        state.Vote(std::move(user), voted_yes);
      });
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

  void OnGuacamoleInstructions(
      std::shared_ptr<std::vector<std::shared_ptr<SharedSocketMessage>>> instructions) {
    state_.dispatch([instructions = std::move(instructions)](auto& state) mutable {
      state.ForEachUser(
        [instructions = std::move(instructions)]
        (const auto&, auto& user)
        {
          user.QueueMessageBatch([instructions](auto enqueue)
          {
            for (auto& instruction : *instructions)
            {
              enqueue(instruction);
            }
          });
        });
      });
  }

  template<typename TCallback>
  void SetRecordingSettings(TCallback&& callback) {
    state_.dispatch([callback = std::forward<TCallback>(callback)](auto& state) {
      state.SetRecordingSettings(std::move(callback));
      });
  }

  struct VmState final
    : TurnController<std::shared_ptr<TClient>>,
      VoteController<VmState>,
      UserChannel<TClient, typename TClient::UserData, VmState>,
      RecordingController<VmState>
  {
    using VmTurnController = TurnController<std::shared_ptr<TClient>>;
    using VmVoteController = VoteController<VmState>;
    using VmUserChannel = UserChannel<TClient, typename TClient::UserData, VmState>;
    using VmRecording = RecordingController<VmState>;

    VmState(
      boost::asio::io_context::strand& strand,
      AdminVirtualMachine& admin_vm,
      const std::uint32_t id,
      boost::asio::io_context& io_context,
      capnp::List<VmSetting>::Reader initial_settings,
      CollabVmServerMessage::AdminVmInfo::Builder admin_vm_info)
      : VmTurnController(strand),
        VmVoteController(strand),
        VmUserChannel(id),
        VmRecording(strand, id),
        connect_delay_timer_(strand),
        message_builder_(std::make_unique<capnp::MallocMessageBuilder>()),
        settings_(GetInitialSettings(initial_settings)),
        guacamole_client_(strand, admin_vm),
        admin_vm_(admin_vm)
    {
      SetAdminVmInfo(admin_vm_info);

      VmTurnController::SetTurnTime(
        std::chrono::seconds(
          GetSetting(VmSetting::Setting::TURN_TIME).getTurnTime()));
    }

    template<typename TCallback>
    void SetRecordingSettings(TCallback&& callback) {
      VmRecording::SetRecordingSettings(callback());
      if (active_
          && GetSetting(VmSetting::Setting::RECORDINGS_ENABLED)
               .getRecordingsEnabled()
          && !VmRecording::IsRecording()) {
        VmRecording::Start();
      }
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
      const VmSetting::Setting::Which setting) const
    {
      return const_cast<capnp::List<VmSetting>::Builder&>(settings_)[setting].getSetting();
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

    void OnAddUser(const std::shared_ptr<TClient>& user) {
      user->QueueMessageBatch([this](auto&& message) {
          WriteChannelJoinMessages(std::forward<decltype(message)>(message));
        });
    }

    // A fake user that will be added to the users list to
    // capture messages and save them to the recording
    struct RecordingUser {
      RecordingUser(VmRecording& recording)
        : recording_(recording) {
        user_data.user_type = CollabVmServerMessage::UserType::ADMIN;
      }
      template<typename TMessage>
      void QueueMessage(TMessage&& socket_message)
      {
        recording_.WriteMessage(*socket_message);
      }
      template<typename TCallback>
      void QueueMessageBatch(TCallback&& callback)
      {
        callback([this](auto&& socket_message) {
          QueueMessage(std::forward<decltype(socket_message)>(socket_message));
        });
      }
      VmRecording& recording_;
      typename TClient::UserData user_data;
    } recording_user_ = {*this};

    template<typename TCallback>
    void OnForEachUsers(TCallback& callback) {
      if constexpr (std::is_invocable_v<
          TCallback, typename TClient::UserData&, RecordingUser&>) {
        callback(recording_user_.user_data, recording_user_);
      }
    }

    template<typename TWriteMessage>
    void WriteChannelJoinMessages(TWriteMessage&& write_message) {
      write_message(GetVmDescriptionMessage());
      write_message(GetTurnQueue());
      write_message(GetVoteStatus());
      guacamole_client_.AddUser(
        [write_message = std::move(write_message)]
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
          write_message(std::move(socket_message));
        });
    }

    void OnRemoveUser(const std::shared_ptr<TClient>& user) {
      VmTurnController::RemoveUser(user);

      const auto user_data = VmUserChannel::GetUserData(user);
      if (!user_data.has_value()) {
        return;
      }
      const auto votes_changed =
        VmVoteController::RemoveVote(user_data->get().vote_data);
      if (votes_changed) {
          VmUserChannel::BroadcastMessage(GetVoteStatus());
      }
    }

    void OnCurrentUserChanged(
        const std::deque<std::shared_ptr<TClient>>& users_queue,
        std::chrono::milliseconds time_remaining) override {
      BroadcastTurnQueue(users_queue, time_remaining);
    }

    void OnUserAdded(
        const std::deque<std::shared_ptr<TClient>>& users_queue,
        std::chrono::milliseconds time_remaining) override {
      BroadcastTurnQueue(users_queue, time_remaining);
    }

    void OnUserRemoved(
        const std::deque<std::shared_ptr<TClient>>& users_queue,
        std::chrono::milliseconds time_remaining) override {
      BroadcastTurnQueue(users_queue, time_remaining);
    }

    void BroadcastTurnQueue(
        const std::deque<std::shared_ptr<TClient>>& users_queue,
        std::chrono::milliseconds time_remaining) {
      VmUserChannel::BroadcastMessage(GetTurnQueue());
    }

    [[nodiscard]]
    std::shared_ptr<SocketMessage> GetTurnQueue() const {
      const auto& users_queue = VmTurnController::GetTurnQueue();
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
      vm_turn_info.setTimeRemaining(VmTurnController::GetTimeRemaining().count());
      auto users_list = vm_turn_info.initUsers(users_queue.size());
      auto i = 0u;
      const auto& channel_users = VmUserChannel::GetUsers();
      for (const auto& user_in_queue : users_queue) {
        if (const auto channel_user = channel_users.find(user_in_queue);
            channel_user != channel_users.end()) {
          users_list.set(i++, channel_user->second.username);
        }
      }
      return message;
    }

    void ApplySettings(const capnp::List<VmSetting>::Reader settings,
                       const capnp::List<VmSetting>::Reader previous_settings)
    {
      VmTurnController::SetTurnTime(
        std::chrono::seconds(
          settings[VmSetting::Setting::TURN_TIME]
          .getSetting().getTurnTime()));
      if (!settings[VmSetting::Setting::Which::TURNS_ENABLED]
           .getSetting().getTurnsEnabled()
        && previous_settings[VmSetting::Setting::Which::
             TURNS_ENABLED]
           .getSetting().getTurnsEnabled())
      {
        VmTurnController::Clear();
      }
      const auto votes_enabled = settings[VmSetting::Setting::Which::VOTES_ENABLED]
        .getSetting().getVotesEnabled();
      if (votes_enabled
          != previous_settings[VmSetting::Setting::Which::
             VOTES_ENABLED].getSetting().getVotesEnabled())
      {
        if (!votes_enabled) {
          VmVoteController::StopVote();
        }
        VmUserChannel::BroadcastMessage(GetVoteStatus());
      }
      const auto description =
        settings[VmSetting::Setting::Which::DESCRIPTION]
        .getSetting().getDescription();
      if (previous_settings[VmSetting::Setting::Which::DESCRIPTION]
             .getSetting().getDescription() != description)
      {
        VmUserChannel::BroadcastMessage(GetVmDescriptionMessage());
      }
      if (settings[VmSetting::Setting::Which::DISALLOW_GUESTS]
            .getSetting().getDisallowGuests()
          && !previous_settings[VmSetting::Setting::Which::DISALLOW_GUESTS]
             .getSetting().getDisallowGuests())
      {
        VmUserChannel::ForEachUser(
          [](auto& user_data, TClient& socket)
          {
            // TODO: Send a channel disconnect message
            // instead of closing the socket
            socket.Close();
          });
      }
      const auto recordings_enabled =
        settings[VmSetting::Setting::RECORDINGS_ENABLED]
        .getSetting().getRecordingsEnabled();
      if (active_ && recordings_enabled
          != previous_settings[VmSetting::Setting::RECORDINGS_ENABLED]
               .getSetting().getRecordingsEnabled()) {
        if (recordings_enabled) {
          VmRecording::Start();
        } else {
          VmRecording::Stop();
        }
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

    void StartGuacamoleClient()
    {
      const auto protocol =
        GetSetting(VmSetting::Setting::PROTOCOL).getProtocol();
      if (protocol == VmSetting::Protocol::RDP)
      {
        guacamole_client_.StartRDP();
      }
      else if (protocol == VmSetting::Protocol::VNC)
      {
        guacamole_client_.StartVNC();
      }
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

    [[nodiscard]]
    std::shared_ptr<SocketMessage> GetVoteStatus() const
    {
      auto message = SocketMessage::CreateShared();
      auto vote_status = message->GetMessageBuilder()
        .initRoot<CollabVmServerMessage>()
        .initMessage()
        .initVoteStatus()
        .initStatus();
      if (!GetVotesEnabled()) {
        vote_status.setDisabled();
        return message;
      }
      if (VmVoteController::IsCoolingDown()) {
        vote_status.setCoolingDown();
        return message;
      }
      const auto time_remaining = VmVoteController::GetTimeRemaining().count();
      if (!time_remaining) {
        vote_status.setIdle();
        return message;
      }
      auto vote_info = vote_status.initInProgress();
      vote_info.setTimeRemaining(time_remaining);
      vote_info.setYesVoteCount(VmVoteController::GetYesVoteCount());
      vote_info.setNoVoteCount(VmVoteController::GetNoVoteCount());
      return message;
    }

    void Vote(std::shared_ptr<TClient>&& user, bool voted_yes) {
      const auto user_vote = VmUserChannel::GetUserData(user);
      if (!user_vote.has_value()) {
        return;
      }
      const auto vote_counted =
        VmVoteController::AddVote(user_vote.value().get().vote_data, voted_yes);
      if (vote_counted) {
          VmUserChannel::BroadcastMessage(GetVoteStatus());
      }
    }

    void OnVoteStart()
    {
    }

    void OnVoteEnd(bool vote_passed)
    {
      if (vote_passed) {
        admin_vm_.Restart();
      }
      auto message = SocketMessage::CreateShared();
      message->GetMessageBuilder()
        .initRoot<CollabVmServerMessage>()
        .initMessage()
        .setVoteResult(vote_passed);
      VmUserChannel::ForEachUser(
        [message = std::move(message)]
        (auto& user_data, auto& socket)
        {
          user_data.vote_data = {};
          socket.QueueMessage(message);
        });
    }

    void OnVoteIdle()
    {
      if (GetVotesEnabled()) {
        VmUserChannel::BroadcastMessage(GetVoteStatus());
      }
    }

    [[nodiscard]]
    bool GetVotesEnabled() const
    {
      return GetSetting(VmSetting::Setting::VOTES_ENABLED).getVotesEnabled();
    }

    [[nodiscard]]
    auto GetVoteTime() const
    {
      return std::chrono::seconds(
        GetSetting(VmSetting::Setting::VOTE_TIME).getVoteTime());
    }

    [[nodiscard]]
    auto GetVoteCooldownTime() const
    {
      return std::chrono::seconds(
        GetSetting(VmSetting::Setting::VOTE_COOLDOWN_TIME).getVoteCooldownTime());
    }

    void OnRecordingStarted(std::chrono::time_point<std::chrono::system_clock> time)
    {
      admin_vm_.server_.GetDatabase().SetRecordingStartTime(
        VmUserChannel::GetId(), VmRecording::GetFilename(), time);
    }

    void OnRecordingStopped(std::chrono::time_point<std::chrono::system_clock> time)
    {
      admin_vm_.server_.GetDatabase().SetRecordingStopTime(
        VmUserChannel::GetId(), VmRecording::GetFilename(), time);
    }

    void OnKeyframeInRecording()
    {
      VmRecording::WriteMessage(
        VmUserChannel::CreateAdminUserListMessage()->GetMessageBuilder());

      auto message_builder = capnp::MallocMessageBuilder();
      auto connect_result =
        message_builder.initRoot<CollabVmServerMessage>()
        .initMessage()
        .initConnectResponse()
        .initResult();
      auto connectSuccess = connect_result.initSuccess();
      VmUserChannel::GetChatRoom().GetChatHistory(connectSuccess);
      VmRecording::WriteMessage(message_builder);

      WriteChannelJoinMessages([this](auto&& message) {
          VmRecording::WriteMessage(*message);
        });
    }

    bool active_ = false;
    bool connected_ = false;
    boost::asio::steady_timer connect_delay_timer_;
    std::size_t viewer_count_ = 0;
    std::unique_ptr<capnp::MallocMessageBuilder> message_builder_;
    capnp::List<VmSetting>::Builder settings_;
    CollabVmGuacamoleClient<AdminVirtualMachine> guacamole_client_;
    AdminVirtualMachine& admin_vm_;
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

      if (state.GetSetting(VmSetting::Setting::RECORDINGS_ENABLED)
              .getRecordingsEnabled()) {
        static_cast<RecordingController<VmState>&>(state).Start();
      }

      if (const auto start_command =
            state.GetSetting(VmSetting::Setting::START_COMMAND).getStartCommand();
          start_command.size()) {
        server_.ExecuteCommandAsync(start_command.cStr());
      }

      state.active_ = true;
      UpdateVmInfo();

      state.SetGuacamoleArguments();
      state.StartGuacamoleClient();
    });
  }

  void Stop()
  {
    state_.dispatch([this](auto& state)
    {
      static_cast<RecordingController<VmState>&>(state).Stop();

      if (!state.active_) {
          return;
      }

      if (const auto stop_command =
            state.GetSetting(VmSetting::Setting::STOP_COMMAND).getStopCommand();
          stop_command.size()) {
        server_.ExecuteCommandAsync(stop_command.cStr());
      }

      state.active_ = false;
      state.connect_delay_timer_.cancel();
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
        server_.ExecuteCommandAsync(restart_command.cStr());
      }
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

  template<typename TGetInstruction>
  void ReadInstruction(std::shared_ptr<TClient> user, TGetInstruction&& getInstruction)
  {
    state_.dispatch(
      [user = std::move(user),
       getInstruction=std::forward<TGetInstruction>(getInstruction)](auto& state)
      {
        if (state.connected_
            && (state.HasCurrentTurn(user) && !state.IsPaused()
                || state.IsAdmin(user))) {
          state.guacamole_client_.ReadInstruction(getInstruction());
          static_cast<RecordingController<VmState>&>(state)
            .WriteMessage(getInstruction());
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

      auto messages = std::vector<std::shared_ptr<SharedSocketMessage>>();
      state.guacamole_client_.AddUser(
        [&messages](capnp::MallocMessageBuilder&& message_builder)
        {
           // TODO: Avoid copying
          const auto guac_instr =
            message_builder.getRoot<Guacamole::GuacServerInstruction>();
          auto socket_message = messages.emplace_back(SocketMessage::CreateShared());
          socket_message->GetMessageBuilder()
                        .initRoot<CollabVmServerMessage>()
                        .initMessage()
                        .setGuacInstr(guac_instr);
          socket_message->CreateFrame();
        });
      state.ForEachUser(
        [&messages]
        (const auto&, auto& user)
        {
          user.QueueMessageBatch(
            [&messages](auto queue_message)
            {
              for (auto& message : messages)
              {
                queue_message(message);
              }
            });
        });
    });
  }

  void OnStop()
  {
    state_.dispatch([this](auto& state)
      {
        if (state.connected_ || !state.active_)
        {
          state.connected_ = false;
          UpdateVmInfo();
        }
        if (!state.active_)
        {
          return;
        }
        state.connect_delay_timer_.expires_after(std::chrono::seconds(1));
        state.connect_delay_timer_.async_wait(
          state_.wrap([this](auto& state, auto error_code)
          {
            if (error_code || !state.active_)
            {
              UpdateVmInfo();
              return;
            }
            if (const auto start_command =
                  state.GetSetting(VmSetting::Setting::START_COMMAND).getStartCommand();
                start_command.size()
                && state.GetSetting(VmSetting::Setting::RUN_START_COMMAND_AFTER_DISCONNECT)
                        .getRunStartCommandAfterDisconnect()) {
              server_.ExecuteCommandAsync(start_command.cStr());
            }
            state.StartGuacamoleClient();
          }));
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
  StrandGuard<boost::asio::io_context::strand, VmState> state_;
  TServer& server_;
};
}
