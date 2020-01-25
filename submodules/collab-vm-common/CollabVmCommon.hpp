#include <algorithm>
#include <cctype>
#include <chrono>
#include <string_view>

namespace CollabVm::Common {

constexpr static auto min_username_len = 3;
constexpr static auto max_username_len = 20;
constexpr static auto max_chat_message_len = 100;
constexpr static auto chat_rate_limit = std::chrono::seconds(2);
constexpr static auto username_change_rate_limit = std::chrono::seconds(30);
constexpr static auto vote_limit = 5;

bool ValidateUsername(const std::string_view username) {
  if (username.length() < min_username_len ||
      username.length() > max_username_len) {
    return false;
  }
  auto prev_symbol = false;
  const auto is_char_allowed = [&prev_symbol](const char c,
                                              const bool allow_space = true) {
    const static std::string allowed_symbols = "_-.?!";
    if (std::isalnum(c)) {
      prev_symbol = false;
      return true;
    }
    const auto is_symbol =
        std::find(allowed_symbols.cbegin(), allowed_symbols.cend(), c) !=
            allowed_symbols.cend() ||
        allow_space && c == ' ';
    if (!is_symbol || prev_symbol) {
      return false;
    }
    prev_symbol = true;
    return true;
  };
  return is_char_allowed(username.front(), false) &&
         std::all_of(std::next(username.cbegin()), std::prev(username.cend()),
                     is_char_allowed) &&
         is_char_allowed(username.back(), false);
  // Alternatively, this beautiful regular expression could be used:
  // "^(?:[a-zA-Z0-9]|[_\-\.!\?](?!_|-|\.| |!|\?))"
  // "(?:[a-zA-Z0-9]|[_\-\.!\? ](?!_|-|\.| |!|\?)){1,18}"
  // "[a-zA-Z0-9_\-\.!\?]$"
}

}