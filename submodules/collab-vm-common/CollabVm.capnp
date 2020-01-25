@0xb9e188bf95349e81;
using import "Guacamole.capnp".GuacServerInstruction;
using import "Guacamole.capnp".GuacClientInstruction;
using VmId = UInt32;
using InviteId = Data;

struct CollabVmServerMessage {
  struct VmInfo {
    id @0 :VmId;
    name @1 :Text;
    host @2 :Text;
    address @3 :Text;
    operatingSystem @4 :Text;
    uploads @5 :Bool;
    input @6 :Bool;
    ram @7 :UInt8;
    diskSpace @8 :UInt16;
    safeForWork @9 :Bool;
    viewerCount @10 :UInt16;
  }

  struct VmThumbnail {
    id @0 :VmId;
    pngBytes @1 :Data;
  }

  enum VmStatus {
    stopped @0;
    starting @1;
    running @2;
  }

  struct AdminVmInfo {
    id @0 :VmId;
    name @1 :Text;
    status @2 :VmStatus;
  }

  struct ChannelChatMessage {
    channel @0 :VmId;
    message @1 :ChatMessage;
  }

  struct ChannelChatMessages {
    channel @0 :UInt32;
    # Circular buffer
    messages @1 :List(ChatMessage);
    # Index of the oldest chat message
    firstMessage @2 :UInt8;
    count @3 :UInt8;
  }

  struct ChatMessage {
    sender @0 :Text;
    message @1 :Text;
    userType @2 :UserType;
    timestamp @3 :UInt64;
  }

  struct Session {
    sessionId @0 :Data;
    username @1 :Text;
    isAdmin @2 :Bool;
  }

  struct LoginResponse {
    enum LoginResult {
      invalidCaptchaToken @0;
      invalidUsername @1;
      invalidPassword @2;
      twoFactorRequired @3;
      accountDisabled @4;
      success @5; # Only used by server
    }

    result :union {
      session @0 :Session;
      result @1 :LoginResult;
    }
  }

  struct RegisterAccountResponse {
    enum RegisterAccountError {
      invalidCaptchaToken @0;
      usernameTaken @1;
      usernameInvalid @2;
      passwordInvalid @3;
      inviteInvalid @4;
      totpError @5;
      success @6; # Only used by server
    }

    result :union {
      session @0 :Session;
      errorStatus @1 :RegisterAccountError;
    }
  }

  struct ChannelConnectResponse {
    struct ConnectInfo {
      chatMessages @0 :List(ChatMessage);
      username @1 :Text;
      captchaRequired @2 :Bool;
    }
    result :union {
      success @0 :ConnectInfo;
      fail @1 :Void;
    }
  }

  struct UsernameChange {
    oldUsername @0 :Text;
    newUsername @1 :Text;
  }

  enum ChatMessageResponse {
    success @0;
    userNotFound @1;
    # The user has too many chat rooms open
    userChatLimit @2;
    # The recipient has too many chat rooms open
    recipientChatLimit @3;
  }

  struct UserInvite {
    id @0 :InviteId;
    inviteName @1 :Text;
    username @2 :Text;
    admin @3 :Bool;
    #vmHost @5 :Bool;
  }

  enum TurnState {
    disabled @0;
    enabled @1;
    paused @2;
  }

  struct VmTurnInfo {
    state @0 :TurnState;
    users @1 :List(Text);
    timeRemaining @2 :UInt32;
  }

  enum UserType {
    guest @0;
    regular @1;
    admin @2;
  }

  struct User {
    username @0 :Text;
    userType @1 :UserType;
  }

  struct UserList {
    channel @0 :VmId;
    users @1 :List(User);
  }

  struct UserListUpdate {
    channel @0 :VmId;
    username @1 :Text;
    userType @2 :UserType;
  }

  struct UserListRemove {
    channel @0 :VmId;
    username @1 :Text;
  }
  struct UserAdmin {
    username @0 :Text;
    userType @1 :UserType;
    ipAddress @2 :IpAddress;
  }

