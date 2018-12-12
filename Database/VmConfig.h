#pragma once
#include <cstdint>
#include <odb/core.hxx>
#include <vector>

#pragma db value
struct SettingIDs
{
    std::uint32_t VmId;
    std::uint8_t SettingID;

  bool operator<(const SettingIDs& right) const
  {
    return false;
  }
};

#pragma db object
struct VmConfig {
  // VmConfig() : id_(0), VmId(0), SettingID(0) {}
  VmConfig() : IDs({ 0, 0 })  {}

  /*
  VmConfig(std::uint32_t vm_id,
           std::uint8_t setting_id,
           std::vector<std::uint8_t>&& blob)
      : id_(0),
				VmId(vm_id),
        SettingID(setting_id),
        Setting(std::move(blob)) {}
        */

  VmConfig(std::uint32_t vm_id,
           std::uint8_t setting_id,
           std::vector<std::uint8_t>&& blob)
      : 
    IDs({ vm_id, setting_id }),
        Setting(std::move(blob)) {}

/*
#pragma db id auto
  std::uint32_t id_;
  */
#pragma db id
 SettingIDs IDs;

#pragma db not_null type("BLOB")
  std::vector<std::uint8_t> Setting;
};

#pragma db view object(VmConfig)
struct NewVmId
{
  #pragma db column("max(" + VmConfig::IDs.VmId + ")+1")
  std::uint32_t Id;
};

#pragma db view object(VmConfig)
struct TotalVms
{
  #pragma db column("COUNT(DISTINCT" + VmConfig::IDs.VmId + ")")
  std::uint32_t Count;
};
