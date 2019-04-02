#pragma once

#include <string_view>
#include <queue>

#include "SocketMessage.hpp"
#include "GuacamoleClient.hpp"

namespace CollabVm::Server {

template<typename TAdminVirtualMachine>
struct CollabVmGuacamoleClient final
  : GuacamoleClient<CollabVmGuacamoleClient<TAdminVirtualMachine>>
{
  CollabVmGuacamoleClient(
    boost::asio::io_context& io_context,
    TAdminVirtualMachine& admin_vm)
    : GuacamoleClient<CollabVmGuacamoleClient>(io_context),
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
    // TODO: Avoid copying by using SharedSocketMessage
    instruction_queue.emplace_back(
      SocketMessage::CopyFromMessageBuilder(message_builder));
  }

  void OnFlush()
  {
    admin_vm_.GetClients(
      [instruction_queue = std::move(instruction_queue)](auto& clients)
      {
        for (auto&& client_ptr : clients)
        {
          client_ptr->QueueMessageBatch([&instruction_queue](auto enqueue)
          {
            for (auto& instruction : instruction_queue)
            {
              enqueue(std::move(instruction));
            }
          });
        }
      });
  }

  TAdminVirtualMachine& admin_vm_;
  std::vector<std::shared_ptr<SocketMessage>> instruction_queue;
};

}
