#pragma once
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <algorithm>
#include <boost/endian/conversion.hpp>
#include <boost/range/counting_range.hpp>
#include <chrono>
#include <cstddef>
#include <gsl/span>

// Implements RFC 6238 (TOTP) and RFC 4226 (HOTP) for SHA1
namespace CollabVm::Server::Totp {
inline int GenerateTotp(
    const gsl::span<const std::byte> key,
    const int digits = 6,
    const std::chrono::seconds time_step = std::chrono::seconds(30),
    const int time_window = 0,
    const std::chrono::seconds timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())) {
  const auto timer =
      boost::endian::native_to_big(timestamp / time_step - time_window);
  const auto digest =
      gsl::make_span(HMAC(EVP_sha1(), key.data(), key.size(),
                          reinterpret_cast<const uint8_t*>(&timer),
                          sizeof(timer), nullptr, nullptr),
                     SHA_DIGEST_LENGTH);
	constexpr unsigned long lowest_4_bits = (1 << 4) - 1;
  const int offset = digest[SHA_DIGEST_LENGTH - 1] & lowest_4_bits;
  constexpr unsigned long lowest_31_bits = (1 << 31) - 1;
  const int binary = boost::endian::endian_reverse(
                         *reinterpret_cast<const int*>(&digest[offset])) &
                     lowest_31_bits;
  return binary % static_cast<int>(std::pow(10, digits));
}

inline bool ValidateTotp(
    const int input,
    const gsl::span<const std::byte> key,
    const int digits = 6,
    const std::chrono::seconds time_step = std::chrono::seconds(30),
    const int time_window = 1) {
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch());
  const auto counter = boost::counting_range(0, time_window + 1);
  return std::any_of(
      counter.begin(), counter.end(), [&](int current_time_window) {
        return GenerateTotp(key, digits, time_step, current_time_window, now) ==
               input;
      });
}
}  // namespace CollabVmServer::Totp
