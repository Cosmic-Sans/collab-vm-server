//typedef bool pthread_mutex_t;
#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <emscripten/wire.h>
#include <cstdint>
#include <set>
#include <iostream>
#include <memory>
#include <string>
#include <iterator>
#include <type_traits>
#include <vector>
#include <algorithm>
#include "guac-bindings.hpp"
#include "pthread-noop.h"
#define pthread_mutex_t int
#include <capnp/dynamic.h>
#include <capnp/serialize.h>
#include "CollabVm.capnp.h"
#include "CollabVmCommon.hpp"

//char (*_nop)[sizeof(pthread_mutex_t)] = 1;

struct GuacamoleParameterWrapper {
  GuacamoleParameterWrapper() = default;
  GuacamoleParameterWrapper(std::string&& name, std::string&& value)
  : name_(std::move(name)), value_(std::move(value)) {}
        std::string getName() const { return name_; }
        void setName(std::string&& name) { name_ = std::move(name); }
        std::string getValue() const { return value_; }
        void setValue(std::string&& value) { value_ = std::move(value); }
private:
        std::string name_;
        std::string value_;
};

struct VmInfoWrapper {
	VmInfoWrapper() {}
	VmInfoWrapper(CollabVmServerMessage::VmInfo::Reader vmInfo) : vmInfo_(vmInfo) {}
  std::uint32_t getId() const { return vmInfo_.getId(); }
	void setId(std::uint32_t&& id) { id_ = std::move(id); }
	std::string getName() const { return vmInfo_.getName(); }
	void setName(std::string&& name) { name_ = std::move(name); }
	std::string getHost() const { return vmInfo_.getHost(); }
	void setHost(std::string&& host) { host_ = std::move(host); }
	std::string getAddress() const { return vmInfo_.getAddress(); }
	void setAddress(std::string&& address) { address_ = std::move(address); }
	std::string getOperatingSystem() const { return vmInfo_.getOperatingSystem(); }
	void setOperatingSystem(std::string&& operatingSystem) { operatingSystem_ = std::move(operatingSystem); }
	bool getUploads() const { return vmInfo_.getUploads(); }
	void setUploads(bool&& uploads) { uploads_ = std::move(uploads); }
	bool getInput() const { return vmInfo_.getInput(); }
	void setInput(bool&& input) { input_ = std::move(input); }
	std::uint8_t getRam() const { return vmInfo_.getRam(); }
	void setRam(std::uint8_t&& ram) { ram_ = std::move(ram); }
	std::uint8_t getDiskSpace() const { return vmInfo_.getDiskSpace(); }
	void setDiskSpace(std::uint8_t&& disk) { disk_ = std::move(disk); }
	bool getSafeForWork() const { return vmInfo_.getSafeForWork(); }
	void setSafeForWork(bool&& safe_for_work) { safe_for_work_ = std::move(safe_for_work); }
  std::uint16_t getViewerCount() const { return vmInfo_.getViewerCount(); }
	void setViewerCount(std::uint16_t&& viewer_count) { viewer_count_ = std::move(viewer_count); }
private:
	CollabVmServerMessage::VmInfo::Reader vmInfo_;
  std::uint32_t id_;
	std::string name_;
	std::string host_;
	std::string address_;
	std::string operatingSystem_;
	bool uploads_;
	bool input_;
	std::uint8_t ram_;
	std::uint8_t disk_;
  bool safe_for_work_;
  std::uint16_t viewer_count_;
};

struct AdminVmInfoWrapper {
        AdminVmInfoWrapper() {}
        AdminVmInfoWrapper(CollabVmServerMessage::AdminVmInfo::Reader adminVmInfo) : adminVmInfo_(adminVmInfo) {}
        std::uint32_t getId() const { return adminVmInfo_.getId(); }
        void setId(std::uint32_t&& id) { id_ = std::move(id); }
        std::string getName() const { return adminVmInfo_.getName(); }
        void setName(std::string&& name) { name_ = std::move(name); }
        std::uint8_t getStatus() const { return static_cast<std::uint8_t>(adminVmInfo_.getStatus()); }
        void setStatus(std::uint8_t&& status) { status_ = std::move(status); }
private:
        CollabVmServerMessage::AdminVmInfo::Reader adminVmInfo_;
        std::uint32_t id_;
        std::string name_;
        std::uint8_t status_;
};

struct LoginWrapper {
	LoginWrapper() {}
	LoginWrapper(CollabVmClientMessage::Login::Reader login) : login_(login) {}
	std::string getUsername() const { return login_.getUsername(); }
	void setUsername(std::string&& username) { username_ = std::move(username); }
	std::string getPassword() const { return login_.getPassword(); }
	void setPassword(std::string&& password) { password_ = std::move(password); }
private:
	CollabVmClientMessage::Login::Reader login_;
	std::string username_;
	std::string password_;
};

struct RegisterAccountWrapper {
        RegisterAccountWrapper() {}
        RegisterAccountWrapper(CollabVmClientMessage::RegisterAccount::Reader registerAccount) : registerAccount_(registerAccount) {}
        std::string getUsername() const { return registerAccount_.getUsername(); }
        void  setUsername(std::string&& username) { username_ = std::move(username); }
        std::string getPassword() const { return registerAccount_.getPassword(); }
        void  setPassword(std::string&& password) { password_ = std::move(password); }
        std::string getTwoFactorToken() const { return std::string(reinterpret_cast<const char*>(registerAccount_.getTwoFactorToken().begin()), registerAccount_.getTwoFactorToken().size()); }
        void  setTwoFactorToken(std::string&& twoFactorToken) { twoFactorToken_ = std::move(twoFactorToken); }
private:
        CollabVmClientMessage::RegisterAccount::Reader registerAccount_;
        std::string username_;
        std::string password_;
        std::string twoFactorToken_;
};

struct UserInviteWrapper {
        UserInviteWrapper() {}
        UserInviteWrapper(CollabVmClientMessage::UserInvite::Reader userInvite) : userInvite_(userInvite) {}
        std::string getId() const { return std::string(reinterpret_cast<const char*>(userInvite_.getId().begin()), userInvite_.getId().size()); }
        void  setId(std::string&& id) { id_ = std::move(id); }
        std::string getInviteName() const { return userInvite_.getInviteName(); }
        void  setInviteName(std::string&& inviteName) { inviteName_ = std::move(inviteName); }
        std::string getUsername() const { return userInvite_.getUsername(); }
        void  setUsername(std::string&& username) { username_ = std::move(username); }
        bool getAdmin() const { return userInvite_.getAdmin(); }
        void  setAdmin(bool&& admin) { admin_ = std::move(admin); }
        CollabVmClientMessage::UserInvite::Reader userInvite_;
        std::string id_;
        std::string inviteName_;
        std::string username_;
        bool admin_;
};

struct CaptchaWrapper {
        CaptchaWrapper() {}
        CaptchaWrapper(ServerSetting::Captcha::Reader captcha) {
          enabled_ = captcha.getEnabled();
          https_ = captcha.getHttps();
          urlHost_ = captcha.getUrlHost();
          urlPort_ = captcha.getUrlPort();
          urlPath_ = captcha.getUrlPath();
          postParams_ = captcha.getPostParams();
          validJSONVariableName_ = captcha.getValidJSONVariableName();
        }

        bool getEnabled() const { return enabled_; }
        void setEnabled(bool&& enabled) { enabled_ = std::forward<bool>(enabled); }

        bool getHttps() const { return https_; }
        void setHttps(bool&& https) { https_ = std::forward<bool>(https); }

        std::string getUrlHost() const { return urlHost_; }
        void setUrlHost(std::string&& urlHost) { urlHost_ = std::forward<std::string>(urlHost); }

        std::uint16_t getUrlPort() const { return urlPort_; }
        void setUrlPort(std::uint16_t&& urlPort) { urlPort_ = std::forward<std::uint16_t>(urlPort); }

        std::string getUrlPath() const { return urlPath_; }
        void setUrlPath(std::string&& urlPath) { urlPath_ = std::forward<std::string>(urlPath); }

        std::string getPostParams() const { return postParams_; }
        void setPostParams(std::string&& postParams) { postParams_ = std::forward<std::string>(postParams); }

        std::string getValidJSONVariableName() const { return validJSONVariableName_; }
        void setValidJSONVariableName(std::string&& validJSONVariableName) { validJSONVariableName_ = std::forward<std::string>(validJSONVariableName); }

        void serialize(ServerSetting::Captcha::Builder builder) {
          builder.setEnabled(enabled_);
          builder.setHttps(https_);
          builder.setUrlHost(urlHost_);
          builder.setUrlPort(urlPort_);
          builder.setUrlPath(urlPath_);
          builder.setPostParams(postParams_);
          builder.setValidJSONVariableName(validJSONVariableName_);
        }
private:
        bool enabled_;
        bool https_;
        std::string urlHost_;
        std::uint16_t urlPort_;
        std::string urlPath_;
        std::string postParams_;
        std::string validJSONVariableName_;
};

struct ServerSettingsWrapper {
    ServerSettingsWrapper(){}
    ServerSettingsWrapper(capnp::List<ServerSetting>::Reader settings) {
        for (auto&& server_setting : settings) {
            const auto setting = server_setting.getSetting();
            switch (setting.which()) {
            case ServerSetting::Setting::ALLOW_ACCOUNT_REGISTRATION:
                allowAccountRegistration_ = setting.getAllowAccountRegistration();
                break;
            case ServerSetting::Setting::CAPTCHA:
                captcha_ = CaptchaWrapper(setting.getCaptcha());
                break;
            case ServerSetting::Setting::CAPTCHA_REQUIRED:
                captchaRequired_ = setting.getCaptchaRequired();
                break;
            case ServerSetting::Setting::USER_VMS_ENABLED:
                userVmsEnabled_ = setting.getUserVmsEnabled();
                break;
            case ServerSetting::Setting::ALLOW_USER_VM_REQUESTS:
                allowUserVmRequests_ = setting.getAllowUserVmRequests();
                break;
            case ServerSetting::Setting::BAN_IP_COMMAND:
                banIpCommand_ = setting.getBanIpCommand();
                break;
            case ServerSetting::Setting::UNBAN_IP_COMMAND:
                unbanIpCommand_ = setting.getUnbanIpCommand();
                break;
            case ServerSetting::Setting::MAX_CONNECTIONS_ENABLED:
                maxConnectionsEnabled_ = setting.getMaxConnectionsEnabled();
                break;
            case ServerSetting::Setting::MAX_CONNECTIONS:
                maxConnections_ = setting.getMaxConnections();
                break;
            }
        }
    }

