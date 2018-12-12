#include "Recaptcha.hpp"

int main() {
  constexpr auto recaptcha_key = "6LeIxAcTAAAAAGG-vFI1TnRWxMZNFuojJ4WifJWe";
  namespace ssl = boost::asio::ssl;
  ssl::context ssl_ctx(ssl::context::sslv23);
  ssl_ctx.set_verify_mode(ssl::context::verify_none);
  boost::asio::io_context io_context(1);
  CollabVm::Server::RecaptchaVerifier verifier(io_context, ssl_ctx, recaptcha_key);
  auto success = false;
  verifier.Verify("asdf", [&success](bool is_valid) { success = is_valid; });
  verifier.Verify("1234", [&success](bool is_valid) { success &= is_valid; });
  io_context.run();
  return success ? 0 : 1;
}