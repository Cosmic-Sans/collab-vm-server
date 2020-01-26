#pragma once
#include <chrono>
#include <openssl/rand.h>
#include <gsl/span>
#include <memory>
#define MODERN_SQLITE_STD_OPTIONAL_SUPPORT
#include <sqlite_modern_cpp.h>
#include <string>
#include <string_view>
#include <utility>

#undef CONST
#include <capnp/message.h>
#include <boost/functional/hash/hash.hpp>
#include <optional>
#include "CollabVm.capnp.h"

namespace CollabVm::Server {

class Database final {
 public:
using Timestamp = std::uint64_t;
using IpAddress = std::vector<std::byte>;
using SessionId = std::vector<std::byte>;
using PasswordSalt = std::vector<std::byte>;
using PasswordHash = std::vector<std::byte>;

struct User {
  constexpr static std::size_t password_hash_len = 32;
  constexpr static std::size_t password_salt_len = 32;
  constexpr static std::size_t totp_key_len = 20;
  constexpr static std::size_t session_id_len = 16;
  std::uint32_t id = 0;
  std::string username;
  std::vector<std::byte> password_hash;
  std::vector<std::byte> password_salt;
  std::vector<std::byte> totp_key;
  std::vector<std::byte> session_id;
  Timestamp registration_date = 0;
  IpAddress registration_ip_address;
  IpAddress last_active_ip_address;
  Timestamp last_login = 0;
  std::optional<Timestamp> last_failed_login;
  Timestamp last_online = 0;
  std::uint32_t failed_logins = 0;
  bool is_admin = false;
  bool is_disabled = false;
};

struct UserInvite {
  constexpr static std::size_t id_length = 32;
	std::vector<std::byte> id;
	std::string username;
	std::string name;
	bool is_admin;
//	bool IsHost;
};

  constexpr static int max_password_len = 160;
  constexpr static int invite_id_len = UserInvite::id_length;
  using InviteId = std::vector<std::byte>;
  using SessionIdHasher = boost::hash<SessionId>;

  Database();

  void SetReCaptchaSettings();

  static Database::PasswordHash HashPassword(const std::string& password,
                                             const PasswordSalt& salt);

  CollabVmServerMessage::RegisterAccountResponse::RegisterAccountError
  CreateAccount(
      const std::string& username,
      const std::string& password,
      const std::optional<gsl::span<const std::byte, User::totp_key_len>> totp_key,
      const std::optional<gsl::span<const std::byte, invite_id_len>> invite_id,
      const IpAddress& ip_address);
  std::optional<User> GetUser(const std::string& username);
  void UpdateUser(const User& user);
void CreateUser(User& user);
static Timestamp GetCurrentTimestamp();

/**
   * @returns The username with correct case and a pair of session IDs, old and
   * new.
   */
  std::tuple<std::string, bool, SessionId, SessionId> CreateSession(
    const std::string& username,
    const IpAddress& ip_address);

  /**
   * Attempts to log in a user.
   * @returns A pair containing the login result and a vector
   * that will contain the TOTP key if two-factor authentication is required.
   */
  std::pair<CollabVmServerMessage::LoginResponse::LoginResult, std::vector<std::
  byte>>
  Login(const std::string& username, const std::string& password);

  bool ChangePassword(const std::string& username,
                      const std::string& old_password,
                      const std::string& new_password);

  bool CreateReservedUsername(const std::string& username);

  std::uint32_t GetReservedUsernamesCount()
  {
    auto count = 0u;
    db_ <<
      "SELECT COUNT(*)"
      "  FROM UnavailableUsername"
      >> count;
    return count;
  }

  template <typename TCallback>
  void ReadReservedUsernames(TCallback callback) {
    db_ <<
      "SELECT Username"
      "  FROM UnavailableUsername"
      >> [callback = std::forward<TCallback>(callback)]
         (const std::string& username)
         {
           callback(username);
         };
  }

  bool DeleteReservedUsername(const std::string_view username);

  std::uint32_t GetInvitesCount()
  {
    std::uint32_t invites_count;
    db_ <<
      "SELECT COUNT(DISTINCT VmConfig.IDs_VmId) FROM VmConfig"
      >> invites_count;
    return invites_count;
  }

  std::optional<InviteId> CreateInvite(const std::string_view invite_name,
                                       const std::string_view username,
                                       const bool is_admin);

  template <typename TCallback>
  void ReadInvites(TCallback callback) {
    db_ <<
      "SELECT"
      "  Id,"
      "  Username,"
      "  InviteName,"
      "  IsAdmin"
      "  FROM UserInvite"
      >> [callback = std::forward<TCallback>(callback)]
         (const std::vector<std::byte> id,
          const std::string& username,
          const std::string& name,
          bool is_admin)
         {
           UserInvite invite;
           invite.id = id;
           invite.username = username;
           invite.name = name;
           invite.is_admin = is_admin;
           callback(std::move(invite));
         };
  }


