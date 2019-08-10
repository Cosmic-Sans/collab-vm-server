#include <algorithm>
#include <chrono>
#include <iostream>

#ifdef WIN32
#include <kj/windows-sanity.h>
#undef VOID
#undef CONST
#endif
#include <capnp/dynamic.h>
#include <capnp/schema.h>

#include <argon2.h>
#include "Database.h"

namespace CollabVm::Server {

Database::Database() : db_("collab-vm.db") {
  if (false) {
    std::cout << "A new database has been created" << std::endl;
  }

  db_ << "PRAGMA foreign_keys = ON";
  db_ <<
    "CREATE TABLE IF NOT EXISTS User ("
    "  Id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,"
    "  Username TEXT NOT NULL,"
    "  PasswordHash BLOB(32) NOT NULL,"
    "  PasswordSalt BLOB(32) NOT NULL,"
    "  TotpKey BLOB(20) NULL,"
    "  SessionId BLOB(16) NULL UNIQUE,"
    "  RegistrationDate INTEGER NOT NULL,"
    "  RegistrationIpAddr BLOB(16) NOT NULL,"
    "  LastActiveIpAddr BLOB(16) NOT NULL,"
    "  LastLogin INTEGER NOT NULL,"
    "  LastFailedLogin INTEGER NULL,"
    "  LastOnline INTEGER NOT NULL,"
    "  FailedLogins INTEGER NOT NULL,"
    "  IsAdmin INTEGER NOT NULL,"
    "  IsDisabled INTEGER NOT NULL)";
  db_ <<
    "CREATE TABLE IF NOT EXISTS UserInvite ("
    "  Id BLOB NOT NULL PRIMARY KEY,"
    "  Username TEXT NULL UNIQUE,"
    "  InviteName TEXT NOT NULL,"
    "  IsAdmin INTEGER NOT NULL,"
    "  Accepted INTEGER NOT NULL DEFAULT 0)";
  db_ <<
    "CREATE TABLE IF NOT EXISTS UnavailableUsername("
    "  Username TEXT NOT NULL PRIMARY KEY)";
  db_ <<
    "CREATE TABLE IF NOT EXISTS ServerConfig ("
    "  ID INTEGER NOT NULL PRIMARY KEY,"
    "  Setting BLOB NOT NULL)";
  db_ <<
    "CREATE TABLE IF NOT EXISTS VmConfig ("
    "  IDs_VmId INTEGER NOT NULL,"
    "  IDs_SettingID INTEGER NOT NULL,"
    "  Setting BLOB NOT NULL,"
    "  PRIMARY KEY (IDs_VmId,"
    "               IDs_SettingID))";
}

void Database::LoadServerSettings(
    capnp::List<ServerSetting>::Builder settings_list) {
  auto fields = capnp::Schema::from<ServerSetting::Setting>().getUnionFields();
  const auto fields_count = fields.size();

  auto setting_id = 0u;
  db_ <<
    "SELECT ServerConfig.ID, ServerConfig.Setting"
    "  FROM ServerConfig"
    "  ORDER BY ServerConfig.ID"
    >> [&](const std::uint8_t id, const std::vector<std::byte>& setting)
    {
      auto server_setting = settings_list[id].initSetting();
      capnp::DynamicStruct::Builder dynamic_server_setting = server_setting;
      const auto field = fields[id];
      auto db_setting = capnp::readMessageUnchecked<ServerSetting::Setting>(
          reinterpret_cast<const capnp::word*>(setting.data()));
      if (db_setting.which() != setting_id) {
        // The Setting union has the wrong field set
        std::cout << "Warning: the server setting '"
                  << field.getProto().getName().cStr() << "' was invalid"
                  << std::endl;
        dynamic_server_setting.clear(field);
        db_ <<
          "INSERT INTO ServerConfig (ID, Setting) VALUES (?, ?)"
          << setting_id << CreateBlob(server_setting.asReader());
        setting_id++;
        return;
      }
      const capnp::DynamicStruct::Reader dynamic_db_setting = db_setting;
      dynamic_server_setting.set(field, dynamic_db_setting.get(field));
      setting_id++;
    };
  if (setting_id)
  {
    return;
  }
  // No settings in the DB, create defaults
  for (; setting_id < fields_count; setting_id++) {
    auto server_setting = settings_list[setting_id].initSetting();
    capnp::DynamicStruct::Builder dynamic_server_setting = server_setting;
    const auto field = fields[setting_id];
    dynamic_server_setting.clear(field);
    db_ <<
      "INSERT INTO ServerConfig (ID, Setting) VALUES (?, ?)"
      << setting_id << CreateBlob(server_setting.asReader());
  }
}

void Database::SaveServerSettings(
    const capnp::List<ServerSetting>::Reader settings_list) {
  for (auto update : settings_list) {
    auto server_setting = update.getSetting();
    db_ <<
      "UPDATE ServerConfig SET Setting=? WHERE ID=?"
      << CreateBlob(server_setting)
      << static_cast<std::uint8_t>(server_setting.which());
  }
}

void Database::CreateVm(const std::uint32_t vm_id,
    const capnp::List<VmSetting>::Reader settings_list) {
  for (auto update : settings_list) {
    auto server_setting = update.getSetting();
    db_ <<
      "INSERT INTO VmConfig (IDs_VmId, IDs_SettingID, Setting) VALUES (?, ?, ?)"
      << vm_id
      << static_cast<std::uint8_t>(server_setting.which())
      << CreateBlob(server_setting);
  }
}

void Database::UpdateVmSettings(const std::uint32_t vm_id,
    const capnp::List<VmSetting>::Reader settings_list) {
  for (auto update : settings_list) {
    auto server_setting = update.getSetting();
    db_ <<
      "UPDATE VmConfig SET Setting=? WHERE IDs_VmId=? AND IDs_SettingID=?"
      << CreateBlob(server_setting)
      << vm_id
      << static_cast<std::uint8_t>(server_setting.which());
  }
}

Database::PasswordHash Database::HashPassword(const std::string& password,
                                       const PasswordSalt& salt) {
  const auto time_cost = 2;
  const auto mem_cost = 32 * 1024;
  const auto parallelism = 1;
  PasswordHash hash(User::password_hash_len);
  argon2i_hash_raw(time_cost, mem_cost, parallelism, password.c_str(),
                   password.length(), salt.data(), User::password_salt_len,
                   hash.data(), User::password_hash_len);
  return hash;
}

CollabVmServerMessage::RegisterAccountResponse::RegisterAccountError
Database::CreateAccount(
    const std::string& username,
    const std::string& password,
    const std::optional<gsl::span<const std::byte, User::totp_key_len>> totp_key,
    const std::optional<gsl::span<const std::byte, invite_id_len>> invite_id,
    const IpAddress& ip_address) {
  //  if (!std::regex_match(username, username_re_)) {
  //    error =
  //    CollabVmServerMessage::RegisterAccountResponse::RegisterAccountError::USERNAME_INVALID;
  //    return;
  //  }

  // First, check if the username is available so time isn't wasted hashing the
  // password
  /*
    {
      odb::transaction t(db_.begin());
      if (db_.query_one<User>(odb::query<User>::Username == username)) {
        return CollabVmServerMessage::RegisterAccountResponse::
            RegisterAccountError::USERNAME_TAKEN;
      }
    }
  */

  if (invite_id.has_value()) {
    db_ <<
      "UPDATE UserInvite"
      "  SET Accepted = 1"
      "  WHERE Id = ? AND Accepted = 0"
      << std::vector<std::byte>(invite_id.value().cbegin(), invite_id.value().cend());
      if (!db_.rows_modified()) {
        return CollabVmServerMessage::RegisterAccountResponse::
            RegisterAccountError::INVITE_INVALID;
      }
  } else {
    auto username_taken = false;
    db_ << "select count(*) from ("
      "select 1 from user"
      "  where username = ?"
      "  union"
      "  select 1 from unavailableusername"
      "  where username = ?"
      "  union"
      "  select 1 from userinvite"
      "  where username = ?)"
      << username
      >> username_taken;
    if (username_taken) {
      return CollabVmServerMessage::RegisterAccountResponse::
          RegisterAccountError::USERNAME_TAKEN;
    }
  }

  const auto salt = GeneratePasswordSalt();
  const auto hash = HashPassword(password, salt);
  User user;
  user.username = username;
  user.password_hash = hash;
  user.password_salt = salt;
  if (totp_key)
  {
    for (auto byte : totp_key.value())
    {
      user.totp_key.push_back(std::byte(byte));
    }
  }
  user.last_online = user.last_login = user.registration_date = GetCurrentTimestamp();
  user.last_active_ip_address = 
    user.registration_ip_address = ip_address;
  try {
    if (invite_id.has_value()) {
      db_ <<
        "SELECT IsAdmin FROM UserInvite"
        "  WHERE Id = ?"
        << std::vector<std::byte>(invite_id.value().cbegin(), invite_id.value().cend())
        >> user.is_admin;
    } else {
        db_ <<
          "SELECT COUNT(*) = 0 FROM User"
          >> user.is_admin;
    }
    CreateUser(user);
  } catch (const sqlite::errors::constraint&) {
    // The username was registered after the first check
    return CollabVmServerMessage::RegisterAccountResponse::
        RegisterAccountError::USERNAME_TAKEN;
  }
  return CollabVmServerMessage::RegisterAccountResponse::RegisterAccountError::
      SUCCESS;
}

using User2=Database::User;
using UserInvite2=Database::UserInvite;
using SessionId2=Database::SessionId;

std::optional<User2> Database::GetUser(const std::string& username)
{
  std::optional<User> optional_user;
  db_ << "SELECT * FROM User WHERE Username = ?" << username
    >> [&optional_user] (
  std::uint32_t id,
  const std::string& username,
  const std::vector<std::byte>& password_hash,
  const std::vector<std::byte>& password_salt,
  const std::vector<std::byte>& totp_key,
  const std::vector<std::byte>& session_id,
  Timestamp registration_date,
  const IpAddress& registration_ip_address,
  const IpAddress& last_active_ip_address,
  Timestamp last_login,
  Timestamp last_failed_login,
  Timestamp last_online,
  std::uint32_t failed_logins,
  bool is_admin,
  bool is_disabled)
  {
    auto& user = optional_user.emplace();
    user.id = id;
    user.username = username;
    user.password_hash = password_hash;
    user.password_salt = password_salt;
    user.totp_key = totp_key;
    user.session_id = session_id;
    user.registration_date = registration_date;
    user.registration_ip_address = registration_ip_address;
    user.last_active_ip_address = last_active_ip_address;
    user.last_login = last_login;
    user.last_failed_login = last_failed_login;
    user.last_online = last_online;
    user.failed_logins = failed_logins;
    user.is_admin = is_admin;
    user.is_disabled = is_disabled;
  };
  return optional_user;
}

void Database::UpdateUser(const User& user)
{
  db_ << "UPDATE User SET "
    "  Id = ?,"
    "  Username = ?,"
    "  PasswordHash = ?,"
    "  PasswordSalt = ?,"
    "  TotpKey = ?,"
    "  SessionId = ?,"
    "  RegistrationDate = ?,"
    "  RegistrationIpAddr = ?,"
    "  LastActiveIpAddr = ?,"
    "  LastLogin = ?,"
    "  LastFailedLogin = ?,"
    "  LastOnline = ?,"
    "  FailedLogins = ?,"
    "  IsAdmin = ?,"
    "  IsDisabled = ?"
    "  WHERE Id = ?"
    << user.id
    << user.username
    << user.password_hash
    << user.password_salt
    << user.totp_key
    << user.session_id
    << user.registration_date
    << user.registration_ip_address
    << user.last_active_ip_address
    << user.last_login
    << user.last_failed_login
    << user.last_online
    << user.failed_logins
    << user.is_admin
    << user.is_disabled
    << user.id;
}

void Database::CreateUser(User& user)
{
  db_ << "INSERT INTO User ("
    "  Username,"
    "  PasswordHash,"
    "  PasswordSalt,"
    "  TotpKey,"
    "  SessionId,"
    "  RegistrationDate,"
    "  RegistrationIpAddr,"
    "  LastActiveIpAddr,"
    "  LastLogin,"
    "  LastFailedLogin,"
    "  LastOnline,"
    "  FailedLogins,"
    "  IsAdmin,"
    "  IsDisabled)"
    "  VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    << user.username
    << user.password_hash
    << user.password_salt
    << user.totp_key
    << user.session_id
    << user.registration_date
    << user.registration_ip_address
    << user.last_active_ip_address
    << user.last_login
    << user.last_failed_login
    << user.last_online
    << user.failed_logins
    << user.is_admin
    << user.is_disabled;
  user.id = db_.last_insert_rowid();
}

Database::Timestamp Database::GetCurrentTimestamp()
{
  return std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::tuple<std::string, bool, SessionId2, SessionId2> Database::CreateSession(
  const std::string& username,
  const IpAddress& ip_address) {
  const auto last_login = GetCurrentTimestamp();
  auto user = std::optional<User>();
  auto success = false;
  SessionId old_session_id;
  SessionId new_session_id;
  do {
    new_session_id = GenerateSessionId();
    try {
      user = GetUser(username);
      if (!user || user->is_disabled) {
        return {};
      }
      user->last_login = last_login;
      user->last_active_ip_address = ip_address;
      old_session_id = new_session_id;
      user->session_id = new_session_id;
      UpdateUser(user.value());
      success = true;
    } catch (const sqlite::errors::constraint&) {
      // Session ID already exists so generate another one and try again
      success = false;
    }
  } while (!success);
  return {std::move(user->username), user->is_admin,
          std::move(old_session_id), std::move(new_session_id)};
}

std::pair<
  CollabVmServerMessage::LoginResponse::LoginResult,
  std::vector<std::byte>>
Database::Login(const std::string& username, const std::string& password) {
  auto user = GetUser(username);
  if (!user) {
    return {
        CollabVmServerMessage::LoginResponse::LoginResult::INVALID_USERNAME,
        {}};
  }
  if (user->is_disabled) {
    return {CollabVmServerMessage::LoginResponse::LoginResult::ACCOUNT_DISABLED,
            {}};
  }
  const auto hash = HashPassword(password, user->password_salt);
  if (!std::equal(user->password_hash.cbegin(), user->password_hash.cend(),
                  hash.cbegin())) {
    return {CollabVmServerMessage::LoginResponse::LoginResult::INVALID_PASSWORD,
            {}};
  }
  if (!user->totp_key.empty()) {
    return {
        CollabVmServerMessage::LoginResponse::LoginResult::TWO_FACTOR_REQUIRED,
        std::move(user->totp_key)};
  }
  return {CollabVmServerMessage::LoginResponse::LoginResult::SUCCESS, {}};
}

bool Database::ChangePassword(const std::string& username,
                    const std::string& old_password,
                    const std::string& new_password)
{
  auto user = GetUser(username);
  if (!user) {
    return false;
  }
  const auto old_hash = HashPassword(old_password, user->password_salt);
  if (!std::equal(user->password_hash.cbegin(), user->password_hash.cend(),
                  old_hash.cbegin())) {
    return false;
  }
  // TODO: Require TOTP for changing the password
  /*
  if (!user.TotpKey.empty()) {
    return false;
  }
  */
  user->password_hash = HashPassword(new_password, user->password_salt);
  UpdateUser(user.value());
  return true;
}

bool Database::CreateReservedUsername(const std::string& username) {
  try {
    db_ <<
      "INSERT INTO UnavailableUsername (Username)"
      " VALUES (?)"
      << username;
  } catch (const sqlite::sqlite_exception&) {
    return false;
  }
  return true;
}

std::optional<Database::InviteId> Database::CreateInvite(
    const std::string_view invite_name,
    const std::string_view username,
    const bool is_admin) {
  if (!username.empty())
  {
    bool username_taken;
    db_ << "select count(*) from user"
      "  where username = ?"
      << std::string(username)
      >> username_taken;
    if (username_taken) {
      return {};
    }
  }

  UserInvite invite;
  invite.name = invite_name;
  invite.username = username;
  invite.is_admin = is_admin;
  auto success = false;
  do {
    invite.id = GenerateInviteId();
    try {
      db_ <<
        "INSERT INTO UserInvite ("
        "  Id,"
        "  Username,"
        "  InviteName,"
        "  IsAdmin)"
        " VALUES (?, ?, ?, ?)"
        << invite.id
        << invite.username
        << invite.name
        << invite.is_admin;
      success = true;
    } catch (const sqlite::errors::constraint_primarykey&) {
      // Session ID already exists so generate another one and try again
      success = false;
    } catch (const sqlite::errors::constraint_unique&) {
      // Username already exists
      break;
    }
  } while (!success);

  if (success) {
    return invite.id;
  }
  return {};
}

bool Database::DeleteReservedUsername(const std::string_view username) {
  try {
    db_ << "DELETE FROM UnavailableUsername WHERE Username = ?"
      << std::string(username);
  } catch (const sqlite::sqlite_exception&) {
    return false;
  }
  return true;
}

bool Database::UpdateInvite(const gsl::span<const std::byte, invite_id_len> id,
                            const std::string_view username,
                            const bool is_admin) {
  db_ <<
    "UPDATE UserInvite"
    "  SET Username = ?, IsAdmin = ?"
    "  WHERE Id = ?"
    << std::string(username)
    << is_admin
    << std::vector<std::byte>(id.cbegin(), id.cend());
  return db_.rows_modified();
}

bool Database::DeleteInvite(const gsl::span<const std::byte, invite_id_len> id) {
db_ <<
  "DELETE FROM UserInvite WHERE Id = ?"
  << std::vector<std::byte>(id.cbegin(), id.cend());
  return db_.rows_modified();
}

std::pair<bool, std::string> Database::ValidateInvite(const gsl::span<const std::byte, invite_id_len> id) {
  std::string username;
  try {
    db_ <<
      "select username FROM UserInvite WHERE Id = ? and Accepted = 0"
      << std::vector<std::byte>(id.cbegin(), id.cend())
      >> username;
  } catch (const sqlite::sqlite_exception&) {
    return {false, {}};
  }
  return {true, username};
}

}  // namespace CollabVm::Server
