namespace CollabVm::Server
{
template<typename TClient,
         typename TUserData,
         typename TBase = std::nullptr_t>
struct UserChannel
{
  explicit UserChannel(const std::uint32_t id) :
    chat_room_(id)
  {
  }

  void Clear()
  {
    users_.clear();
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
    OnForEachUsers(callback);
    std::for_each(
      users_.begin(),
      users_.end(),
      [callback=std::forward<TCallback>(callback)]
      (auto& user) mutable {
        callback(user.second, *user.first);
      });
  }

  template<typename TCallback>
  void OnForEachUsers(TCallback& callback) {
    if constexpr (!std::is_null_pointer_v<TBase>) {
      if constexpr (!std::is_same_v<
                        decltype(&UserChannel::OnForEachUsers<TCallback>),
                        decltype(&TBase::template OnForEachUsers<TCallback>)>) {
        static_cast<TBase&>(*this).OnForEachUsers(callback);
      }
    }
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

    ForEachUser([excluded_user = user.get(), user_message=std::move(user_message),
                 admin_user_message=std::move(admin_user_message)]
      (const auto& user_data, TClient& user)
      {
        if (&user == excluded_user) {
          return;
        }
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
      [this, users_it = users.begin()](auto& user_data, TClient&) mutable
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
  CollabVmChatRoom<TClient,
	                 CollabVm::Common::max_username_len,
                   CollabVm::Common::max_chat_message_len> chat_room_;
  capnp::MallocMessageBuilder message_builder_;
};
}