  bool UpdateInvite(const gsl::span<const std::byte, invite_id_len> id,
                    const std::string_view username,
                    const bool is_admin);

  bool DeleteInvite(const gsl::span<const std::byte, invite_id_len> id);

  std::pair<bool, std::string> ValidateInvite(const gsl::span<const std::byte, invite_id_len> id);

  std::uint32_t GetNewVmId() {
    std::uint32_t new_vm_id;
    db_ << "SELECT MAX(VmConfig.IDs_VmId) + 1 FROM VmConfig"
      >> new_vm_id;
    return (std::max)(new_vm_id, 1u);
  }

  std::uint32_t GetVmCount()
  {
    std::uint32_t vm_count;
    db_ <<
      "SELECT COUNT(DISTINCT VmConfig.IDs_VmId) FROM VmConfig"
      >> vm_count;
    return vm_count;
  }

  void CreateVm(const std::uint32_t vm_id,
    const capnp::List<VmSetting>::Reader settings_list);

  template <typename TCallback>
  void ReadVmSettings(TCallback callback) {
    db_ <<
      "SELECT VmConfig.IDs_VmId, VmConfig.IDs_SettingID, VmConfig.Setting"
      "  FROM VmConfig"
      "  ORDER BY VmConfig.IDs_VmId, VmConfig.IDs_SettingID"
      >> [callback = std::forward<TCallback>(callback)]
         (std::uint32_t vm_id,
          std::uint32_t setting_id,
          const std::vector<std::byte>& setting) mutable
          {
            auto db_setting =
              capnp::readMessageUnchecked<VmSetting>(
                reinterpret_cast<const capnp::word*>(setting.data()));
            callback(vm_id, setting_id, db_setting);
          };
  }

  void UpdateVmSettings(const std::uint32_t vm_id,
    const capnp::List<VmSetting>::Reader settings_list);

  void DeleteVm(std::uint32_t id)
  {
    db_ << "DELETE FROM VmConfig WHERE IDs_VmId = ?"
      << id;
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

  void SetRecordingStartTime(
      const std::uint32_t vm_id,
      const std::string_view file_path,
      const std::chrono::time_point<std::chrono::system_clock> time)
  {
    SetRecordingStartStopTime(vm_id, file_path, time, true);
  }

  void SetRecordingStopTime(
      const std::uint32_t vm_id,
      const std::string_view file_path,
      const std::chrono::time_point<std::chrono::system_clock> time)
  {
    SetRecordingStartStopTime(vm_id, file_path, time, false);
  }

  std::tuple<std::string,
            std::chrono::time_point<std::chrono::system_clock>,
            std::chrono::time_point<std::chrono::system_clock>>
    GetRecordingFilePath(
      const std::uint32_t vm_id,
      const std::chrono::time_point<std::chrono::system_clock> start_time,
      const std::chrono::time_point<std::chrono::system_clock> stop_time);

 private:
  void CreateTestVm();

  void SetRecordingStartStopTime(
      const std::uint32_t vm_id,
      const std::string_view file_path,
      const std::chrono::time_point<std::chrono::system_clock> time,
      bool start_time);

  template<typename TReader>
  static std::vector<std::byte> CreateBlob(const TReader server_setting)
  {
    // capnp::copyToUnchecked requires the target buffer to have an extra word
    const auto blob_word_size = server_setting.totalSize().wordCount + 1;
    auto blob = std::vector<std::byte>(blob_word_size * sizeof(capnp::word));
    capnp::copyToUnchecked(
      server_setting,
      kj::arrayPtr(reinterpret_cast<capnp::word*>(&*blob.begin()),
                   blob_word_size));
    return blob;
  }

  template <std::size_t N>
  static std::vector<std::byte> GetRandomBytes() {
    auto bytes = std::vector<std::byte>(N);
    RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), N);
    return bytes;
  }
  constexpr static auto GenerateSessionId = GetRandomBytes<User::session_id_len>;
  constexpr static auto GenerateInviteId = GetRandomBytes<invite_id_len>;
  constexpr static auto GeneratePasswordSalt = GetRandomBytes<User::password_salt_len>;

  sqlite::database db_;
};
}  // namespace CollabVm::Server

namespace std {
  template <>
  struct hash<CollabVm::Server::Database::SessionId>
  {
    std::size_t operator()(const CollabVm::Server::Database::SessionId& session_id) const noexcept
    {
      std::size_t seed = 0;
      for (auto byte : session_id)
      {
        boost::hash_combine(seed, static_cast<std::uint8_t>(byte));
      }
      return seed;
    }
  };
}
