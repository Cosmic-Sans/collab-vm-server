#pragma once

#include <boost/asio.hpp>
#include <capnp/serialize.h>
#include <kj/std/iostream.h>
#include <guacamole/timestamp.h>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include "CollabVm.capnp.h"
#include "SocketMessage.hpp"

namespace CollabVm::Server {
template<typename TCallbacks>
struct RecordingController {
  template<typename TExecutionContext>
  RecordingController(TExecutionContext& context, std::uint32_t vm_id)
      : vm_id_(vm_id),
        stop_timer_(context),
        keyframe_timer_(context) {
  }

  void SetRecordingSettings(ServerSetting::Recordings::Reader settings) {
    const auto keyframe_interval = std::chrono::seconds(settings.getKeyframeInterval());
    const auto create_keyframe =
      keyframe_interval_ != keyframe_interval
      || settings_.getCaptureDisplay() != settings.getCaptureDisplay()
      || settings_.getCaptureAudio() != settings.getCaptureAudio();
    settings_message_builder_.setRoot(settings);
    settings_ = settings_message_builder_.getRoot<ServerSetting::Recordings>();
    file_duration_ = std::chrono::minutes(settings.getFileDuration());
    keyframe_interval_ = keyframe_interval;

    if (!IsRecording()) {
      return;
    }
    if (const auto expiration =
          stop_timer_.expiry() - std::chrono::steady_clock::now();
        expiration < file_duration_) {
      Start();
      return;
    }
    if (create_keyframe) {
      StartKeyframeTimer();
    }
  }

  void Start() {
    if (!file_duration_.count()) {
      return;
    }
    auto start_time = Stop();
    if (start_time == std::chrono::time_point<std::chrono::system_clock>::min()) {
      start_time = std::chrono::system_clock::now();
    }
    auto error_code = std::error_code();
    std::filesystem::create_directories(recordings_directory, error_code);
    if (error_code) {
      return;
    }
    const auto date_time = GetCurrentDateTime();
    if (date_time.empty()) {
      return;
    }
    filename_ = std::string(recordings_directory) + "vm"
      + std::to_string(vm_id_) + '_' + date_time + ".bin";
    file_stream_.open(filename_, std::fstream::binary | std::fstream::out);
    if (!IsRecording()) {
      std::cout << "Failed to create recording file \"" << filename_ << '"' << std::endl;
      return;
    }
    auto file_header = file_header_.initRoot<RecordingFileHeader>();
    file_header.setVmId(vm_id_);
    file_header.setStartTime(
      std::chrono::duration_cast<std::chrono::milliseconds>(
        start_time.time_since_epoch()).count());
    auto keyframe_offsets = file_header.initKeyframes(
      keyframe_interval_.count() ? file_duration_ / keyframe_interval_ : 0);
    next_keyframe_offset_ = keyframe_offsets.begin();
    stop_timer_.expires_after(file_duration_);
    stop_timer_.async_wait([this](const auto error_code) {
      if (error_code) {
        Stop();
      } else {
        Start();
      }
    });
    WriteFileHeader();
    static_cast<TCallbacks&>(*this).OnRecordingStarted(start_time);
    StartKeyframeTimer();
  }

  std::chrono::time_point<std::chrono::system_clock> Stop() {
    if (!IsRecording()) {
      return std::chrono::time_point<std::chrono::system_clock>::min();
    }
    keyframe_timer_.cancel();
    stop_timer_.cancel();
    const auto now = std::chrono::system_clock::now();
    auto file_header = file_header_.getRoot<RecordingFileHeader>();
    file_header.setStopTime(
      std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count());
    WriteFileHeader();
    file_stream_.close();
    static_cast<TCallbacks&>(*this).OnRecordingStopped(now);
    filename_ = "";
    return now;
  }

  [[nodiscard]]
  bool IsRecording() const {
    return file_stream_.is_open() && file_stream_.good();
  }

  [[nodiscard]]
  std::string_view GetFilename() const {
    return filename_;
  }

  void WriteMessage(SocketMessage& message) {
    auto collab_vm_message = message.GetRoot<CollabVmServerMessage::Message>();
    if (!IsRecording()
        || !ShouldRecordMessage(collab_vm_message)) {
      return;
    }
    IncludeTimestamp(collab_vm_message);
    message.CreateFrame();
    for (auto&& buffer : message.GetBuffers()) {
      file_stream_.write(
        static_cast<const char*>(buffer.data()), buffer.size());
    }
  }

  void WriteMessage(capnp::MessageBuilder& message_builder) {
    auto message = message_builder.getRoot<CollabVmServerMessage::Message>();
    if (!IsRecording()
        || !ShouldRecordMessage(message)) {
      return;
    }
    auto output_stream = kj::std::StdOutputStream(file_stream_);
    IncludeTimestamp(message);
    capnp::writeMessage(output_stream, message_builder);
  }