    void getServerConfigModifications(capnp::MallocMessageBuilder& message_builder) {
            auto message = message_builder.initRoot<CollabVmClientMessage::Message>();
            auto config_mod = message.initServerConfigModifications(modified_settings_.size());
            auto i = 0;
            for (const auto setting_type : modified_settings_) {
                        auto setting = config_mod[i].initSetting();
                        switch (static_cast<const ServerSetting::Setting::Which>(setting_type)) {
            case ServerSetting::Setting::ALLOW_ACCOUNT_REGISTRATION:
                                setting.setAllowAccountRegistration(allowAccountRegistration_);
                break;
            case ServerSetting::Setting::CAPTCHA:
                                captcha_.serialize(setting.initCaptcha());
                break;
            case ServerSetting::Setting::CAPTCHA_REQUIRED:
                                setting.setCaptchaRequired(captchaRequired_);
                break;
            case ServerSetting::Setting::USER_VMS_ENABLED:
                                setting.setUserVmsEnabled(userVmsEnabled_);
                break;
            case ServerSetting::Setting::ALLOW_USER_VM_REQUESTS:
                                setting.setAllowUserVmRequests(allowUserVmRequests_);
                break;
            case ServerSetting::Setting::BAN_IP_COMMAND:
                                setting.setBanIpCommand(banIpCommand_);
                break;
            case ServerSetting::Setting::UNBAN_IP_COMMAND:
                                setting.setUnbanIpCommand(unbanIpCommand_);
                break;
            case ServerSetting::Setting::MAX_CONNECTIONS_ENABLED:
                                setting.setMaxConnectionsEnabled(maxConnectionsEnabled_);
                break;
            case ServerSetting::Setting::MAX_CONNECTIONS:
                                setting.setMaxConnections(maxConnections_);
                break;
                                default:
                                assert(false);
                        }
                        i++;
                }
            modified_settings_.clear();
        }

    bool getAllowAccountRegistration() const {
        return allowAccountRegistration_;
    }
    void setAllowAccountRegistration(bool&& allowAccountRegistration) {
        allowAccountRegistration_ = std::move(allowAccountRegistration);
        modified_settings_.emplace(ServerSetting::Setting::ALLOW_ACCOUNT_REGISTRATION);
    }
    CaptchaWrapper getCaptcha() const {
        return captcha_;
    }
    void setCaptcha(CaptchaWrapper&& captcha) {
        captcha_ = std::forward<CaptchaWrapper>(captcha);
        modified_settings_.emplace(ServerSetting::Setting::CAPTCHA);
    }
    bool getCaptchaRequired() const {
        return captchaRequired_;
    }
    void setCaptchaRequired(bool enabled) {
        captchaRequired_ = enabled;
        modified_settings_.emplace(ServerSetting::Setting::CAPTCHA_REQUIRED);
    }
    bool getUserVmsEnabled() const {
        return userVmsEnabled_;
    }
    void setUserVmsEnabled(bool&& userVmsEnabled) {
        userVmsEnabled_ = std::move(userVmsEnabled);
        modified_settings_.emplace(ServerSetting::Setting::USER_VMS_ENABLED);
    }
    bool getAllowUserVmRequests() const {
        return allowUserVmRequests_;
    }
    void setAllowUserVmRequests(bool&& allowUserVmRequests) {
        allowUserVmRequests_ = std::move(allowUserVmRequests);
        modified_settings_.emplace(ServerSetting::Setting::ALLOW_USER_VM_REQUESTS);
    }
    std::string getBanIpCommand() const {
        return banIpCommand_;
    }
    void setBanIpCommand(std::string&& banIpCommand) {
        banIpCommand_ = std::move(banIpCommand);
        modified_settings_.emplace(ServerSetting::Setting::BAN_IP_COMMAND);
    }
    std::string getUnbanIpCommand() const {
        return unbanIpCommand_;
    }
    void setUnbanIpCommand(std::string&& unbanIpCommand) {
        unbanIpCommand_ = std::move(unbanIpCommand);
        modified_settings_.emplace(ServerSetting::Setting::UNBAN_IP_COMMAND);
    }
    bool getMaxConnectionsEnabled() const {
        return maxConnectionsEnabled_;
    }
    void setMaxConnectionsEnabled(bool maxConnectionsEnabled) {
        maxConnectionsEnabled_ = std::move(maxConnectionsEnabled);
        modified_settings_.emplace(ServerSetting::Setting::MAX_CONNECTIONS_ENABLED);
    }
    std::uint8_t getMaxConnections() const {
        return maxConnections_;
    }
    void setMaxConnections(std::uint8_t&& maxConnections) {
        maxConnections_ = std::move(maxConnections);
        modified_settings_.emplace(ServerSetting::Setting::MAX_CONNECTIONS);
    }
    private:
        bool allowAccountRegistration_;
        bool captchaRequired_;
        CaptchaWrapper captcha_;
        bool recaptchaEnabled_;
        std::string recaptchaKey_;
        bool userVmsEnabled_;
        bool allowUserVmRequests_;
        std::string banIpCommand_;
        std::string unbanIpCommand_;
        bool maxConnectionsEnabled_;
        std::uint8_t maxConnections_;
        std::set<int> modified_settings_;
};

struct VmSettingsWrapper {
    VmSettingsWrapper(){}
    VmSettingsWrapper(capnp::List<VmSetting>::Reader settings) {
        for (auto&& server_setting : settings) {
            const auto setting = server_setting.getSetting();
            switch (setting.which()) {
            case VmSetting::Setting::AUTO_START:
                auto_start_ = setting.getAutoStart();
                break;
            case VmSetting::Setting::NAME:
                name_ = setting.getName();
                break;
            case VmSetting::Setting::DESCRIPTION:
                description_ = setting.getDescription();
                break;
            case VmSetting::Setting::HOST:
                host_ = setting.getHost();
                break;
            case VmSetting::Setting::OPERATING_SYSTEM:
                operatingSystem_ = setting.getOperatingSystem();
                break;
            case VmSetting::Setting::RAM:
                ram_ = setting.getRam();
                break;
            case VmSetting::Setting::DISK_SPACE:
                disk_ = setting.getDiskSpace();
                break;
            case VmSetting::Setting::START_COMMAND:
                startCommand_ = setting.getStartCommand();
                break;
            case VmSetting::Setting::STOP_COMMAND:
                stopCommand_ = setting.getStopCommand();
                break;
            case VmSetting::Setting::RESTART_COMMAND:
                restartCommand_ = setting.getRestartCommand();
                break;
            case VmSetting::Setting::SNAPSHOT_COMMANDS:
                break;
            case VmSetting::Setting::TURNS_ENABLED:
                turnsEnabled_ = setting.getTurnsEnabled();
                break;
            case VmSetting::Setting::TURN_TIME:
                turnTime_ = setting.getTurnTime();
                break;
            case VmSetting::Setting::UPLOADS_ENABLED:
                uploadsEnabled_ = setting.getUploadsEnabled();
                break;
            case VmSetting::Setting::UPLOAD_COOLDOWN_TIME:
                uploadCooldownTime_ = setting.getUploadCooldownTime();
                break;
            case VmSetting::Setting::MAX_UPLOAD_SIZE:
                maxUploadSize_ = setting.getMaxUploadSize();
                break;
            case VmSetting::Setting::VOTES_ENABLED:
                votesEnabled_ = setting.getVotesEnabled();
                break;
            case VmSetting::Setting::VOTE_TIME:
                voteTime_ = setting.getVoteTime();
                break;
            case VmSetting::Setting::VOTE_COOLDOWN_TIME:
                voteCooldownTime_ = setting.getVoteCooldownTime();
                break;
            case VmSetting::Setting::PROTOCOL:
                protocol_ = static_cast<std::uint16_t>(setting.getProtocol());
                break;
            case VmSetting::Setting::AGENT_ADDRESS:
                address_ = setting.getAgentAddress();
                break;
            case VmSetting::Setting::GUACAMOLE_PARAMETERS:
                for (auto guac_param : setting.getGuacamoleParameters()) {
                  guacamole_parameters_.emplace_back(guac_param.getName(), guac_param.getValue());
                }
                break;
            case VmSetting::Setting::SAFE_FOR_WORK:
                safe_for_work_ = setting.getSafeForWork();
                break;
            case VmSetting::Setting::DISALLOW_GUESTS:
                disallow_guests_ = setting.getDisallowGuests();
                break;
            }
        }
    }

    std::size_t size() const {
    	return modified_settings_.size();
    }

    void getCreateVmRequest(capnp::List<VmSetting>::Builder settings) {
                auto i = 0;
                for (const auto setting_type : modified_settings_) {
                        auto setting = settings[i].initSetting();
                        switch (static_cast<const VmSetting::Setting::Which>(setting_type)) {
            case VmSetting::Setting::AUTO_START:
                                setting.setAutoStart(auto_start_);
                break;
            case VmSetting::Setting::NAME:
                                setting.setName(name_);
                break;
            case VmSetting::Setting::DESCRIPTION:
                                setting.setDescription(description_);
                break;
            case VmSetting::Setting::HOST:
                                setting.setHost(host_);
                break;
            case VmSetting::Setting::OPERATING_SYSTEM:
                                setting.setOperatingSystem(operatingSystem_);
                break;
            case VmSetting::Setting::RAM:
                                setting.setRam(ram_);
                break;
            case VmSetting::Setting::DISK_SPACE:
                                setting.setDiskSpace(disk_);
                break;
            case VmSetting::Setting::START_COMMAND:
                                setting.setStartCommand(startCommand_);
                break;
            case VmSetting::Setting::STOP_COMMAND:
                                setting.setStopCommand(stopCommand_);
                break;
            case VmSetting::Setting::RESTART_COMMAND:
                                setting.setRestartCommand(restartCommand_);
                break;
            case VmSetting::Setting::SNAPSHOT_COMMANDS:
                break;
            case VmSetting::Setting::TURNS_ENABLED:
                                setting.setTurnsEnabled(turnsEnabled_);
                break;
            case VmSetting::Setting::TURN_TIME:
                                setting.setTurnTime(turnTime_);
                break;
            case VmSetting::Setting::UPLOADS_ENABLED:
                                setting.setUploadsEnabled(uploadsEnabled_);
                break;
            case VmSetting::Setting::UPLOAD_COOLDOWN_TIME:
                                setting.setUploadCooldownTime(uploadCooldownTime_);
                break;
            case VmSetting::Setting::MAX_UPLOAD_SIZE:
                                setting.setMaxUploadSize(maxUploadSize_);
                break;
            case VmSetting::Setting::VOTES_ENABLED:
                                setting.setVotesEnabled(votesEnabled_);
                break;
            case VmSetting::Setting::VOTE_TIME:
                                setting.setVoteTime(voteTime_);
                break;
            case VmSetting::Setting::VOTE_COOLDOWN_TIME:
                                setting.setVoteCooldownTime(voteCooldownTime_);
                break;
            case VmSetting::Setting::PROTOCOL:
                setting.setProtocol(VmSetting::Protocol(protocol_));
                break;
            case VmSetting::Setting::AGENT_ADDRESS:
                                setting.setAgentAddress(address_);
                break;
            case VmSetting::Setting::GUACAMOLE_PARAMETERS:
            {
                  auto guac_params = setting.initGuacamoleParameters(guacamole_parameters_.size());
                  for (auto i = 0u; i < guacamole_parameters_.size(); i++) {
                    guac_params[i].setName(guacamole_parameters_[i].getName());
                    guac_params[i].setValue(guacamole_parameters_[i].getValue());
                  }
            }
            break;
            case VmSetting::Setting::SAFE_FOR_WORK:
                setting.setSafeForWork(safe_for_work_);
                break;
            case VmSetting::Setting::DISALLOW_GUESTS:
                setting.setDisallowGuests(disallow_guests_);
                break;
            default:
              std::cerr << "unknown setting" << std::endl;
              assert(false);
                      }
                        i++;
                }
                modified_settings_.clear();
        }