  struct AdminUserListUpdate {
    channel @0 :VmId;
    user @1 :UserAdmin;
  }

  struct AdminUserList {
    channel @0 :VmId;
    users @1 :List(UserAdmin);
  }

  struct InviteValidationResponse {
    isValid @0 :Bool;
    username @1 :Text;
  }

  message :union {
    vmListResponse @0 :List(VmInfo);
    vmThumbnail @1 :VmThumbnail;
    chatMessage @2 :ChannelChatMessage;
    chatMessages @3 :ChannelChatMessages;
    loginResponse @4 :LoginResponse;
    accountRegistrationResponse @5 :RegisterAccountResponse;
    serverSettings @6 :List(ServerSetting);
    connectResponse @7 :ChannelConnectResponse;
    usernameTaken @8 :Void;
    changeUsername @9 :UsernameChange;
    chatMessageResponse @10 :ChatMessageResponse;
    newChatChannel @11 :ChannelChatMessage;
    reserveUsernameResult @12 :Bool;
    createInviteResult @13 :InviteId;
    readInvitesResponse @14 :List(UserInvite);
    updateInviteResult @15 :Bool;
    readReservedUsernamesResponse @16 :List(Text);
    readVmsResponse @17 :List(AdminVmInfo);
    readVmConfigResponse @18 :List(VmSetting);
    createVmResponse @19 :UInt32;
    guacInstr @20 :GuacServerInstruction;
    changePasswordResponse @21 :Bool;
    vmTurnInfo @22 :VmTurnInfo;
    userList @23 :UserList;
    userListAdd @24 :UserListUpdate;
    userListRemove @25 :UserListRemove;
    adminUserList @26 :AdminUserList;
    adminUserListAdd @27 :AdminUserListUpdate;
    vmDescription @28 :Text;
    inviteValidationResponse @29 :InviteValidationResponse;
    captchaRequired @30 :Bool;
    voteStatus @31 :VoteStatus;
    voteResult @32 :Bool;
  }
}

struct VoteStatus {
  status :union {
    disabled @0 :Void;
    idle @1 :Void;
    coolingDown @2 :Void;
    inProgress @3 :VoteInfo;
  }
  struct VoteInfo {
    timeRemaining @0 :UInt32;
    yesVoteCount @1 :UInt32;
    noVoteCount @2 :UInt32;
  }
}

struct ServerSetting {
  setting :union {
    allowAccountRegistration @0 :Bool = true;
    captcha @1 :Captcha;
    captchaRequired @2 :Bool;
    userVmsEnabled @3 :Bool;
    allowUserVmRequests @4 :Bool;
    banIpCommand @5 :Text;
    unbanIpCommand @6 :Text;
    maxConnectionsEnabled @7 :Bool;
    maxConnections @8 :UInt8;
  }
  struct Captcha {
    enabled @0 :Bool;
    https @1 : Bool;
    urlHost @2 :Text;
    urlPort @3 :UInt16;
    urlPath @4 :Text;
    postParams @5 :Text;
    validJSONVariableName @6 :Text;
  }
}

struct VmSetting {
  struct VmSnapshot {
    name @0 :Text;
    command @1 :Text;
  }

  enum Protocol {
    rdp @0;
    vnc @1;
  }

  enum SocketType {
    # Unix Domain Socket or named pipe on Windows
    local @0;
    tcp @1;
  }

  struct GuacamoleParameter {
    name @0 :Text;
    value @1 :Text;
  }