  void WriteMessage(
      Guacamole::GuacClientInstruction::Reader guacamole_instruction) {
    if (!IsRecording() || !settings_.getCaptureInput()) {
      return;
    }
    if (guacamole_instruction.which() == Guacamole::GuacClientInstruction::MOUSE) {
      auto message_builder = capnp::MallocMessageBuilder();
      auto server_mouse = message_builder
        .initRoot<CollabVmServerMessage>()
        .initMessage()
        .initGuacInstr()
        .initMouse();
      auto client_mouse = guacamole_instruction.getMouse();
      server_mouse.setX(client_mouse.getX());
      server_mouse.setY(client_mouse.getY());
      server_mouse.setButtonMask(client_mouse.getButtonMask());
      server_mouse.setTimestamp(
        std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count());
      WriteMessage(message_builder);
    } else if (guacamole_instruction.which() == Guacamole::GuacClientInstruction::KEY) {
      auto message_builder = capnp::MallocMessageBuilder();
      auto client_key = guacamole_instruction.getKey();
      auto server_key = message_builder
        .initRoot<CollabVmServerMessage>()
        .initMessage()
        .initGuacInstr()
        .initKey();
      server_key.setKeysym(client_key.getKeysym());
      server_key.setPressed(client_key.getPressed());
      server_key.setTimestamp(
        std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count());
      WriteMessage(message_builder);
    }
  }

private:
  [[nodiscard]]
  bool ShouldRecordMessage(CollabVmServerMessage::Message::Reader message) {
    if (message.which() != CollabVmServerMessage::Message::GUAC_INSTR) {
      return true;
    }
    auto guacamole_instruction = message.getGuacInstr();
    switch (guacamole_instruction.which()) {
    case Guacamole::GuacServerInstruction::SYNC:
      return settings_.getCaptureDisplay()
          || settings_.getCaptureInput()
          || settings_.getCaptureAudio();
    case Guacamole::GuacServerInstruction::BLOB:
      return !ignored_streams_.count(guacamole_instruction.getBlob().getStream());
    case Guacamole::GuacServerInstruction::END:
      return !ignored_streams_.erase(guacamole_instruction.getEnd());
    case Guacamole::GuacServerInstruction::AUDIO:
      if (settings_.getCaptureAudio()) {
        return true;
      }
      ignored_streams_.insert(guacamole_instruction.getAudio().getStream());
      return false;
    case Guacamole::GuacServerInstruction::MOUSE:
    case Guacamole::GuacServerInstruction::KEY:
      return settings_.getCaptureInput();
    default:
      if (!settings_.getCaptureDisplay()) {
        // Assume all other instructions are display-related
        return false;
      }
      break;
    }
    return true;
  }

  void IncludeTimestamp(CollabVmServerMessage::Message::Reader message) {
    switch (message.which()) {
    case CollabVmServerMessage::Message::ADMIN_USER_LIST_ADD:
    case CollabVmServerMessage::Message::USER_LIST_REMOVE:
    case CollabVmServerMessage::Message::CHANGE_USERNAME:
    case CollabVmServerMessage::Message::VM_TURN_INFO:
    case CollabVmServerMessage::Message::VOTE_RESULT:
    {
      timestamp_message_builder
        .initRoot<CollabVmServerMessage::Message>()
        .setRecordingTimestamp(
          std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
      auto output_stream = kj::std::StdOutputStream(file_stream_);
      capnp::writeMessage(output_stream, timestamp_message_builder);
    }
    default:
      break;
    }
  }

  void StartKeyframeTimer() {
    ignored_streams_.clear();
    static_cast<TCallbacks&>(*this).OnKeyframeInRecording();

    if (!keyframe_interval_.count()) {
      keyframe_timer_.cancel();
      return;
    }
    keyframe_timer_.expires_after(keyframe_interval_);
    keyframe_timer_.async_wait([this](const auto error_code) {
      if (error_code) {
        return;
      }
      auto file_header = file_header_.getRoot<RecordingFileHeader>();
      auto keyframe_offsets = file_header.getKeyframes();
      if (next_keyframe_offset_ == keyframe_offsets.end()) {
        Start();
        return;
      }

      auto keyframe =
        keyframe_offsets[next_keyframe_offset_ - keyframe_offsets.begin()];
      keyframe.setFileOffset(file_stream_.tellp());
      keyframe.setTimestamp(
        std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count());
      ++next_keyframe_offset_;
      file_header.setKeyframesCount(file_header.getKeyframesCount() + 1);
      WriteFileHeader();
      StartKeyframeTimer();
    });
  };

  void WriteFileHeader() {
    const auto original_position = file_stream_.tellp();
    file_stream_.seekp(0);
    auto output_stream = kj::std::StdOutputStream(file_stream_);
    capnp::writeMessage(output_stream, file_header_);
    if (original_position) {
      file_stream_.seekp(original_position);
    }
  }

  static std::string GetCurrentDateTime() {
    const auto now = std::time(nullptr);
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%I-%M-%S_%p", std::localtime(&now));
    return buffer;
  }

  const std::uint32_t vm_id_;
  std::ofstream file_stream_;
  capnp::MallocMessageBuilder file_header_;
  capnp::List<RecordingFileHeader::Keyframe>::Builder::Iterator next_keyframe_offset_;
  boost::asio::steady_timer stop_timer_;
  boost::asio::steady_timer keyframe_timer_;
  std::chrono::minutes file_duration_ = std::chrono::minutes::zero();
  std::chrono::seconds keyframe_interval_ = std::chrono::seconds::zero();
  std::string filename_;
  capnp::MallocMessageBuilder settings_message_builder_;
  ServerSetting::Recordings::Reader settings_ = settings_message_builder_.initRoot<ServerSetting::Recordings>();
  capnp::MallocMessageBuilder timestamp_message_builder;
  std::unordered_set<std::int32_t> ignored_streams_;
  static constexpr std::string_view recordings_directory = "./recordings/";
};
}
