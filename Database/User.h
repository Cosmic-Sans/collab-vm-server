#pragma once

#include <algorithm>
#include <array>
#include <boost/preprocessor.hpp>
#include <odb/nullable.hxx>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifdef _MSC_VER
// Disable unknown pragma warnings
#pragma warning(push)
#pragma warning(disable : 4068)
#endif

// IPv4 addresses are mapped and stored as IPv6 addresses
// as described in RFC 4291
typedef std::array<uint8_t, 16> IpAddr;
#pragma db value(IpAddr) type("BLOB(16)")

typedef std::array<uint8_t, 16> SessionId;
#pragma db value(SessionId) type("BLOB(16)")


//#pragma db value(std::chrono::seconds) type("BIGINT")
#pragma db object
struct UnavailableUsername {
	UnavailableUsername() { }
	UnavailableUsername(const std::string& username) : Username(username) { }
#pragma db id
	std::string Username;
};


#pragma db object
struct UserInvite {
#define INVITE_ID_LEN 32
#pragma db id //type("BLOB(" BOOST_PP_STRINGIZE(INVITE_ID_LEN) ")")
//	std::array<std::uint32_t, INVITE_ID_LEN> Id;
	std::string Id;
	std::unique_ptr<UnavailableUsername> Username;
	std::string InviteName;
	bool IsAdmin;
//	bool IsHost;
};

#pragma db object
struct User {
#define PASSWORD_HASH_LEN 32
#define PASSWORD_SALT_LEN 32
#define TOTP_KEY_LEN 20
#define SESSION_ID_LEN 16
	using PasswordHash = std::array<std::uint8_t, PASSWORD_HASH_LEN>;
	using PasswordSalt = std::array<std::uint8_t, PASSWORD_SALT_LEN>;

  User() {}
  User(std::unique_ptr<UnavailableUsername>&& username,
       const PasswordHash& password_hash,
       const PasswordSalt& password_salt,
       const uint8_t totp_key[TOTP_KEY_LEN],
			 const std::array<std::uint8_t, 16>& ip_address,
			 const std::uint64_t registration_date = 
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count())
		: Username(std::move(username)),
		  PasswordHash_(password_hash),
		  PasswordSalt_(password_salt),
		  TotpKey(totp_key,
		          totp_key == nullptr ? totp_key : totp_key + sizeof(totp_key)),
		  RegistrationDate(registration_date),
		  RegistrationIpAddr(ip_address), IsAdmin(false), IsDisabled(false)
	{
	}
#pragma db id auto
	std::uint32_t Id;
#pragma db not_null
	std::unique_ptr<UnavailableUsername> Username;
#pragma db type("BLOB(" BOOST_PP_STRINGIZE(PASSWORD_HASH_LEN) ")")
  PasswordHash PasswordHash_;
#pragma db type("BLOB(" BOOST_PP_STRINGIZE(PASSWORD_SALT_LEN) ")")
  PasswordSalt PasswordSalt_;
#pragma db type("BLOB(" BOOST_PP_STRINGIZE(TOTP_KEY_LEN) ")")
  std::vector<uint8_t> TotpKey;
#pragma db unique type("BLOB(" BOOST_PP_STRINGIZE(SESSION_ID_LEN) ")")
  odb::nullable<std::vector<std::uint8_t>> SessionId;

  uint64_t RegistrationDate;
  IpAddr RegistrationIpAddr;
  IpAddr LastActiveIpAddr;
  uint64_t LastLogin;
  uint64_t LastFailedLogin;
  uint64_t LastOnline;
  uint32_t FailedLogins;

  bool IsAdmin;
  bool IsDisabled;
};

#pragma db view object(User)
struct UserCount
{
  #pragma db column("count(" + User::Username + ")")
  std::size_t count;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
