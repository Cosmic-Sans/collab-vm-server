#include "Totp.hpp"
#include <cstddef>
#include <gsl/span>
#include <iomanip>
#include <iostream>

int main() {
  constexpr unsigned char totp[] = {61,  198, 202, 164, 130, 74, 109,
                                    40,  135, 103, 178, 51,  30, 32,
                                    180, 49,  102, 203, 133, 217};
  constexpr unsigned digits = 6;
  std::cout << std::dec << std::setfill('0') << std::setw(digits)
            << CollabVmServer::Totp::GenerateTotp(
                   gsl::make_span(reinterpret_cast<const std::byte*>(totp), sizeof(totp)),
//                   reinterpret_cast<const std::byte&>(totp), sizeof(totp),
                   digits)
            << std::endl;

  return 0;
}