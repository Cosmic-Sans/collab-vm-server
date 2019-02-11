#pragma once

namespace CollabVm::Server {

struct CopiedSocketMessage;
struct SharedSocketMessage;

struct SocketMessage : std::enable_shared_from_this<SocketMessage>
{
protected:
  ~SocketMessage() = default;

public:
  virtual std::vector<boost::asio::const_buffer>& GetBuffers(
    CapnpMessageFrameBuilder<>&) = 0;

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
  std::vector<boost::asio::const_buffer>& GetBuffers(
    CapnpMessageFrameBuilder<>& frame_builder) override
  {
    auto segments = shared_message_builder.getSegmentsForOutput();
    const auto segment_count = segments.size();
    framed_buffers_.resize(segment_count + 1);
    const auto it = framed_buffers_.begin();
    frame_builder.Init(segment_count);
    const auto& frame = frame_builder.GetFrame();
    *it = boost::asio::const_buffer(frame.data(),
                                    frame.size() * sizeof(frame.front()));
    std::transform(
      segments.begin(), segments.end(), std::next(it),
      [&frame_builder](const kj::ArrayPtr<const capnp::word> a)
      {
        frame_builder.AddSegment(a.size());
        return boost::asio::const_buffer(a.begin(),
                                         a.size() * sizeof(a[0]));
      });
    frame_builder.Finalize(segment_count);
    return framed_buffers_;
  }

  capnp::MallocMessageBuilder& GetMessageBuilder()
  {
    return shared_message_builder;
  }

  ~SharedSocketMessage() = default;
private:
  capnp::MallocMessageBuilder shared_message_builder;
  std::vector<boost::asio::const_buffer> framed_buffers_;
};

struct CopiedSocketMessage final : SocketMessage {
  virtual ~CopiedSocketMessage() = default;

  CopiedSocketMessage(capnp::MallocMessageBuilder& message_builder)
    : buffer_(capnp::messageToFlatArray(message_builder)),
    framed_buffers_(
      { boost::asio::const_buffer(buffer_.asBytes().begin(),
                                 buffer_.asBytes().size()) }) {}

  std::vector<boost::asio::const_buffer>& GetBuffers(
    CapnpMessageFrameBuilder<>&) override {
    return framed_buffers_;
  }
private:
  kj::Array<capnp::word> buffer_;
  std::vector<boost::asio::const_buffer> framed_buffers_;
};

}
