#pragma once
#include <openssl/rand.h>
#include <gsl/span>
#include <memory>
#include <odb/sqlite/database.hxx>
#include <string>
#include <utility>

#undef VOID
#include <capnp/message.h>
#include <boost/functional/hash/hash.hpp>
#include <optional>
#include "CollabVm.capnp.h"
#include "ServerConfig-odb.hxx"
#include "ServerConfig.h"
#include "User-odb.hxx"
#include "User.h"
#include "VmConfig-odb.hxx"
#include "VmConfig.h"

namespace CollabVm::Server {
class Database {
 public:
  constexpr static int max_password_len = 160;
  constexpr static int password_hash_len = PASSWORD_HASH_LEN;
  constexpr static int password_salt_len = PASSWORD_SALT_LEN;
  constexpr static int session_id_len = SESSION_ID_LEN;
  constexpr static int totp_token_len = TOTP_KEY_LEN;
  constexpr static int invite_id_len = INVITE_ID_LEN;
  using SessionId = std::array<std::uint8_t, session_id_len>;
  using InviteId = std::array<std::uint8_t, invite_id_len>;
  using SessionIdHasher = boost::hash<SessionId>;

  Database();

  CollabVmServerMessage::RegisterAccountResponse::RegisterAccountError
  CreateAccount(
      const std::string& username,
      const std::string& password,
      const std::optional<gsl::span<const std::byte, TOTP_KEY_LEN>> totp_key,
      const std::array<std::uint8_t, 16>& ip_address);

  /**
   * @returns The username with correct case and a pair of session IDs, old and
   * new.
   */
  std::tuple<std::string, bool, SessionId, SessionId> CreateSession(
      const std::string& username,
      const std::array<std::uint8_t, 16>& ip_address);

  /**
   * Attempts to log in a user.
   * @returns A pair containing the login result and a vector
   * that will contain the TOTP key if two-factor authentication is required.
   */
  std::pair<CollabVmServerMessage::LoginResponse::LoginResult,
            std::vector<uint8_t>>
  Login(const std::string& username, const std::string& password);

  bool CreateReservedUsername(const std::string& username);

  template <typename TCallback>
  void ReadReservedUsernames(TCallback callback) {
    odb::transaction t(db_.begin());
    auto usernames = db_.query<UnavailableUsername>();
    callback(usernames);
  }

  bool DeleteReservedUsername(const std::string& username);

  std::optional<InviteId> CreateInvite(const std::string& invite_name,
                                       const std::string& username,
                                       const bool username_already_reserved,
                                       const bool is_admin);

  template <typename TCallback>
  void ReadInvites(TCallback callback) {
    odb::transaction t(db_.begin());
    auto invites = db_.query<UserInvite>();
    callback(invites);
  }

  bool UpdateInvite(const std::string& id,
                    const std::string& username,
                    const bool is_admin);

  bool DeleteInvite(const std::string& id);

  /**
   * Save a newly created virtual machine to the DB and
   * add it to the map. The virtual machine will be assigned
   * a new ID.
   */
  void AddVm(std::shared_ptr<VmConfig>& vm);

  void UpdateVm(std::shared_ptr<VmConfig>& vm);

  void RemoveVm(const std::string& name);

  template <typename It>
  std::uint32_t CreateVm(It begin, It end) {
    capnp::MallocMessageBuilder message_builder;
    auto fields = capnp::Schema::from<VmSetting::Setting>().getUnionFields();
    odb::transaction transaction(db_.begin());
    const auto vm_id = db_.query_value<NewVmId>().Id;
    for (auto i = 0u; i < fields.size(); i++) {
      while (begin != end && begin->getSetting().which() < i)
      {
        begin++;
      }
      if (begin != end && begin->getSetting().which() == i)
      {
        auto vm_config = ConvertToVmSetting(vm_id, begin->getSetting());
        db_.persist(vm_config);
      }
      else
      {
        const auto field = fields[i];
        auto setting = message_builder.initRoot<VmSetting::Setting>();
        auto dynamic_setting = capnp::DynamicStruct::Builder(setting);
        dynamic_setting.clear(field);
        auto vm_config = ConvertToVmSetting(vm_id, setting.asReader());
        db_.persist(vm_config);
      }
    }
    transaction.commit();
    return vm_id;
  }

