#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <odb/core.hxx>

#ifdef ODB_COMPILER
#pragma db model version(1, 1)
#endif

#pragma db object
struct ServerConfig
{
  ServerConfig(): ID(0) { }

  ServerConfig(std::uint8_t id, std::vector<std::uint8_t>&& blob) :
		ID(id),
    Setting(std::move(blob))
	{
	}

#pragma db id
	std::uint8_t ID;

#pragma db not_null type("BLOB")
  std::vector<std::uint8_t> Setting;
};
