#pragma once

#include <mutex>
#include <queue>
#include <string_view>

#include "SocketMessage.hpp"
#include "GuacamoleClient.hpp"

namespace CollabVm::Server {

template<typename TAdminVirtualMachine>
struct CollabVmGuacamoleClient final
  : GuacamoleClient<CollabVmGuacamoleClient<TAdminVirtualMachine>>
{
  CollabVmGuacamoleClient(
    boost::asio::io_context::strand& execution_context,
    TAdminVirtualMachine& admin_vm)
    : GuacamoleClient<CollabVmGuacamoleClient>(execution_context),
      admin_vm_(admin_vm)
  {
  }

  void OnStart()
  {
    admin_vm_.OnStart();
  }

  void OnStop()
  {
    admin_vm_.OnStop();
  }

  void OnLog(const std::string_view message)
  {
    std::cout << message << std::endl;
  }

  void OnInstruction(capnp::MallocMessageBuilder& message_builder)
  {
    // TODO: Avoid copying
    auto guac_instr =
      message_builder.getRoot<Guacamole::GuacServerInstruction>();
    auto socket_message = SocketMessage::CreateShared();
    socket_message->GetMessageBuilder()
                  .initRoot<CollabVmServerMessage>()
                  .initMessage()
                  .setGuacInstr(guac_instr);
    socket_message->CreateFrame();

    const auto lock = std::lock_guard(instruction_queue_mutex_);
    instruction_queue_.emplace_back(std::move(socket_message));
  }

  void OnFlush()
  {
    auto lock = std::unique_lock(instruction_queue_mutex_);
    if (instruction_queue_.empty()) {
      return;
    }
    auto instruction_queue =
      std::make_shared<std::vector<std::shared_ptr<SharedSocketMessage>>>(
        std::move(instruction_queue_));
    lock.unlock();

    admin_vm_.OnGuacamoleInstructions(std::move(instruction_queue));
  }

  TAdminVirtualMachine& admin_vm_;
  std::vector<std::shared_ptr<SharedSocketMessage>> instruction_queue_;
  std::mutex instruction_queue_mutex_;
};

}
