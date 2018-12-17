#include <algorithm>
#include <iostream>

#include <odb/connection.hxx>
#include <odb/database.hxx>
#include <odb/schema-catalog.hxx>
#include <odb/transaction.hxx>
#ifdef WIN32
#include <kj/windows-sanity.h>
#undef VOID
#undef CONST
#endif
#include <capnp/dynamic.h>
#include <capnp/schema.h>

#include <argon2.h>
#include "Database.h"
#include "ServerConfig-odb.hxx"
#include "VmConfig-odb.hxx"

namespace CollabVm::Server {

Database::Database()
    : db_("collab-vm.db", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE) {
  odb::connection_ptr c(db_.connection());
  c->execute("PRAGMA foreign_keys=OFF");

  odb::transaction t(c->begin());

  if (db_.schema_version() == 0) {
    odb::schema_catalog::create_schema(db_);
    std::cout << "A new database has been created" << std::endl;
  }

  /*

          // Load all the VMs and add them to the map
          // TODO: catch exceptions
          odb::result<VmConfig> vms = db_.query<VmConfig>();
          for (auto&& vm : vms) {
                  // it.load(vm);
                  VirtualMachines[vm.Name] = std::make_shared<VmConfig>(vm);
          }

  */
  t.commit();

  c->execute("PRAGMA foreign_keys=ON");
}

void Database::LoadServerSettings(
    capnp::List<ServerSetting>::Builder settings_list) {
  auto fields = capnp::Schema::from<ServerSetting::Setting>().getUnionFields();
  const auto fields_count = fields.size();

  odb::transaction tran(db_.begin());
  odb::result<ServerConfig> settings(
      db_.query<ServerConfig>("ORDER BY" + odb::query<ServerConfig>::ID));
  auto db_settings_it = settings.begin();
  for (auto setting_id = 0u; setting_id < fields_count; setting_id++) {
    auto server_setting = settings_list[setting_id].initSetting();
    capnp::DynamicStruct::Builder dynamic_server_setting = server_setting;
    const auto field = fields[setting_id];
    if (db_settings_it != settings.end() && db_settings_it->ID == setting_id) {
      // TODO: Make sure db_setting_it->Setting is properly aligned
      auto db_setting = capnp::readMessageUnchecked<ServerSetting::Setting>(
          reinterpret_cast<const capnp::word*>(db_settings_it->Setting.data()));
      if (db_setting.which() == setting_id) {
        const capnp::DynamicStruct::Reader dynamic_db_setting = db_setting;
        dynamic_server_setting.set(field, dynamic_db_setting.get(field));
        // Incrementing db_settings_it will invalidate the object so
        // it needs to be done after the object has been copied into the list
        db_settings_it++;
        continue;
      }
      db_settings_it++;
      // The Setting union has the wrong field set
      std::cout << "Warning: the server setting '"
                << field.getProto().getName().cStr() << "' was invalid"
                << std::endl;
    }
    dynamic_server_setting.clear(field);
    db_.persist(ConvertToDbSetting(server_setting.asReader()));
  }
  tran.commit();
}

void Database::SaveServerSettings(
    const capnp::List<ServerSetting>::Reader updates) {
  odb::transaction tran(db_.begin());
  for (auto update : updates) {
    auto setting = ConvertToDbSetting(update.getSetting());
    db_.update(setting);
  }
  tran.commit();
}
ServerConfig Database::ConvertToDbSetting(
    const ServerSetting::Setting::Reader& server_setting) const {
  // capnp::copyToUnchecked requires the target buffer to have an extra word
  const auto blob_word_size = server_setting.totalSize().wordCount + 1;
  // TODO: Make sure alignment requirements are met
  std::vector<uint8_t> setting_blob(blob_word_size * sizeof(capnp::word));
  capnp::copyToUnchecked(
      server_setting,
      kj::arrayPtr(reinterpret_cast<capnp::word*>(&*setting_blob.begin()),
                   blob_word_size));
  return {static_cast<uint8_t>(server_setting.which()),
          std::move(setting_blob)};
}

VmConfig Database::ConvertToVmSetting(
    const std::uint32_t vm_id,
    const VmSetting::Setting::Reader& setting) const {
  // capnp::copyToUnchecked requires the target buffer to have an extra word
  const auto blob_word_size = setting.totalSize().wordCount + 1;
  // TODO: Make sure alignment requirements are met
  std::vector<std::uint8_t> setting_blob(blob_word_size * sizeof(capnp::word));
  capnp::copyToUnchecked(
      setting,
      kj::arrayPtr(reinterpret_cast<capnp::word*>(&*setting_blob.begin()),
                   blob_word_size));
  return {vm_id, static_cast<std::uint8_t>(setting.which()),
          std::move(setting_blob)};
}

static User::PasswordHash HashPassword(const std::string& password,
                                       const User::PasswordSalt& salt) {
  const auto time_cost = 2;
  const auto mem_cost = 32 * 1024;
  const auto parallelism = 1;
  User::PasswordHash hash;
  argon2i_hash_raw(time_cost, mem_cost, parallelism, password.c_str(),
                   password.length(), salt.data(), Database::password_salt_len,
                   hash.data(), Database::password_hash_len);
  return hash;
}

CollabVmServerMessage::RegisterAccountResponse::RegisterAccountError
Database::CreateAccount(
    const std::string& username,
    const std::string& password,
    const std::optional<gsl::span<const std::byte, TOTP_KEY_LEN>> totp_key,
    const std::array<std::uint8_t, 16>& ip_address) {
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

  auto username_ptr = std::make_unique<UnavailableUsername>(username);
  try {
    odb::transaction t(db_.begin());
    db_.persist(*username_ptr);
    t.commit();
  } catch (const odb::object_already_persistent&) {
    return CollabVmServerMessage::RegisterAccountResponse::
        RegisterAccountError::USERNAME_TAKEN;
  }

  const auto salt = GeneratePasswordSalt();
  const auto hash = HashPassword(password, salt);
  User user(std::move(username_ptr), hash, salt,
            totp_key ? nullptr
                     : reinterpret_cast<const std::uint8_t*>(totp_key->data()),
            ip_address);
  try {
    odb::transaction t(db_.begin());
    // Make the first user an admin
    user.IsAdmin = !db_.query_value<UserCount>().count;
    db_.persist(user);
    t.commit();
  } catch (const odb::object_already_persistent&) {
    // The username was registered after the first check
    return CollabVmServerMessage::RegisterAccountResponse::
        RegisterAccountError::USERNAME_TAKEN;
  }
  return CollabVmServerMessage::RegisterAccountResponse::RegisterAccountError::
      SUCCESS;
}

std::tuple<std::string, bool, SessionId, SessionId> Database::CreateSession(
    const std::string& username,
    const std::array<std::uint8_t, 16>& ip_address) {
  const auto last_login =
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count();
  User user;
  bool success;
  SessionId old_session_id;
  SessionId new_session_id;
  do {
    new_session_id = GenerateSessionId();
    try {
      odb::transaction t(db_.begin());
      if (!db_.query_one<User>(odb::query<User>::Username == username, user) ||
          user.IsDisabled) {
        return {};
      }
      user.LastLogin = last_login;
      user.LastActiveIpAddr = ip_address;
      old_session_id = new_session_id;
      user.SessionId = std::vector<std::uint8_t>(new_session_id.cbegin(),
                                                 new_session_id.cend());
      db_.update(user);
      t.commit();
      success = true;
    } catch (const odb::object_already_persistent&) {
      // Session ID already exists so generate another one and try again
      success = false;
    }
  } while (!success);
  return {std::move(user.Username->Username), user.IsAdmin,
          std::move(old_session_id), std::move(new_session_id)};
}

std::pair<CollabVmServerMessage::LoginResponse::LoginResult,
          std::vector<uint8_t>>
Database::Login(const std::string& username, const std::string& password) {
  User user;
  {
    odb::transaction t(db_.begin());
    if (!db_.query_one<User>(odb::query<User>::Username == username, user)) {
      return {
          CollabVmServerMessage::LoginResponse::LoginResult::INVALID_USERNAME,
          {}};
    }
  }
  if (user.IsDisabled) {
    return {CollabVmServerMessage::LoginResponse::LoginResult::ACCOUNT_DISABLED,
            {}};
  }
  const auto hash = HashPassword(password, user.PasswordSalt_);
  if (!std::equal(user.PasswordHash_.cbegin(), user.PasswordHash_.cend(),
                  hash.cbegin())) {
    return {CollabVmServerMessage::LoginResponse::LoginResult::INVALID_PASSWORD,
            {}};
  }
  if (!user.TotpKey.empty()) {
    return {
        CollabVmServerMessage::LoginResponse::LoginResult::TWO_FACTOR_REQUIRED,
        std::move(user.TotpKey)};
  }
  return {CollabVmServerMessage::LoginResponse::LoginResult::SUCCESS, {}};
}

bool Database::CreateReservedUsername(const std::string& username) {
  try {
    odb::transaction t(db_.begin());
    db_.persist(UnavailableUsername(username));
    t.commit();
  } catch (const odb::object_already_persistent&) {
    return false;
  }
  return true;
}

std::optional<Database::InviteId> Database::CreateInvite(
    const std::string& invite_name,
    const std::string& username,
    const bool username_already_reserved,
    const bool is_admin) {
  auto username_ptr = std::make_unique<UnavailableUsername>(username);
  try {
    odb::transaction t(db_.begin());
    db_.persist(*username_ptr);
    t.commit();
  } catch (const odb::object_already_persistent&) {
    if (username_already_reserved) {
      return {};
    }
  }

  UserInvite invite;
  invite.InviteName = invite_name;
  invite.Username = std::move(username_ptr);
  invite.IsAdmin = is_admin;
  InviteId invite_id;
  auto success = false;
  do {
    invite_id = GenerateInviteId();
    try {
      odb::transaction t(db_.begin());
      invite.Id = std::string(reinterpret_cast<const char*>(invite_id.data()),
                              invite_id.size());
      db_.persist(invite);
      t.commit();
      success = true;
    } catch (const odb::object_already_persistent&) {
      // Session ID already exists so generate another one and try again
      success = false;
    }
  } while (!success);

  return invite_id;
}

bool Database::DeleteReservedUsername(const std::string& username) {
  try {
    odb::transaction t(db_.begin());
    db_.erase<UnavailableUsername>(username);
    t.commit();
  } catch (const odb::object_not_persistent&) {
    return false;
  }
  return true;
}

bool Database::UpdateInvite(const std::string& id,
                            const std::string& username,
                            const bool is_admin) {
  UserInvite invite;
  invite.Id = id;
  invite.Username = std::make_unique<UnavailableUsername>(username);
  invite.IsAdmin = is_admin;
  try {
    odb::transaction t(db_.begin());
    db_.update(invite);
    t.commit();
  } catch (const odb::object_not_persistent&) {
    return false;
  }
  return true;
}

bool Database::DeleteInvite(const std::string& id) {
  try {
    odb::transaction t(db_.begin());
    db_.erase<UserInvite>(id);
    t.commit();
  } catch (const odb::object_not_persistent&) {
    return false;
  }
  return true;
}

void Database::AddVm(std::shared_ptr<VmConfig>& vm) {
  odb::transaction t(db_.begin());

  db_.persist(*vm);

  t.commit();

  // Add the VM to the map
  //  VirtualMachines[vm->Name] = vm;
}

void Database::UpdateVm(std::shared_ptr<VmConfig>& vm) {
  odb::transaction t(db_.begin());

  db_.update(*vm);

  t.commit();

  // VirtualMachines[vm->name] = vm;
}

void Database::RemoveVm(const std::string& name) {
  //  auto it = VirtualMachines.find(name);
  //  if (it == VirtualMachines.end())
  return;

  // The name parameter could be a reference to the key in the map
  // so the VM should be erased from the database first, and then
  // from the map
  odb::transaction t(db_.begin());

  //  db_.erase<VmConfig>(name);

  t.commit();

  //  VirtualMachines.erase(it);
}

}  // namespace CollabVm::Server