  setting :union {
    autoStart @0 :Bool;
    name @1 :Text;
    description @2 :Text;
    safeForWork @3 :Bool;
    host @4 :Text;
    operatingSystem @5 :Text;
    ram @6 :UInt8;
    diskSpace @7 :UInt8;
    startCommand @8 :Text;
    stopCommand @9 :Text;
    restartCommand @10 :Text;
    snapshotCommands @11 :List(VmSnapshot);
    # Allow users to take turns controlling the VM
    turnsEnabled @12 :Bool;
    # Number of seconds a turn will last
    turnTime @13 :UInt16 = 20;
    # Allow users to upload files to the VM
    uploadsEnabled @14 :Bool;
    # Number of seconds a user must wait in between uploads
    uploadCooldownTime @15 :UInt16 = 180;
    # Max number of bytes a user is allowed to upload
    maxUploadSize @16 :UInt32 = 15728640; # 15 MiB
    # Allow users to vote for resetting the VM
    votesEnabled @17 :Bool;
    # Number of seconds a vote will last
    voteTime @18 :UInt16 = 60;
    # Number of seconds in between votes
    voteCooldownTime @19 :UInt16 = 600;
    agentAddress @20 :Text;
    protocol @21 :Protocol;
    guacamoleParameters @22 :List(GuacamoleParameter);
    disallowGuests @23 :Bool;
  }
}

struct VmConfigModifications {
  id @0 :UInt32;
  modifications @1 :List(VmSetting);
}

struct CollabVmClientMessage {
  message :union { 
    connectToChannel @0 :UInt32;
    chatMessage @1 :ChatMessage;
    vmListRequest @2 :Void;
    loginRequest @3 :Login;
    twoFactorResponse @4 :UInt32;
    accountRegistrationRequest @5 :RegisterAccount;
    changeUsername @6 :Text;
    # Server config
    serverConfigRequest @7 :Void;
    serverConfigModifications @8 :List(ServerSetting);
    serverConfigHidden @9 :Void;
    # VM config
    createVm @10 :List(VmSetting);
    readVms @11 :Void;
    readVmConfig @12 :UInt32;
    updateVmConfig @13 :VmConfigModifications;
    deleteVm @14 :UInt32;
    # Reserved usernames
    createReservedUsername @15 :Text;
    readReservedUsernames @16 :Text;
    deleteReservedUsername @17 :Text;
    # User invites
    createInvite @18 :UserInvite;
    readInvites @19 :Void;
    updateInvite @20 :UserInvite;
    deleteInvite @21 :InviteId;
    changePasswordRequest @22 :ChangePasswordRequest;
    startVms @23 :List(VmId);
    stopVms @24 :List(VmId);
    restartVms @25 :List(VmId);
    guacInstr @26 :GuacClientInstruction;
    turnRequest @27 :Void;
    banIp @28 :IpAddress;
    kickUser @29 :KickUserRequest;
    sendCaptcha @30 :SendCaptchaRequest;
    pauseTurnTimer @31 :Void;
    resumeTurnTimer @32 :Void;
    endTurn @33 :Void;
    validateInvite @34 :InviteId;
    captchaCompleted @35 :Text;
    vote @36 :Bool;
  }

  struct ChangePasswordRequest {
    oldPassword @0 :Text;
    newPassword @1 :Text;
  }

  struct UserInvite {
    id @0 :InviteId;
    inviteName @1 :Text;
    username @2 :Text;
    admin @3 :Bool;
    #vmHost @4 :Bool;
  }

  struct ChatMessage {
    message @0 :Text;
    destination @1 :ChatMessageDestination;
  }

  struct ChatMessageDestination {
    destination :union {
      vm @0 :UInt32;
      newDirect @1 :Text;
      direct @2 :UInt8;
    }
  }
  
  struct Login {
    username @0 :Text;
    password @1 :Text;
    captchaToken @2 :Text;
  }

  struct RegisterAccount {
    username @0 :Text;
    password @1 :Text;
    twoFactorToken @2 :Data;
    inviteId @3 :InviteId;
    captchaToken @4 :Text;
  }

  struct KickUserRequest {
    username @0 :Text;
    channel @1 :VmId;
    message @2 :Text;
  }

  struct SendCaptchaRequest {
    username @0 :Text;
    channel @1 :VmId;
  }
}

struct IpAddress {
  first @0 :UInt64;
  second @1 :UInt64;
}
