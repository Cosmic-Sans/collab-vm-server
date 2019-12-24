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

  struct VmState final
    : TurnController<std::shared_ptr<TClient>>,
      VoteController<VmState>,
      UserChannel<TClient, typename TClient::UserData, VmState>
  {
    using VmTurnController = TurnController<std::shared_ptr<TClient>>;
    using VmVoteController = VoteController<VmState>;
    using VmUserChannel = UserChannel<TClient, typename TClient::UserData, VmState>;

    template<typename TSettingProducer>
    VmState(
      AdminVirtualMachine& admin_vm,
      const std::uint32_t id,
      boost::asio::io_context& io_context,
      TSettingProducer&& get_setting,
      CollabVmServerMessage::AdminVmInfo::Builder admin_vm_info)
      : VmTurnController(io_context),
        VmVoteController(io_context),
        VmUserChannel(id),
        message_builder_(std::make_unique<capnp::MallocMessageBuilder>()),
        settings_(GetInitialSettings(std::forward<TSettingProducer>(get_setting))),
        guacamole_client_(io_context, admin_vm),
        admin_vm_(admin_vm)
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
        VmVoteController(io_context),
        VmUserChannel(id),
        message_builder_(std::make_unique<capnp::MallocMessageBuilder>()),
        settings_(GetInitialSettings(initial_settings)),
        guacamole_client_(io_context, admin_vm),
        admin_vm_(admin_vm)
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
      user->QueueMessageBatch(
        [&guacamole_client=guacamole_client_,
         description_message = GetVmDescriptionMessage(),
         vote_status_message = GetVoteStatus()]
        (auto queue_message) mutable
        {
          queue_message(std::move(description_message));
          queue_message(std::move(vote_status_message));
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

    void OnRemoveUser(const std::shared_ptr<TClient>& user) {
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
      if (settings[VmSetting::Setting::Which::VOTES_ENABLED]
           .getSetting().getVotesEnabled()
        != previous_settings[VmSetting::Setting::Which::
             VOTES_ENABLED]
           .getSetting().getVotesEnabled())
      {
        if (!settings[VmSetting::Setting::Which::VOTES_ENABLED]
             .getSetting().getVotesEnabled()) {
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

    [[nodiscard]]
    std::shared_ptr<SocketMessage> GetVoteStatus() const
    {
        auto message = SocketMessage::CreateShared();
        auto vote_status = message->GetMessageBuilder()
          .initRoot<CollabVmServerMessage>()
          .initMessage()
          .initVoteStatus()
          .initStatus();
        if (GetVotesEnabled()) {
          auto vote_info = vote_status.initEnabled();
          vote_info.setTimeRemaining(VmVoteController::GetTimeRemaining().count());
          vote_info.setYesVoteCount(VmVoteController::GetYesVoteCount());
          vote_info.setNoVoteCount(VmVoteController::GetNoVoteCount());
        } else {
          vote_status.setDisabled();
        }
        return message;
    }

    void Vote(std::shared_ptr<TClient>&& user, bool voted_yes) {
      const auto user_vote = VmUserChannel::GetUserData(user);
      if (!user_vote.has_value()) {
        return;
      }
      const auto vote_counted =
      VmVoteController::Vote(user_vote.value().get().vote_data, voted_yes);
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

    bool active_ = false;
    bool connected_ = false;
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

      if (const auto start_command =
            state.GetSetting(VmSetting::Setting::START_COMMAND).getStartCommand();
          start_command.size()) {
        server_.ExecuteCommandAsync(start_command.cStr());
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
        server_.ExecuteCommandAsync(stop_command.cStr());
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
        server_.ExecuteCommandAsync(restart_command.cStr());
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
      const auto& users = state.GetUsers();
      for (auto& user : users)
      {
        user.first->QueueMessageBatch(
          [&messages](auto queue_message)
          {
            for (auto& message : messages)
            {
              queue_message(message);
            }
          });
      }
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
  StrandGuard<boost::asio::io_context::strand, VmState> state_;
  TServer& server_;
};
}
