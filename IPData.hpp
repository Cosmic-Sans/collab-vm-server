#pragma once

#include <chrono>
#include <cstdint>

namespace CollabVm::Server
{
/**
 * Data associated with a user's IP address to be used for spam prevention.
 */
struct IPData
{
  /**
   * The number of active connections from the IP.
   */
  std::uint8_t connections = 0;

  /**
   * IP data associated with a VM.
   */
  struct ChannelData
  {
  };
};
} // namespace CollabVm::Server