  /*
  template <typename T, typename TCallback>
  void Read(TCallback callback) {
    odb::transaction tran(db_.begin());
    auto it = db_.query<T>();
    callback(it);
  }
  */

  template <typename TCallback>
  void ReadVirtualMachines(TCallback callback) {
    odb::transaction tran(db_.begin());
		const auto total_virtual_machines = db_.query_value<TotalVms>().Count;
    auto it = db_.query<VmConfig>("ORDER BY " +
      odb::query<VmConfig>::IDs.VmId + ", " + odb::query<VmConfig>::IDs.SettingID);
    callback(total_virtual_machines, it);
  }

  /*
  template <typename TList, typename TCallback>
  void UpdateServerSettings(const typename capnp::List<TList>::Reader updates,
                            TCallback callback) {
    auto message_builder = std::make_unique<capnp::MallocMessageBuilder>();
    auto list = InitSettings(*message_builder);
    UpdateList<TList>(settings_list_, list, updates);
    settings_ = std::move(message_builder);
    settings_list_ = list;

    odb::transaction tran(db_.begin());
    for (auto update : updates) {
      auto setting = ConvertToDbSetting(
          list[update.getSetting().which()].getSetting().asReader());
      db_.update(setting);
    }
    tran.commit();
  }
  */

  void UpdateVmSettings(const std::uint32_t vm_id,
    const capnp::List<VmSetting>::Reader updates) {
    odb::transaction tran(db_.begin());
    for (auto update : updates) {
      auto setting = ConvertToVmSetting(vm_id, update.getSetting());
      db_.update(setting);
    }
    tran.commit();
  }

  template <typename TList>
  static void UpdateList(const typename capnp::List<TList>::Reader old_list,
                  typename capnp::List<TList>::Builder new_list,
                  const typename capnp::List<TList>::Reader list_updates) {
    assert(old_list.size() == new_list.size());
    for (auto old_setting : old_list) {
      const auto setting_type = old_setting.getSetting().which();
      // TODO: Make this more generic so it doesn't depend on getSetting()
      capnp::DynamicStruct::Builder current_setting = new_list[setting_type].getSetting();
      const auto updated_setting = std::find_if(list_updates.begin(),
                                                   list_updates.end(),
        [setting_type](const auto updated_setting)
        {
          return updated_setting.getSetting().which() == setting_type;
        });
      if (updated_setting != list_updates.end())
      {
        const capnp::DynamicStruct::Reader reader = updated_setting->getSetting();
        KJ_IF_MAYBE(field, reader.which()) {
          current_setting.set(*field, reader.get(*field));
          continue;
        }
      }
      const capnp::DynamicStruct::Reader reader = old_setting.getSetting();
      KJ_IF_MAYBE(field, reader.which()) {
        current_setting.set(*field, reader.get(*field));
      }
    }
  }

  void LoadServerSettings(capnp::List<ServerSetting>::Builder settings_list);
  void SaveServerSettings(
      const capnp::List<ServerSetting>::Reader settings_list);

 private:
  ServerConfig ConvertToDbSetting(
      const ServerSetting::Setting::Reader& server_setting) const;

  VmConfig ConvertToVmSetting(const std::uint32_t vm_id,
                              const VmSetting::Setting::Reader& setting) const;

  template <std::size_t N>
  static std::array<std::uint8_t, N> GetRandomBytes() {
    std::array<std::uint8_t, N> bytes;
    RAND_bytes(bytes.data(), N);
    return bytes;
  }
  static constexpr auto GenerateSessionId =
      Database::GetRandomBytes<Database::session_id_len>;
  static constexpr auto GenerateInviteId =
      Database::GetRandomBytes<Database::invite_id_len>;
  static constexpr auto GeneratePasswordSalt =
      Database::GetRandomBytes<Database::password_salt_len>;

  odb::sqlite::database db_;
};
}  // namespace CollabVmServer