    bool getAutoStart() const {
        return auto_start_;
    }
    void setAutoStart(bool&& auto_start) {
        auto_start_ = auto_start;
        modified_settings_.emplace(VmSetting::Setting::AUTO_START);
    }
    std::string getName() const {
        return name_;
    }
    void setName(std::string&& name) {
        name_ = std::move(name);
        modified_settings_.emplace(VmSetting::Setting::NAME);
    }
    std::string getDescription() const {
        return description_;
    }
    void setDescription(std::string&& description) {
        description_ = std::move(description);
        modified_settings_.emplace(VmSetting::Setting::DESCRIPTION);
    }
    std::string getHost() const {
        return host_;
    }
    void setHost(std::string&& host) {
        host_ = std::move(host);
        modified_settings_.emplace(VmSetting::Setting::HOST);
    }
    std::string getOperatingSystem() const {
        return operatingSystem_;
    }
    void setOperatingSystem(std::string&& operatingSystem) {
        operatingSystem_ = std::move(operatingSystem);
        modified_settings_.emplace(VmSetting::Setting::OPERATING_SYSTEM);
    }
    std::uint8_t getRam() const {
        return ram_;
    }
    void setRam(std::uint8_t&& ram) {
        ram_ = std::move(ram);
        modified_settings_.emplace(VmSetting::Setting::RAM);
    }
    std::uint8_t getDiskSpace() const {
        return disk_;
    }
    void setDiskSpace(std::uint8_t&& disk) {
        disk_ = std::move(disk);
        modified_settings_.emplace(VmSetting::Setting::DISK_SPACE);
    }
    std::string getStartCommand() const {
        return startCommand_;
    }
    void setStartCommand(std::string&& startCommand) {
        startCommand_ = std::move(startCommand);
        modified_settings_.emplace(VmSetting::Setting::START_COMMAND);
    }
    std::string getStopCommand() const {
        return stopCommand_;
    }
    void setStopCommand(std::string&& stopCommand) {
        stopCommand_ = std::move(stopCommand);
        modified_settings_.emplace(VmSetting::Setting::STOP_COMMAND);
    }
    std::string getRestartCommand() const {
        return restartCommand_;
    }
    void setRestartCommand(std::string&& restartCommand) {
        restartCommand_ = std::move(restartCommand);
        modified_settings_.emplace(VmSetting::Setting::RESTART_COMMAND);
    }
    bool getTurnsEnabled() const {
        return turnsEnabled_;
    }
    void setTurnsEnabled(bool&& turnsEnabled) {
        turnsEnabled_ = std::move(turnsEnabled);
        modified_settings_.emplace(VmSetting::Setting::TURNS_ENABLED);
    }
    std::uint16_t getTurnTime() const {
        return turnTime_;
    }
    void setTurnTime(std::uint16_t&& turnTime) {
        turnTime_ = std::move(turnTime);
        modified_settings_.emplace(VmSetting::Setting::TURN_TIME);
    }
    bool getUploadsEnabled() const {
        return uploadsEnabled_;
    }
    void setUploadsEnabled(bool&& uploadsEnabled) {
        uploadsEnabled_ = std::move(uploadsEnabled);
        modified_settings_.emplace(VmSetting::Setting::UPLOADS_ENABLED);
    }
    std::uint16_t getUploadCooldownTime() const {
        return uploadCooldownTime_;
    }
    void setUploadCooldownTime(std::uint16_t&& uploadCooldownTime) {
        uploadCooldownTime_ = std::move(uploadCooldownTime);
        modified_settings_.emplace(VmSetting::Setting::UPLOAD_COOLDOWN_TIME);
    }
    std::uint32_t getMaxUploadSize() const {
        return maxUploadSize_;
    }
    void setMaxUploadSize(std::uint32_t&& maxUploadSize) {
        maxUploadSize_ = std::move(maxUploadSize);
        modified_settings_.emplace(VmSetting::Setting::MAX_UPLOAD_SIZE);
    }
    bool getVotesEnabled() const {
        return votesEnabled_;
    }
    void setVotesEnabled(bool&& votesEnabled) {
        votesEnabled_ = std::move(votesEnabled);
        modified_settings_.emplace(VmSetting::Setting::VOTES_ENABLED);
    }
    std::uint16_t getVoteTime() const {
        return voteTime_;
    }
    void setVoteTime(std::uint16_t&& voteTime) {
        voteTime_ = std::move(voteTime);
        modified_settings_.emplace(VmSetting::Setting::VOTE_TIME);
    }
    std::uint16_t getVoteCooldownTime() const {
        return voteCooldownTime_;
    }
    void setVoteCooldownTime(std::uint16_t&& voteCooldownTime) {
        voteCooldownTime_ = std::move(voteCooldownTime);
        modified_settings_.emplace(VmSetting::Setting::VOTE_COOLDOWN_TIME);
    }
    auto getProtocol() const {
        return protocol_;
    }
    void setProtocol(std::uint16_t protocol) {
        protocol_ = protocol;
        modified_settings_.emplace(VmSetting::Setting::PROTOCOL);
    }
    std::string getAgentAddress() const {
        return address_;
    }
    void setAgentAddress(std::string&& address) {
        address_ = std::move(address);
        modified_settings_.emplace(VmSetting::Setting::AGENT_ADDRESS);
    }

  std::vector<GuacamoleParameterWrapper> getGuacamoleParameters() const { return guacamole_parameters_; }
  void setGuacamoleParameters(std::vector<GuacamoleParameterWrapper>&& guacParams) {
    modified_settings_.emplace(VmSetting::Setting::GUACAMOLE_PARAMETERS);
    guacamole_parameters_ = guacParams;
  }
    bool getSafeForWork() const {
        return safe_for_work_;
    }
    void setSafeForWork(bool safe_for_work) {
        safe_for_work_ = safe_for_work;
        modified_settings_.emplace(VmSetting::Setting::SAFE_FOR_WORK);
    }
    bool getDisallowGuests() const {
        return disallow_guests_;
    }
    void setDisallowGuests(bool disallow_guests) {
        disallow_guests_ = disallow_guests;
        modified_settings_.emplace(VmSetting::Setting::DISALLOW_GUESTS);
    }
    private:
        bool auto_start_;
        std::string name_;
        std::string description_;
        std::string host_;
        std::string operatingSystem_;
        std::uint8_t ram_;
        std::uint8_t disk_;
        std::string startCommand_;
        std::string stopCommand_;
        std::string restartCommand_;
        bool turnsEnabled_;
        std::uint16_t turnTime_;
        bool uploadsEnabled_;
        std::uint16_t uploadCooldownTime_;
        std::uint32_t maxUploadSize_;
        bool votesEnabled_;
        std::uint16_t voteTime_;
        std::uint16_t voteCooldownTime_;
        std::uint16_t protocol_;
        std::string address_;
        std::uint16_t socket_type_;
        std::set<int> modified_settings_;
        std::vector<GuacamoleParameterWrapper> guacamole_parameters_;
        bool safe_for_work_;
        bool disallow_guests_;
};

struct Deserializer {
  template<typename TWrapper, typename TList, typename TTransformer=std::nullptr_t>
  static auto to_vector(TList list, TTransformer transform={}) {
    auto vector = std::vector<TWrapper>();
    vector.reserve(list.size());
    if constexpr (std::is_same_v<TTransformer, std::nullptr_t>) {
      for (auto vmInfo : list) {
        vector.emplace_back(vmInfo);
      }
    } else {
      std::transform(list.begin(), list.end(), std::back_inserter(vector), transform);
    }
    return vector;
  }

  static std::vector<std::uint8_t> convertIpAddressToBytes(IpAddress::Reader ip_address) {
    auto bytes = std::vector<std::uint8_t>();
    bytes.reserve(16);
    for (auto i = 0u; i < 8; i++) {
      bytes.push_back(ip_address.getFirst() >> ((7 - i) * 8));
    }
    for (auto i = 0u; i < 8; i++) {
      bytes.push_back(ip_address.getSecond() >> ((7 - i) * 8));
    }
    return bytes;
  }

