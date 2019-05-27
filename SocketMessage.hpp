#pragma once

#include <memory>
#include <capnp/serialize.h>

namespace CollabVm::Server {

struct CopiedSocketMessage;
struct SharedSocketMessage;

struct SocketMessage : std::enable_shared_from_this<SocketMessage>
{
protected:
  ~SocketMessage() = default;

public:
  virtual std::vector<boost::asio::const_buffer>& GetBuffers() = 0;
  virtual void CreateFrame() = 0;

  static std::shared_ptr<SharedSocketMessage> CreateShared() {
    return std::make_shared<SharedSocketMessage>();
  }

  static std::shared_ptr<CopiedSocketMessage> CopyFromMessageBuilder(
    capnp::MallocMessageBuilder& message_builder) {
    return std::make_shared<CopiedSocketMessage>(message_builder);
  }
};

struct SharedSocketMessage final : SocketMessage
{
  std::vector<boost::asio::const_buffer>& GetBuffers() override {
    assert(!framed_buffers_.empty());
    return framed_buffers_;
  }
  // An alternate implementation of capnp::messageToFlatArray()
  // that doesn't copy segment data
  void CreateFrame() override {
    if (!framed_buffers_.empty()) {
      return;
    }
    auto segments = shared_message_builder.getSegmentsForOutput();
    const auto segment_count = segments.size();
    const auto frame_size = (segment_count + 2) & ~size_t(1);
    frame_.reserve(frame_size);
    frame_.push_back(segment_count - 1);
    framed_buffers_.reserve(segment_count + 1);
    framed_buffers_.push_back({
        frame_.data(), frame_size * sizeof(decltype(frame_)::value_type)
      });
    for (auto segment : segments) {
      frame_.push_back(segment.size());
      const auto segment_bytes = segment.asBytes();
      framed_buffers_.push_back(
        { segment_bytes.begin(), segment_bytes.size() });
    }
    if (segment_count % 2 == 0) {
      // Set padding byte
      frame_.push_back(0);
    }
  }

  ~SharedSocketMessage() = default;

  capnp::MallocMessageBuilder& GetMessageBuilder() {
    assert(frame_.empty() && framed_buffers_.empty());
    return shared_message_builder;
  }

private:
  std::vector<std::uint32_t> frame_;
  capnp::MallocMessageBuilder shared_message_builder;
  std::vector<boost::asio::const_buffer> framed_buffers_;
};

struct CopiedSocketMessage final : SocketMessage {
  CopiedSocketMessage(capnp::MallocMessageBuilder& message_builder)
    : buffer_(capnp::messageToFlatArray(message_builder)),
    framed_buffers_(
      { boost::asio::const_buffer(buffer_.asBytes().begin(),
                                 buffer_.asBytes().size()) }) {}

  virtual ~CopiedSocketMessage() = default;

  std::vector<boost::asio::const_buffer>& GetBuffers() override {
    return framed_buffers_;
  }
  void CreateFrame() override {
  }
private:
  kj::Array<capnp::word> buffer_;
  std::vector<boost::asio::const_buffer> framed_buffers_;
};

}
