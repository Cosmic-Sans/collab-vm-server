#pragma once

#include <memory>
#include <capnp/serialize.h>

namespace CollabVm::Server {

struct CopiedSocketMessage;
struct SharedSocketMessage;

struct SocketMessage : std::enable_shared_from_this<SocketMessage>
{
  virtual ~SocketMessage() noexcept = default;

  virtual const std::vector<boost::asio::const_buffer>& GetBuffers() const = 0;
  virtual void CreateFrame() = 0;
  virtual capnp::AnyPointer::Reader GetRoot() const = 0;
  template<typename T>
  typename T::Reader GetRoot() const {
    return GetRoot().getAs<T>();
  }

  static std::shared_ptr<SharedSocketMessage> CreateShared() {
    return std::make_shared<SharedSocketMessage>();
  }

  static std::shared_ptr<CopiedSocketMessage> CopyFromMessageBuilder(
    capnp::MessageBuilder& message_builder) {
    return std::make_shared<CopiedSocketMessage>(message_builder);
  }
};

struct SharedSocketMessage final : SocketMessage
{
  const std::vector<boost::asio::const_buffer>& GetBuffers() const override {
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

  capnp::AnyPointer::Reader GetRoot() const override {
    return const_cast<capnp::MallocMessageBuilder&>(
      shared_message_builder).getRoot<capnp::AnyPointer>();
  }

  ~SharedSocketMessage() noexcept override { }

  capnp::MessageBuilder& GetMessageBuilder() {
    assert(frame_.empty() && framed_buffers_.empty());
    return shared_message_builder;
  }

private:
  std::vector<std::uint32_t> frame_;
  capnp::MallocMessageBuilder shared_message_builder;
  std::vector<boost::asio::const_buffer> framed_buffers_;
};

struct CopiedSocketMessage final : SocketMessage {
  CopiedSocketMessage(capnp::MessageBuilder& message_builder)
    : buffer_(capnp::messageToFlatArray(message_builder)),
      framed_buffers_(
        { boost::asio::const_buffer(buffer_.asBytes().begin(),
          buffer_.asBytes().size()) }),
      reader_(buffer_) {
  }

  ~CopiedSocketMessage() noexcept override { }

  const std::vector<boost::asio::const_buffer>& GetBuffers() const override {
    return framed_buffers_;
  }
  void CreateFrame() override {
  }
  capnp::AnyPointer::Reader GetRoot() const override {
    return const_cast<capnp::FlatArrayMessageReader&>(
      reader_).getRoot<capnp::AnyPointer>();
  }
private:
  const kj::Array<capnp::word> buffer_;
  const std::vector<boost::asio::const_buffer> framed_buffers_;
  capnp::FlatArrayMessageReader reader_;
};

}