	void deserialize(const std::string& message_bytes) {
		auto word_array = kj::ArrayPtr<const capnp::word>(reinterpret_cast<const capnp::word*>(message_bytes.c_str()), message_bytes.size()/sizeof(capnp::word));
    auto message_count = 0u;
    while (word_array.size()) {
      auto reader = capnp::FlatArrayMessageReader(word_array);
      const auto message = reader.getRoot<CollabVmServerMessage>().getMessage();
      switch (message.which())
      {
        case CollabVmServerMessage::Message::VM_LIST_RESPONSE:
        {
          const auto vmInfoList = message.getVmListResponse();
          onVmInfo(to_vector<VmInfoWrapper>(vmInfoList));
          break;
        }
        case CollabVmServerMessage::Message::VM_TURN_INFO:
        {
          const auto vm_turn_info = message.getVmTurnInfo();
          onVmTurnInfo(to_vector<std::string>(vm_turn_info.getUsers()), vm_turn_info.getTimeRemaining(), vm_turn_info.getState() == CollabVmServerMessage::TurnState::PAUSED);
          break;
        }
        case CollabVmServerMessage::Message::VM_THUMBNAIL:
        {
          const auto thumbnail = message.getVmThumbnail();
          const auto png_bytes = thumbnail.getPngBytes();
          onVmThumbnail(
              thumbnail.getId(),
              emscripten::val(emscripten::typed_memory_view(
                png_bytes.size(),
                png_bytes.begin())
                ));
          break;
        }
        case CollabVmServerMessage::Message::ACCOUNT_REGISTRATION_RESPONSE:
        {
          const auto resp = message.getAccountRegistrationResponse().getResult();
          if (resp.hasSession()) {
            const auto session = resp.getSession();
            const auto session_id = session.getSessionId();
            const auto username = session.getUsername();
            //onRegisterAccountSucceeded(emscripten::val(emscripten::typed_memory_view(session_id.size(), session_id.begin())));
            onRegisterAccountSucceeded(std::string(reinterpret_cast<const char*>(session_id.begin()), session_id.size()), username);
          } else if (resp.isErrorStatus()) {
            std::string error_message;
            switch (resp.getErrorStatus()) {
              case CollabVmServerMessage::RegisterAccountResponse::RegisterAccountError::USERNAME_TAKEN:
              error_message = "That username is taken";
              break;
              case CollabVmServerMessage::RegisterAccountResponse::RegisterAccountError::USERNAME_INVALID:
              error_message = "There was a problem with your username";
              break;
              case CollabVmServerMessage::RegisterAccountResponse::RegisterAccountError::PASSWORD_INVALID:
              error_message = "There was a problem with your password";
              break;
              case CollabVmServerMessage::RegisterAccountResponse::RegisterAccountError::TOTP_ERROR:
              error_message = "There was a problem with 2FA";
              break;
            }
            onRegisterAccountFailed(error_message);
          }
          break;
        }
        case CollabVmServerMessage::Message::LOGIN_RESPONSE:
        {
          const auto response = message.getLoginResponse().getResult();
          if (response.hasSession()) {
            const auto session = response.getSession();
            const auto session_id = session.getSessionId();
            const auto username = session.getUsername();
            const auto is_admin = session.getIsAdmin();
            onLoginSucceeded(std::string(reinterpret_cast<const char*>(session_id.begin()), session_id.size()), username, is_admin);
          } else switch (response.getResult()) {
            case CollabVmServerMessage::LoginResponse::LoginResult::INVALID_PASSWORD:
            onLoginFailed("invalid password");
            break;
            case CollabVmServerMessage::LoginResponse::LoginResult::INVALID_USERNAME:
            onLoginFailed("invalid username");
            break;
            case CollabVmServerMessage::LoginResponse::LoginResult::INVALID_CAPTCHA_TOKEN:
            onLoginFailed("captcha verification failed");
            break;
          }
        break;
        }
        case CollabVmServerMessage::Message::SERVER_SETTINGS:
          onServerConfig(ServerSettingsWrapper(message.getServerSettings()));
          break;
        case CollabVmServerMessage::Message::READ_VMS_RESPONSE:
        {
          onAdminVms(to_vector<AdminVmInfoWrapper>(message.getReadVmsResponse()));
          break;
        }
        case CollabVmServerMessage::Message::READ_VM_CONFIG_RESPONSE:
          onVmConfig(VmSettingsWrapper(message.getReadVmConfigResponse()));
          break;
        case CollabVmServerMessage::Message::GUAC_INSTR:
        {
          callGuacInstrHandler(message.getGuacInstr());
          break;
        }
        case CollabVmServerMessage::Message::CREATE_VM_RESPONSE:
          onVmCreated(message.getCreateVmResponse());
          break;
        case CollabVmServerMessage::Message::CONNECT_RESPONSE:
        {
          const auto result = message.getConnectResponse().getResult();
          if (result.which() == CollabVmServerMessage::ChannelConnectResponse::Result::SUCCESS) {
            auto success = result.getSuccess();
            onConnect(success.getUsername(), success.getCaptchaRequired());
            for (auto chat_message :
                 success.getChatMessages()) {
              onChatMessage(0, chat_message.getSender(), static_cast<std::uint8_t>(chat_message.getUserType()), chat_message.getMessage(), chat_message.getTimestamp());
            }
          } else if (result.which() == CollabVmServerMessage::ChannelConnectResponse::Result::FAIL) {
            onConnectFail();
          }
          break;
        }
        case CollabVmServerMessage::Message::CHAT_MESSAGE:
        {
          auto chat_message = message.getChatMessage();
          onChatMessage(chat_message.getChannel(),
                        chat_message.getMessage().getSender(),
                        static_cast<std::uint8_t>(chat_message.getMessage().getUserType()),
                        chat_message.getMessage().getMessage(),
                        chat_message.getMessage().getTimestamp());
          break;
        }
        case CollabVmServerMessage::Message::CHAT_MESSAGES:
        {
          auto chat_messages = message.getChatMessages();
          const auto channel_id = chat_messages.getChannel();
          const auto first_message_index = chat_messages.getFirstMessage();
          if (first_message_index == 0) {
            for (auto chat_message : chat_messages.getMessages()) {
              if (!chat_message.getMessage().size()) {
                break;
              }
              onChatMessage(channel_id, chat_message.getSender(), static_cast<std::uint8_t>(chat_message.getUserType()), chat_message.getMessage(), chat_message.getTimestamp());
            }
          } else {
            auto i = first_message_index;
            do {
              onChatMessage(channel_id,
                            chat_messages.getMessages()[i].getSender(),
                            static_cast<std::uint8_t>(chat_messages.getMessages()[i].getUserType()),
                            chat_messages.getMessages()[i].getMessage(),
                            chat_messages.getMessages()[i].getTimestamp());
              i = (i + 1) % chat_messages.getMessages().size();
            } while (i != first_message_index);
          }
          break;
        }
        case CollabVmServerMessage::Message::USER_LIST_ADD:
        {
          const auto added_user = message.getUserListAdd();
          onUserListAdd(added_user.getChannel(), added_user.getUsername(), static_cast<std::uint8_t>(added_user.getUserType()));
          break;
        }
        case CollabVmServerMessage::Message::USER_LIST_REMOVE:
        {
          const auto removed_user = message.getUserListRemove();
          onUserListRemove(removed_user.getChannel(), removed_user.getUsername());
          break;
        }
        case CollabVmServerMessage::Message::ADMIN_USER_LIST_ADD:
        {
          const auto update = message.getAdminUserListAdd();
          const auto added_user = update.getUser();
          onAdminUserListAdd(update.getChannel(), added_user.getUsername(), static_cast<std::uint8_t>(added_user.getUserType()), convertIpAddressToBytes(added_user.getIpAddress()));
          break;
        }
        case CollabVmServerMessage::Message::USER_LIST:
        {
          const auto user_list = message.getUserList();
          auto usernames = to_vector<std::string>(user_list.getUsers(), [](auto user){ return user.getUsername(); });
          auto user_types = to_vector<std::uint8_t>(user_list.getUsers(), [](auto user){ return static_cast<std::uint8_t>(user.getUserType()); });
          onUserList(user_list.getChannel(), std::move(usernames), std::move(user_types));
        }
        break;
        case CollabVmServerMessage::Message::ADMIN_USER_LIST:
        {
          const auto user_list = message.getAdminUserList();
          auto usernames = to_vector<std::string>(user_list.getUsers(), [](auto user){ return user.getUsername(); });
          auto user_types = to_vector<std::uint8_t>(user_list.getUsers(), [](auto user){ return static_cast<std::uint8_t>(user.getUserType()); });
          auto ip_addresses = to_vector<std::vector<std::uint8_t>>(
            user_list.getUsers(),
            [](auto user){
              return convertIpAddressToBytes(user.getIpAddress());
            });
          onAdminUserList(user_list.getChannel(), std::move(usernames),std::move(user_types), std::move(ip_addresses));
        }
        break;
        case CollabVmServerMessage::Message::USERNAME_TAKEN:
        {
          onUsernameTaken();
        }
        break;
        case CollabVmServerMessage::Message::CHANGE_USERNAME:
        {
          const auto username_change = message.getChangeUsername();
          onUsernameChange(username_change.getOldUsername(), username_change.getNewUsername());
        }
        break;
        case CollabVmServerMessage::Message::VM_DESCRIPTION:
          onVmDescription(message.getVmDescription());
          break;
        case CollabVmServerMessage::Message::CREATE_INVITE_RESULT:
        {
          const auto id = message.getCreateInviteResult();
          onCreateInviteResult({id.begin(), id.end()});
          break;
        }
        case CollabVmServerMessage::Message::INVITE_VALIDATION_RESPONSE:
        {
          const auto invite = message.getInviteValidationResponse();
          onInviteValidationResponse(invite.getIsValid(), invite.getUsername());
          break;
        }
        case CollabVmServerMessage::Message::CAPTCHA_REQUIRED:
        {
          const auto required = message.getCaptchaRequired();
          onCaptchaRequired(required);
          break;
        }
        case CollabVmServerMessage::Message::VOTE_STATUS:
        {
          const auto vote_status = message.getVoteStatus().getStatus();
          switch (vote_status.which()) {
            case VoteStatus::Status::DISABLED:
              onVoteDisabled();
            break;
            case VoteStatus::Status::IDLE:
              onVoteIdle();
            break;
            case VoteStatus::Status::COOLING_DOWN:
              onVoteCoolingDown();
            break;
            case VoteStatus::Status::IN_PROGRESS:
              const auto vote_info = vote_status.getInProgress();
              onVoteStatus(vote_info.getTimeRemaining(),
                           vote_info.getYesVoteCount(),
                           vote_info.getNoVoteCount());
            break;
          }
          break;
        }
        case CollabVmServerMessage::Message::VOTE_RESULT:
        {
          const auto vote_passed = message.getVoteResult();
          onVoteResult(vote_passed);
          break;
        }
      }
      word_array = kj::ArrayPtr(reader.getEnd(), word_array.end());
    }
	}

	void deserializeGuacInstr(const std::string& buffer) {
		kj::ArrayPtr<const capnp::word> array(reinterpret_cast<const capnp::word*>(buffer.c_str()), buffer.size()/sizeof(capnp::word));
		capnp::FlatArrayMessageReader reader(array);
		auto message = reader.getRoot<Guacamole::GuacServerInstruction>();
		callGuacInstrHandler(message);
	}

