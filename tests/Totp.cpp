#include "Totp.hpp"
#include <array>
#include <cstddef>
#include <iomanip>
#include <iostream>

template<typename... T>
constexpr auto make_byte_array(T... t)
{
  return std::array<std::byte, sizeof...(T)>{std::byte(t)...};
}

int main() {
  constexpr auto byte_array =
      make_byte_array(61,  198, 202, 164, 130, 74, 109,
                      40,  135, 103, 178, 51,  30, 32,
                      180, 49,  102, 203, 133, 217);
  constexpr auto digits = 6;

  std::cout << std::dec << std::setfill('0') << std::setw(digits)
            << CollabVm::Server::Totp::GenerateTotp(byte_array, digits)
            << std::endl;

  return 0;
}
