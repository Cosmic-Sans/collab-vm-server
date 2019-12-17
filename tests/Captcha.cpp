#include "CaptchaVerifier.hpp"

int main() {
  namespace ssl = boost::asio::ssl;
  ssl::context ssl_ctx(ssl::context::sslv23);
  ssl_ctx.set_verify_mode(ssl::context::verify_none);
  boost::asio::io_context io_context(1);
  auto verifier = CollabVm::Server::CaptchaVerifier(io_context, ssl_ctx);
  capnp::MallocMessageBuilder message_builder;
  auto settings = message_builder.initRoot<ServerSetting::Captcha>();
  settings.setEnabled(true);
  settings.setHttps(true);
  settings.setUrlHost("google.com");
  settings.setUrlPort(443);
  settings.setUrlPath("/recaptcha/api/siteverify");
  settings.setPostParams("secret=6LeIxAcTAAAAAGG-vFI1TnRWxMZNFuojJ4WifJWe&response=$TOKEN&remoteip=$IP");
  settings.setValidJSONVariableName("success");
  verifier.SetSettings([settings]() { return settings; });
  
  auto success = false;
  verifier.Verify("asdf", [&success](bool is_valid) { success = is_valid; });
  verifier.Verify("1234", [&success](bool is_valid) { success &= is_valid; });
  io_context.run();
  return !success;
}