	void callGuacInstrHandler(Guacamole::GuacServerInstruction::Reader instr) {
		switch (instr.which()) {
			case Guacamole::GuacServerInstruction::Which::ARC:
			{
				auto val = instr.getArc();
				onGuacInstr("arc", emscripten::val(ArcWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::CFILL:
			{
				auto val = instr.getCfill();
				onGuacInstr("cfill", emscripten::val(CfillWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::CLIP:
			{
				auto val = instr.getClip();
				onGuacInstr("clip", emscripten::val(val));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::CLOSE:
			{
				auto val = instr.getClose();
				onGuacInstr("close", emscripten::val(val));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::COPY:
			{
				auto val = instr.getCopy();
				onGuacInstr("copy", emscripten::val(CopyWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::CSTROKE:
			{
				auto val = instr.getCstroke();
				onGuacInstr("cstroke", emscripten::val(CstrokeWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::CURSOR:
			{
				auto val = instr.getCursor();
				onGuacInstr("cursor", emscripten::val(CursorWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::CURVE:
			{
				auto val = instr.getCurve();
				onGuacInstr("curve", emscripten::val(CurveWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::DISPOSE:
			{
				auto val = instr.getDispose();
				onGuacInstr("dispose", emscripten::val(val));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::DISTORT:
			{
				auto val = instr.getDistort();
				onGuacInstr("distort", emscripten::val(DistortWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::IDENTITY:
			{
				auto val = instr.getIdentity();
				onGuacInstr("identity", emscripten::val(val));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::LFILL:
			{
				auto val = instr.getLfill();
				onGuacInstr("lfill", emscripten::val(LfillWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::LINE:
			{
				auto val = instr.getLine();
				onGuacInstr("line", emscripten::val(LineWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::LSTROKE:
			{
				auto val = instr.getLstroke();
				onGuacInstr("lstroke", emscripten::val(LstrokeWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::MOVE:
			{
				auto val = instr.getMove();
				onGuacInstr("move", emscripten::val(MoveWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::POP:
			{
				auto val = instr.getPop();
				onGuacInstr("pop", emscripten::val(val));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::PUSH:
			{
				auto val = instr.getPush();
				onGuacInstr("push", emscripten::val(val));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::RECT:
			{
				auto val = instr.getRect();
				onGuacInstr("rect", emscripten::val(RectWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::RESET:
			{
				auto val = instr.getReset();
				onGuacInstr("reset", emscripten::val(val));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::SET:
			{
				auto val = instr.getSet();
				onGuacInstr("set", emscripten::val(SetWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::SHADE:
			{
				auto val = instr.getShade();
				onGuacInstr("shade", emscripten::val(ShadeWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::SIZE:
			{
				auto val = instr.getSize();
				onGuacInstr("size", emscripten::val(LayerSizeWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::START:
			{
				auto val = instr.getStart();
				onGuacInstr("start", emscripten::val(StartWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::TRANSFER:
			{
				auto val = instr.getTransfer();
				onGuacInstr("transfer", emscripten::val(TransferWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::TRANSFORM:
			{
				auto val = instr.getTransform();
				onGuacInstr("transform", emscripten::val(TransformWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::ACK:
			{
				auto val = instr.getAck();
				onGuacInstr("ack", emscripten::val(AckWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::AUDIO:
			{
				auto val = instr.getAudio();
				onGuacInstr("audio", emscripten::val(AudioWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::BLOB:
			{
				auto val = instr.getBlob();
				onGuacInstr("blob", emscripten::val(BlobWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::CLIPBOARD:
			{
				auto val = instr.getClipboard();
				onGuacInstr("clipboard", emscripten::val(ClipboardWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::END:
			{
				auto val = instr.getEnd();
				onGuacInstr("end", emscripten::val(val));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::FILE:
			{
				auto val = instr.getFile();
				onGuacInstr("file", emscripten::val(FileWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::IMG:
			{
				auto val = instr.getImg();
				onGuacInstr("img", emscripten::val(ImgWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::NEST:
			{
				auto val = instr.getNest();
				onGuacInstr("nest", emscripten::val(NestWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::PIPE:
			{
				auto val = instr.getPipe();
				onGuacInstr("pipe", emscripten::val(PipeWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::VIDEO:
			{
				auto val = instr.getVideo();
				onGuacInstr("video", emscripten::val(VideoWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::BODY:
			{
				auto val = instr.getBody();
				onGuacInstr("body", emscripten::val(BodyWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::FILESYSTEM:
			{
				auto val = instr.getFilesystem();
				onGuacInstr("filesystem", emscripten::val(FilesystemWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::UNDEFINE:
			{
				auto val = instr.getUndefine();
				onGuacInstr("undefine", emscripten::val(val));
				return;
			}
			/*
			case Guacamole::GuacServerInstruction::Which::ARGS:
			{
				auto val = instr.getArgs();
				onGuacInstr("args", emscripten::val(List(Text)Wrapper(&val)));
				return;
			}
			*/
			case Guacamole::GuacServerInstruction::Which::DISCONNECT:
			{
				auto val = instr.getDisconnect();
				onGuacInstr("disconnect", emscripten::val::null());
				return;
			}
			case Guacamole::GuacServerInstruction::Which::ERROR:
			{
				auto val = instr.getError();
				onGuacInstr("error", emscripten::val(ErrorWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::LOG:
			{
				auto val = instr.getLog();
				onGuacInstr("log", emscripten::val(std::string(val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::MOUSE:
			{
				auto val = instr.getMouse();
				onGuacInstr("mouse", emscripten::val(ServerMouseWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::KEY:
			{
				auto val = instr.getKey();
				onGuacInstr("key", emscripten::val(ServerKeyWrapper(&val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::NOP:
			{
				auto val = instr.getNop();
				onGuacInstr("nop", emscripten::val::null());
				return;
			}
			case Guacamole::GuacServerInstruction::Which::READY:
			{
				auto val = instr.getReady();
				onGuacInstr("ready", emscripten::val(std::string(val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::SYNC:
			{
				auto val = instr.getSync();
				onGuacInstr("sync", emscripten::val(static_cast<double>(val)));
				return;
			}
			case Guacamole::GuacServerInstruction::Which::NAME:
			{
				auto val = instr.getName();
				onGuacInstr("name", emscripten::val(std::string(val)));
				return;
			}
		}
	}

	void test() {
		capnp::MallocMessageBuilder message_builder;
		auto instr = message_builder.initRoot<Guacamole::GuacServerInstruction>();
    auto arc = instr.initMouse();
    auto arc_reader = arc.asReader();
		onGuacInstr("arc", emscripten::val(ServerMouseWrapper(&arc_reader)));
	}

	virtual void onVmInfo(std::vector<VmInfoWrapper>&& vmInfo) = 0;
	virtual void onVmTurnInfo(std::vector<std::string>&& users, std::uint32_t time_remaining, bool is_paused) = 0;
	virtual void onVmThumbnail(std::uint32_t vm_id, emscripten::val thumbnail) = 0;
	virtual void onRegisterAccountSucceeded(std::string&& session_id, std::string&& username) = 0;
	virtual void onRegisterAccountFailed(const std::string error_message) = 0;
	virtual void onLoginSucceeded(std::string&& session_id, std::string&& username, bool is_admin) = 0;
	virtual void onLoginFailed(const std::string error_message) = 0;
	virtual void onServerConfig(ServerSettingsWrapper&& config) = 0;
	virtual void onVmCreated(unsigned int vm_id) = 0;
	virtual void onVmConfig(VmSettingsWrapper&& config) = 0;
	virtual void onAdminVms(const std::vector<AdminVmInfoWrapper>& vms) = 0;
	virtual void onGuacInstr(std::string&& instr_name, const emscripten::val& instr) = 0;
  virtual void onChatMessage(std::uint32_t channel_id, std::string&& username, std::uint8_t user_type, std::string&& message, double timestamp) = 0;
  virtual void onConnect(std::string&& username, bool captcha_required) = 0;
  virtual void onConnectFail() = 0;
  virtual void onUserList(std::uint32_t channel_id, std::vector<std::string>&& usernames, std::vector<std::uint8_t>&& user_types) = 0;
  virtual void onUserListAdd(std::uint32_t channel_id, std::string&& username, std::uint8_t user_type) = 0;
  virtual void onUserListRemove(std::uint32_t channel_id, std::string&& username) = 0;
  virtual void onAdminUserList(std::uint32_t channel_id, std::vector<std::string>&& usernames, std::vector<std::uint8_t>&& user_types, std::vector<std::vector<std::uint8_t>>&& ip_addresses) = 0;
  virtual void onAdminUserListAdd(std::uint32_t channel_id, std::string&& username, std::uint8_t user_type, std::vector<std::uint8_t> ip_address) = 0;
  virtual void onUsernameTaken() = 0;
  virtual void onUsernameChange(std::string&& oldUsername, std::string&& newUsername) = 0;
  virtual void onVmDescription(std::string&& username) = 0;
  virtual void onCreateInviteResult(std::vector<std::uint8_t>&& id) = 0;
  virtual void onInviteValidationResponse(bool is_valid, std::string&& username) = 0;
	virtual void onCaptchaRequired(bool required) = 0;
	virtual void onVoteDisabled() = 0;
	virtual void onVoteIdle() = 0;
	virtual void onVoteCoolingDown() = 0;
	virtual void onVoteStatus(std::uint32_t time_remaining, std::uint32_t yes_vote_count, std::uint32_t no_vote_count) = 0;
	virtual void onVoteResult(bool vote_passed) = 0;

  virtual ~Deserializer() = default;
};

struct DeserializerWrapper : public emscripten::wrapper<Deserializer> {
	EMSCRIPTEN_WRAPPER(DeserializerWrapper);
	void onVmInfo(std::vector<VmInfoWrapper>&& vmInfo) {
		return call<void>("onVmInfo", std::move(vmInfo));
	}
	void onVmTurnInfo(std::vector<std::string>&& users, std::uint32_t time_remaining, bool is_paused) {
		return call<void>("onVmTurnInfo", std::move(users), time_remaining, is_paused);
  }

	void onVmThumbnail(std::uint32_t vm_id, emscripten::val thumbnail) {
		return call<void>("onVmThumbnail", vm_id, thumbnail);
	}

	void onRegisterAccountSucceeded(std::string&& session_id, std::string&& username) {
		return call<void>("onRegisterAccountSucceeded", std::move(session_id), std::move(username));
	}

	void onRegisterAccountFailed(const std::string error_message) {
		return call<void>("onRegisterAccountFailed", error_message);
	}

	void onLoginSucceeded(std::string&& session_id, std::string&& username, bool is_admin) {
		return call<void>("onLoginSucceeded", std::move(session_id), std::move(username), is_admin);
	}

	void onLoginFailed(const std::string error_message) {
		return call<void>("onLoginFailed", error_message);
	}

	void onServerConfig(ServerSettingsWrapper&& config) {
		return call<void>("onServerConfig", std::move(config));
	}

	void onVmCreated(unsigned int vm_id) {
    return call<void>("onVmCreated", vm_id);
  }

  void onVmConfig(VmSettingsWrapper&& config) {
    return call<void>("onVmConfig", config);
  }

  void onAdminVms(const std::vector<AdminVmInfoWrapper>& vms) {
    return call<void>("onAdminVms", vms);
  }

	void onGuacInstr(std::string&& instr_name, const emscripten::val& instr) {
		return call<void>("onGuacInstr", instr_name, instr);
	}
  void onChatMessage(std::uint32_t channel_id, std::string&& username, std::uint8_t user_type, std::string&& message, double timestamp) {
		return call<void>("onChatMessage", channel_id, std::move(username), user_type, std::move(message), timestamp);
  }
  void onConnect(std::string&& username, bool captcha_required) {
		return call<void>("onConnect", std::move(username), captcha_required);
  }
  virtual void onConnectFail() {
    return call<void>("onConnectFail");
  }
  virtual void onUserList(std::uint32_t channel_id, std::vector<std::string>&& usernames, std::vector<std::uint8_t>&& user_types) {
		return call<void>("onUserList", channel_id, usernames, user_types);
  }
  virtual void onUserListAdd(std::uint32_t channel_id, std::string&& username, std::uint8_t user_type) {
		return call<void>("onUserListAdd", channel_id, username, user_type);
  }
  virtual void onUserListRemove(std::uint32_t channel_id, std::string&& username) {
		return call<void>("onUserListRemove", channel_id, username);
  }
  virtual void onAdminUserList(std::uint32_t channel_id, std::vector<std::string>&& usernames, std::vector<std::uint8_t>&& user_types, std::vector<std::vector<std::uint8_t>>&& ip_addresses) {
		return call<void>("onAdminUserList", channel_id, usernames, user_types, ip_addresses);
  }
  virtual void onAdminUserListAdd(std::uint32_t channel_id, std::string&& username, std::uint8_t user_type, std::vector<std::uint8_t> ip_address) {
		return call<void>("onAdminUserListAdd", channel_id, username, user_type, ip_address);
  }
  virtual void onUsernameTaken() {
    return call<void>("onUsernameTaken");
  }
  virtual void onUsernameChange(std::string&& oldUsername, std::string&& newUsername) {
    return call<void>("onUsernameChange", oldUsername, newUsername);
  }
  virtual void onVmDescription(std::string&& description) {
    return call<void>("onVmDescription", std::move(description));
  }
  virtual void onCreateInviteResult(std::vector<std::uint8_t>&& id) {
    return call<void>("onCreateInviteResult", std::move(id));
  }
  virtual void onInviteValidationResponse(bool is_valid, std::string&& username) {
    return call<void>("onInviteValidationResponse", is_valid, std::move(username));
  }
  virtual void onCaptchaRequired(bool required) {
    return call<void>("onCaptchaRequired", required);
  }
	virtual void onVoteDisabled() {
    return call<void>("onVoteDisabled");
  }
	virtual void onVoteIdle() {
    return call<void>("onVoteIdle");
  }
	virtual void onVoteCoolingDown() {
    return call<void>("onVoteCoolingDown");
  }
	virtual void onVoteStatus(std::uint32_t time_remaining, std::uint32_t yes_vote_count, std::uint32_t no_vote_count) {
    return call<void>("onVoteStatus", time_remaining, yes_vote_count, no_vote_count);
  }
  virtual void onVoteResult(bool vote_passed) {
    return call<void>("onVoteResult", vote_passed);
  }
};

struct Serializer {
	/*static CollabVmClientMessage::Message::Builder getClientMessage(capnp::MallocMessageBuilder& message_builder) {
		return std::move(message_builder.initRoot<CollabVmClientMessage::Message>());
	}

	static emscripten::val serialize(capnp::MallocMessageBuilder& message_builder) {
	    const auto array = capnp::messageToFlatArray(message_builder);
const auto byte_array = array.asBytes();
		return std::move(emscripten::val(emscripten::typed_memory_view(byte_array.size(), byte_array.begin())));
	}*/

	void sendVmListRequest() {
		capnp::MallocMessageBuilder message_builder;
		auto message = message_builder.initRoot<CollabVmClientMessage::Message>();
		message.setVmListRequest();
		messageReady(message_builder);
	}

	void sendReadVmsRequest() {
		capnp::MallocMessageBuilder message_builder;
		auto message = message_builder.initRoot<CollabVmClientMessage::Message>();
		message.setReadVms();
		messageReady(message_builder);
	}

  void sendReadVmConfig(std::uint32_t vm_id) {
		capnp::MallocMessageBuilder message_builder;
		auto message = message_builder.initRoot<CollabVmClientMessage::Message>();
		message.setReadVmConfig(vm_id);
		messageReady(message_builder);
  }

  void sendStartVmsRequest(std::vector<std::uint32_t> vm_ids) {
		capnp::MallocMessageBuilder message_builder;
		auto message = message_builder.initRoot<CollabVmClientMessage::Message>();
		message.setStartVms(
        kj::ArrayPtr<std::uint32_t>(vm_ids.data(), vm_ids.size()));
		messageReady(message_builder);
  }

  void sendStopVmsRequest(std::vector<std::uint32_t> vm_ids) {
		capnp::MallocMessageBuilder message_builder;
		auto message = message_builder.initRoot<CollabVmClientMessage::Message>();
		message.setStopVms(
        kj::ArrayPtr<std::uint32_t>(vm_ids.data(), vm_ids.size()));
		messageReady(message_builder);
  }

  void sendRestartVmsRequest(std::vector<std::uint32_t> vm_ids) {
		capnp::MallocMessageBuilder message_builder;
		auto message = message_builder.initRoot<CollabVmClientMessage::Message>();
		message.setRestartVms(
        kj::ArrayPtr<std::uint32_t>(vm_ids.data(), vm_ids.size()));
		messageReady(message_builder);
  }

	void sendLoginRequest(const std::string& username, const std::string& password, const std::string& captcha_token) {
		capnp::MallocMessageBuilder message_builder;
		auto message = message_builder.initRoot<CollabVmClientMessage::Message>();
		auto loginRequest = message.initLoginRequest();
		loginRequest.setUsername(username);
		loginRequest.setPassword(password);
		loginRequest.setCaptchaToken(captcha_token);
		messageReady(message_builder);
	}

	void sendAccountRegistrationRequest(const std::string& username, const std::string& password, const std::string& two_factor_token, const std::vector<std::uint8_t>& invite_id, const std::string& captcha_token) {
		capnp::MallocMessageBuilder message_builder;
		auto message = message_builder.initRoot<CollabVmClientMessage::Message>();
		auto registration_request = message.initAccountRegistrationRequest();
		registration_request.setUsername(username);
		registration_request.setPassword(password);
		if (!two_factor_token.empty()) {
			registration_request.setTwoFactorToken(capnp::Data::Reader(reinterpret_cast<const uint8_t*>(two_factor_token.c_str()), two_factor_token.length()));
		}
    if (!invite_id.empty()) {
      registration_request.setInviteId(kj::ArrayPtr<const std::uint8_t>(invite_id.data(), invite_id.size()));
    }
    registration_request.setCaptchaToken(captcha_token);
		messageReady(message_builder);
	}

	void sendCaptchaCompleted(const std::string& captcha_token) {
		capnp::MallocMessageBuilder message_builder;
		auto message = message_builder.initRoot<CollabVmClientMessage::Message>();
		message.setCaptchaCompleted(captcha_token);
		messageReady(message_builder);
	}

	void sendServerConfigRequest() {
		capnp::MallocMessageBuilder message_builder;
		message_builder.initRoot<CollabVmClientMessage::Message>().setServerConfigRequest();
		messageReady(message_builder);
	}

	void sendServerConfigModifications(ServerSettingsWrapper& serverSettings) {
		capnp::MallocMessageBuilder message_builder;
		serverSettings.getServerConfigModifications(message_builder);
    messageReady(message_builder);
	}

	void sendCreateVmRequest(VmSettingsWrapper& vmSettings) {
		capnp::MallocMessageBuilder message_builder;
		auto settings = message_builder.initRoot<CollabVmClientMessage::Message>().initCreateVm(vmSettings.size());
		vmSettings.getCreateVmRequest(settings);
		messageReady(message_builder);
	}

	void sendVmSettings(std::uint32_t vm_id, VmSettingsWrapper& vmSettings) {
		capnp::MallocMessageBuilder message_builder;
		auto settings = message_builder.initRoot<CollabVmClientMessage::Message>().initUpdateVmConfig();
    settings.setId(vm_id);
    auto modifications = settings.initModifications(vmSettings.size());
    vmSettings.getCreateVmRequest(modifications);
		messageReady(message_builder);
	}

  static capnp::DynamicValue::Reader convertEmscriptenValToCapnpVal(
      const emscripten::val& value) {
    // switch statement would be better here
    if (value.isNumber()) {
      return value.as<double>();
    }
    if (value.isString()) {
      return capnp::Text::Reader(value.as<std::string>());
    }
    if (value.isTrue()) {
      return true;
    }
    if (value.isFalse()) {
      return false;
    }
    // null, undefined, arrays, and objects are not supported
    assert(false);
  }

  void sendGuacInstr(const std::string& instr_name,
                     emscripten::val&& arguments) {
    if (!arguments.isArray()) {
      std::cerr << "second argument must be array" << '\n';
      return;
    }
    const auto args_length = arguments["length"].as<unsigned>();

    capnp::MallocMessageBuilder message_builder;
    auto instr = message_builder.initRoot<CollabVmClientMessage::Message>().initGuacInstr();
    capnp::DynamicStruct::Builder dynamic_reader = instr;

    auto fields = dynamic_reader.getSchema().getUnionFields();
    const auto instr_field = std::find_if(fields.begin(), fields.end(),
        [&instr_name](auto field)
        {
          return field.getProto().getName() == instr_name;
        });
    if (instr_field == fields.end())
    {
      std::cerr << "Unknown instruction '" << instr_name << "'\n";
      return;
    }

    if (instr_field->getIndex() == Guacamole::GuacClientInstruction::Which::NOP)
    {
      return;
    }

    const auto instr_type = instr_field->getType();
    if (!instr_type.isStruct()) {
      if (instr_type.isVoid()) {
        assert(!args_length);
      } else {
        assert(args_length == 1);
        dynamic_reader.set(*instr_field,
                           convertEmscriptenValToCapnpVal(arguments[0]));
      }
      messageReady(message_builder);
      return;
    }

    auto field_builder = dynamic_reader.init(*instr_field);
    auto instr_builder = field_builder.as<capnp::DynamicStruct>();
    if (args_length != instr_builder.getSchema().getFields().size()) {
      std::cerr << "incorrect number of args" << std::endl;
      assert(false);
    }

    auto arg_index = 0u;
    for (auto field : instr_builder.getSchema().getFields()) {
      if (field.getType().isStruct()) {
        std::cerr << "nested structs not supported" << '\n';
        assert(false);
      }
      instr_builder.set(field, convertEmscriptenValToCapnpVal(arguments[arg_index++]));
    }

		messageReady(message_builder);
  }

	void sendConnectRequest(std::uint32_t id) {
		capnp::MallocMessageBuilder message_builder;
		message_builder.initRoot<CollabVmClientMessage::Message>().setConnectToChannel(id);
		messageReady(message_builder);
  }

	void changeUsername(std::string new_username) {
		capnp::MallocMessageBuilder message_builder;
		message_builder.initRoot<CollabVmClientMessage::Message>().setChangeUsername(new_username);
		messageReady(message_builder);
  }

	void sendChatMessage(std::uint32_t id, const std::string& message) {
		capnp::MallocMessageBuilder message_builder;
		auto chat_message = message_builder.initRoot<CollabVmClientMessage::Message>().initChatMessage();
    chat_message.setMessage(message);
    auto destination = chat_message.initDestination();
    destination.initDestination().setVm(id);
		messageReady(message_builder);
  }

	void sendDeleteVm(std::uint32_t id) {
		capnp::MallocMessageBuilder message_builder;
		message_builder.initRoot<CollabVmClientMessage::Message>().setDeleteVm(id);
		messageReady(message_builder);
  }

	void sendTurnRequest() {
		capnp::MallocMessageBuilder message_builder;
		message_builder.initRoot<CollabVmClientMessage::Message>().setTurnRequest();
		messageReady(message_builder);
  }

  void sendVote(bool voted_yes) {
		capnp::MallocMessageBuilder message_builder;
		message_builder.initRoot<CollabVmClientMessage::Message>().setVote(voted_yes);
		messageReady(message_builder);
  }

	void sendCaptcha(const std::string& username, std::uint32_t id) {
		capnp::MallocMessageBuilder message_builder;
		auto message = message_builder.initRoot<CollabVmClientMessage::Message>();
		auto captcha = message.initSendCaptcha();
    captcha.setUsername(username);
    captcha.setChannel(id);
		messageReady(message_builder);
	}

	void kickUser(const std::string& username, std::uint32_t id) {
		capnp::MallocMessageBuilder message_builder;
		auto message = message_builder.initRoot<CollabVmClientMessage::Message>();
		auto captcha = message.initKickUser();
    captcha.setUsername(username);
    captcha.setChannel(id);
		messageReady(message_builder);
	}

  void sendBanIpRequest(std::vector<std::uint8_t> ip_address_bytes) {
		capnp::MallocMessageBuilder message_builder;
		auto message = message_builder.initRoot<CollabVmClientMessage::Message>();
		auto ip_address = message.initBanIp();
		assert(ip_address_bytes.size() == 16);
		auto first = std::uint64_t();
    for (auto i = 0u; i < 8; i++) {
			first |= (ip_address_bytes[i] << ((7 - i) * 8));
    }
		auto second = std::uint64_t();
    for (auto i = 0u; i < 8; i++) {
			second |= (ip_address_bytes[8 + i] << ((7 - i) * 8));
    }
    ip_address.setFirst(first);
    ip_address.setSecond(second);
		messageReady(message_builder);
  }

	void pauseTurnTimer() {
		capnp::MallocMessageBuilder message_builder;
		message_builder.initRoot<CollabVmClientMessage::Message>().setPauseTurnTimer();
		messageReady(message_builder);
  }

	void resumeTurnTimer() {
		capnp::MallocMessageBuilder message_builder;
		message_builder.initRoot<CollabVmClientMessage::Message>().setResumeTurnTimer();
		messageReady(message_builder);
  }

	void endTurn() {
		capnp::MallocMessageBuilder message_builder;
		message_builder.initRoot<CollabVmClientMessage::Message>().setEndTurn();
		messageReady(message_builder);
  }

	void createUserInvite(UserInviteWrapper& invite) {
		capnp::MallocMessageBuilder message_builder;
		auto message = message_builder.initRoot<CollabVmClientMessage::Message>();
    auto invite_message = message.initCreateInvite();
    invite_message.setInviteName(invite.inviteName_);
    invite_message.setUsername(invite.username_);
    invite_message.setAdmin(invite.admin_);
    messageReady(message_builder);
	}

	void validateInvite(const std::vector<std::uint8_t>& invite_id) {
		capnp::MallocMessageBuilder message_builder;
		auto message = message_builder.initRoot<CollabVmClientMessage::Message>();
		message.setValidateInvite(kj::ArrayPtr<const std::uint8_t>(invite_id.data(), invite_id.size()));
		messageReady(message_builder);
	}

	virtual void onMessageReady(const emscripten::val& message) = 0;

  virtual ~Serializer() = default;
private:
	void messageReady(capnp::MallocMessageBuilder& message_builder) {
		const auto word_array = capnp::messageToFlatArray(message_builder);
		const auto byte_array = word_array.asBytes();
		onMessageReady(emscripten::val(
          emscripten::typed_memory_view(byte_array.size(), byte_array.begin())));
	}
};

struct SerializerWrapper : public emscripten::wrapper<Serializer> {
	EMSCRIPTEN_WRAPPER(SerializerWrapper);

	void onMessageReady(const emscripten::val& message) {
		return call<void>("onMessageReady", message);
	}
};

EMSCRIPTEN_BINDINGS(deserializer) {

	emscripten::register_vector<std::string>("StringVector");
	emscripten::register_vector<std::uint32_t>("UInt32Vector");
	emscripten::register_vector<std::uint8_t>("UInt8Vector");
	emscripten::register_vector<std::vector<std::uint8_t>>("IpAddressesVector");
	emscripten::register_vector<VmInfoWrapper>("VmInfoVector");
	emscripten::register_vector<AdminVmInfoWrapper>("AdminVmInfoVector");
	emscripten::register_vector<GuacamoleParameterWrapper>("GuacamoleParameters");
	//emscripten::internal::_embind_register_std_string(emscripten::internal::TypeID<capnp::Text::Reader>::get(), "capnp::Text::Reader");
emscripten::value_object<GuacamoleParameterWrapper>("GuacamoleParameter")
    .field("name", &GuacamoleParameterWrapper::getName, &GuacamoleParameterWrapper::setName)
    .field("value", &GuacamoleParameterWrapper::getValue, &GuacamoleParameterWrapper::setValue)
;

	emscripten::value_object<VmInfoWrapper>("VmInfo")
	.field("id", &VmInfoWrapper::getId, &VmInfoWrapper::setId)
	.field("name", &VmInfoWrapper::getName, &VmInfoWrapper::setName)
	.field("host", &VmInfoWrapper::getHost, &VmInfoWrapper::setHost)
	.field("address", &VmInfoWrapper::getAddress, &VmInfoWrapper::setAddress)
	.field("operatingSystem", &VmInfoWrapper::getOperatingSystem, &VmInfoWrapper::setOperatingSystem)
	.field("uploads", &VmInfoWrapper::getUploads, &VmInfoWrapper::setUploads)
	.field("input", &VmInfoWrapper::getInput, &VmInfoWrapper::setInput)
	.field("ram", &VmInfoWrapper::getRam, &VmInfoWrapper::setRam)
	.field("disk", &VmInfoWrapper::getDiskSpace, &VmInfoWrapper::setDiskSpace)
	.field("safeForWork", &VmInfoWrapper::getSafeForWork, &VmInfoWrapper::setSafeForWork)
	.field("viewerCount", &VmInfoWrapper::getViewerCount, &VmInfoWrapper::setViewerCount)
	;                                                                                        
	emscripten::value_object<AdminVmInfoWrapper>("AdminVmInfo")
    .field("id", &AdminVmInfoWrapper::getId, &AdminVmInfoWrapper::setId)
    .field("name", &AdminVmInfoWrapper::getName, &AdminVmInfoWrapper::setName)
    .field("status", &AdminVmInfoWrapper::getStatus, &AdminVmInfoWrapper::setStatus)
	;

	emscripten::value_object<LoginWrapper>("Login")
	.field("username", &LoginWrapper::getUsername, &LoginWrapper::setUsername)
	.field("password", &LoginWrapper::getPassword, &LoginWrapper::setPassword)
	;

emscripten::value_object<RegisterAccountWrapper>("RegisterAccount")
        .field("username", &RegisterAccountWrapper::getUsername, &RegisterAccountWrapper::setUsername)
        .field("password", &RegisterAccountWrapper::getPassword, &RegisterAccountWrapper::setPassword)
        .field("twoFactorToken", &RegisterAccountWrapper::getTwoFactorToken, &RegisterAccountWrapper::setTwoFactorToken)
;

emscripten::value_object<UserInviteWrapper>("UserInvite")
        .field("id", &UserInviteWrapper::getId, &UserInviteWrapper::setId)
        .field("inviteName", &UserInviteWrapper::getInviteName, &UserInviteWrapper::setInviteName)
        .field("username", &UserInviteWrapper::getUsername, &UserInviteWrapper::setUsername)
        .field("admin", &UserInviteWrapper::getAdmin, &UserInviteWrapper::setAdmin)
;

emscripten::value_object<CaptchaWrapper>("Captcha")
    .field("enabled", &CaptchaWrapper::getEnabled, &CaptchaWrapper::setEnabled)
    .field("https", &CaptchaWrapper::getHttps, &CaptchaWrapper::setHttps)
    .field("urlHost", &CaptchaWrapper::getUrlHost, &CaptchaWrapper::setUrlHost)
    .field("urlPort", &CaptchaWrapper::getUrlPort, &CaptchaWrapper::setUrlPort)
    .field("urlPath", &CaptchaWrapper::getUrlPath, &CaptchaWrapper::setUrlPath)
    .field("postParams", &CaptchaWrapper::getPostParams, &CaptchaWrapper::setPostParams)
    .field("validJSONVariableName", &CaptchaWrapper::getValidJSONVariableName, &CaptchaWrapper::setValidJSONVariableName)
;

emscripten::class_<ServerSettingsWrapper>("ServerSetting")
    .function("getAllowAccountRegistration", &ServerSettingsWrapper::getAllowAccountRegistration)
    .function("setAllowAccountRegistration", &ServerSettingsWrapper::setAllowAccountRegistration)
    .function("getCaptcha", &ServerSettingsWrapper::getCaptcha)
    .function("setCaptcha", &ServerSettingsWrapper::setCaptcha)
    .function("getCaptchaRequired", &ServerSettingsWrapper::getCaptchaRequired)
    .function("setCaptchaRequired", &ServerSettingsWrapper::setCaptchaRequired)
    .function("getUserVmsEnabled", &ServerSettingsWrapper::getUserVmsEnabled)
    .function("setUserVmsEnabled", &ServerSettingsWrapper::setUserVmsEnabled)
    .function("getAllowUserVmRequests", &ServerSettingsWrapper::getAllowUserVmRequests)
    .function("setAllowUserVmRequests", &ServerSettingsWrapper::setAllowUserVmRequests)
    .function("getBanIpCommand", &ServerSettingsWrapper::getBanIpCommand)
    .function("setBanIpCommand", &ServerSettingsWrapper::setBanIpCommand)
    .function("getUnbanIpCommand", &ServerSettingsWrapper::getUnbanIpCommand)
    .function("setUnbanIpCommand", &ServerSettingsWrapper::setUnbanIpCommand)
    .function("getMaxConnectionsEnabled", &ServerSettingsWrapper::getMaxConnectionsEnabled)
    .function("setMaxConnectionsEnabled", &ServerSettingsWrapper::setMaxConnectionsEnabled)
    .function("getMaxConnections", &ServerSettingsWrapper::getMaxConnections)
    .function("setMaxConnections", &ServerSettingsWrapper::setMaxConnections)
;

	emscripten::class_<Deserializer>("Deserializer")
	.function("deserialize", &Deserializer::deserialize)
	.function("deserializeGuacInstr", &Deserializer::deserializeGuacInstr)
	.function("test", &Deserializer::test)
	.function("onVmInfo", &Deserializer::onVmInfo, emscripten::pure_virtual())
	.function("onVmTurnInfo", &Deserializer::onVmTurnInfo, emscripten::pure_virtual())
	.function("onChatMessage", &Deserializer::onChatMessage, emscripten::pure_virtual())
	.function("onVmDescription", &Deserializer::onVmDescription, emscripten::pure_virtual())
	.function("onUserList", &Deserializer::onUserList, emscripten::pure_virtual())
	.function("onUserListAdd", &Deserializer::onUserListAdd, emscripten::pure_virtual())
	.function("onUserListRemove", &Deserializer::onUserListRemove, emscripten::pure_virtual())
	.function("onAdminUserList", &Deserializer::onAdminUserList, emscripten::pure_virtual())
	.function("onAdminUserListAdd", &Deserializer::onAdminUserListAdd, emscripten::pure_virtual())
	.function("onConnect", &Deserializer::onConnect, emscripten::pure_virtual())
	.function("onConnectFail", &Deserializer::onConnectFail, emscripten::pure_virtual())
	.function("onVmThumbnail", &Deserializer::onVmThumbnail, emscripten::pure_virtual())
	.function("onRegisterAccountSucceeded", &Deserializer::onRegisterAccountSucceeded, emscripten::pure_virtual())
	.function("onRegisterAccountFailed", &Deserializer::onRegisterAccountFailed, emscripten::pure_virtual())
	.allow_subclass<DeserializerWrapper>("DeserializerWrapper")
	;

	emscripten::class_<Serializer>("Serializer")
	.function("sendVmListRequest", &Serializer::sendVmListRequest)
	.function("sendReadVmsRequest", &Serializer::sendReadVmsRequest)
	.function("sendReadVmConfig", &Serializer::sendReadVmConfig)
	.function("sendStartVmsRequest", &Serializer::sendStartVmsRequest)
	.function("sendStopVmsRequest", &Serializer::sendStopVmsRequest)
	.function("sendRestartVmsRequest", &Serializer::sendRestartVmsRequest)
	.function("sendLoginRequest", &Serializer::sendLoginRequest)
	.function("sendAccountRegistrationRequest", &Serializer::sendAccountRegistrationRequest)
	.function("sendServerConfigRequest", &Serializer::sendServerConfigRequest)
	.function("sendCreateVmRequest", &Serializer::sendCreateVmRequest)
  .function("sendGuacInstr", &Serializer::sendGuacInstr)
	.function("onMessageReady", &Serializer::onMessageReady, emscripten::pure_virtual())
	.function("sendServerConfigModifications", &Serializer::sendServerConfigModifications)
	.function("sendVmSettings", &Serializer::sendVmSettings)
	.function("sendConnectRequest", &Serializer::sendConnectRequest)
	.function("changeUsername", &Serializer::changeUsername)
	.function("sendChatMessage", &Serializer::sendChatMessage)
	.function("sendDeleteVm", &Serializer::sendDeleteVm)
	.function("sendTurnRequest", &Serializer::sendTurnRequest)
	.function("sendVote", &Serializer::sendVote)
	.function("sendCaptcha", &Serializer::sendCaptcha)
	.function("kickUser", &Serializer::kickUser)
	.function("sendBanIpRequest", &Serializer::sendBanIpRequest)
	.function("pauseTurnTimer", &Serializer::pauseTurnTimer)
	.function("resumeTurnTimer", &Serializer::resumeTurnTimer)
	.function("endTurn", &Serializer::endTurn)
	.function("createUserInvite", &Serializer::createUserInvite)
	.function("validateInvite", &Serializer::validateInvite)
	.function("sendCaptchaCompleted", &Serializer::sendCaptchaCompleted)
	.allow_subclass<SerializerWrapper>("SerializerWrapper")
	;
}

EMSCRIPTEN_BINDINGS(settings) {

emscripten::class_<VmSettingsWrapper>("VmSettings")
	.constructor()
    .function("getAutoStart", &VmSettingsWrapper::getAutoStart)
    .function("setAutoStart", &VmSettingsWrapper::setAutoStart)
    .function("getName", &VmSettingsWrapper::getName)
    .function("setName", &VmSettingsWrapper::setName)
    .function("getDescription", &VmSettingsWrapper::getDescription)
    .function("setDescription", &VmSettingsWrapper::setDescription)
    .function("getHost", &VmSettingsWrapper::getHost)
    .function("setHost", &VmSettingsWrapper::setHost)
    .function("getOperatingSystem", &VmSettingsWrapper::getOperatingSystem)
    .function("setOperatingSystem", &VmSettingsWrapper::setOperatingSystem)
    .function("getRam", &VmSettingsWrapper::getRam)
    .function("setRam", &VmSettingsWrapper::setRam)
    .function("getDiskSpace", &VmSettingsWrapper::getDiskSpace)
    .function("setDiskSpace", &VmSettingsWrapper::setDiskSpace)
    .function("getStartCommand", &VmSettingsWrapper::getStartCommand)
    .function("setStartCommand", &VmSettingsWrapper::setStartCommand)
    .function("getStopCommand", &VmSettingsWrapper::getStopCommand)
    .function("setStopCommand", &VmSettingsWrapper::setStopCommand)
    .function("getRestartCommand", &VmSettingsWrapper::getRestartCommand)
    .function("setRestartCommand", &VmSettingsWrapper::setRestartCommand)
    .function("getTurnsEnabled", &VmSettingsWrapper::getTurnsEnabled)
    .function("setTurnsEnabled", &VmSettingsWrapper::setTurnsEnabled)
    .function("getTurnTime", &VmSettingsWrapper::getTurnTime)
    .function("setTurnTime", &VmSettingsWrapper::setTurnTime)
    .function("getUploadsEnabled", &VmSettingsWrapper::getUploadsEnabled)
    .function("setUploadsEnabled", &VmSettingsWrapper::setUploadsEnabled)
    .function("getUploadCooldownTime", &VmSettingsWrapper::getUploadCooldownTime)
    .function("setUploadCooldownTime", &VmSettingsWrapper::setUploadCooldownTime)
    .function("getMaxUploadSize", &VmSettingsWrapper::getMaxUploadSize)
    .function("setMaxUploadSize", &VmSettingsWrapper::setMaxUploadSize)
    .function("getVotesEnabled", &VmSettingsWrapper::getVotesEnabled)
    .function("setVotesEnabled", &VmSettingsWrapper::setVotesEnabled)
    .function("getVoteTime", &VmSettingsWrapper::getVoteTime)
    .function("setVoteTime", &VmSettingsWrapper::setVoteTime)
    .function("getVoteCooldownTime", &VmSettingsWrapper::getVoteCooldownTime)
    .function("setVoteCooldownTime", &VmSettingsWrapper::setVoteCooldownTime)
    .function("getAgentAddress", &VmSettingsWrapper::getAgentAddress)
    .function("setAgentAddress", &VmSettingsWrapper::setAgentAddress)
    .function("getProtocol", &VmSettingsWrapper::getProtocol)
    .function("setProtocol", &VmSettingsWrapper::setProtocol)
    .function("getGuacamoleParameters", &VmSettingsWrapper::getGuacamoleParameters)
    .function("setGuacamoleParameters", &VmSettingsWrapper::setGuacamoleParameters)
    .function("getSafeForWork", &VmSettingsWrapper::getSafeForWork)
    .function("setSafeForWork", &VmSettingsWrapper::setSafeForWork)
    .function("getDisallowGuests", &VmSettingsWrapper::getDisallowGuests)
    .function("setDisallowGuests", &VmSettingsWrapper::setDisallowGuests)
;
}

struct Constants {
  template<typename T>
  static constexpr std::int32_t toMilliseconds(T time) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(time).count();
  }
  static constexpr std::int32_t getMaxChatMessageLength() { return CollabVm::Common::max_chat_message_len; }
  static constexpr std::int32_t getChatRateLimit() { return toMilliseconds(CollabVm::Common::chat_rate_limit); }
  static constexpr std::int32_t getUsernameChangeRateLimit() { return toMilliseconds(CollabVm::Common::username_change_rate_limit); }
};

EMSCRIPTEN_BINDINGS(Constants) {
  emscripten::class_<Constants>("Constants")
    .class_function("getMaxChatMessageLength", &Constants::getMaxChatMessageLength)
    .class_function("getChatRateLimit", &Constants::getChatRateLimit)
    .class_function("getUsernameChangeRateLimit", &Constants::getUsernameChangeRateLimit)
    ;
}
