import {registerUrlRouter, setUrl, getSocket, addMessageHandlers, createObject, saveSessionInfo, loadSessionInfo} from "common";
import $ from "jquery";
import Tabulator from "tabulator-tables";
import "tabulator_semantic-ui.css";


$(".ui.checkbox").checkbox();
showLoading();

registerUrlRouter(path => {
  const socket = getSocket();
  socket.onSocketDisconnect = () => {
    showLoading();
  };
  if (path === "/admin") {
    socket.onSocketConnect = () => {
      // TODO: Try restoring the session
      /*
      const session = loadSessionInfo();
      if (session.username && session.sessionId) {

      } else {
        showLoginForm();
      }
      */
      showLoginForm();
    };
    if (socket.connected) {
      socket.onSocketConnect();
    }
  } else {
    //hideEverything();
    return false;
  }
});

function showVmConfig(vmConfig) {
  const socket = getSocket();
  function initCheckbox($checkbox, checked, onChange) {
    const setValueString = "set " + (checked ? "checked" : "unchecked");
    $checkbox.parent()
      .checkbox() // Remove any previous handlers
      .checkbox(setValueString) // Set current value
      .checkbox({ // Add a new handler
        onChange: onChange
      });
  }
  initCheckbox($("#vm-settings :checkbox[name='autostart']"),
    vmConfig.getAutoStart(), function() {
        const vmSettings = createObject("VmSettings");
        vmSettings.setAutoStart(this.checked);
        socket.sendVmSettings(currentVmId, vmSettings);
      });

  $("#vm-settings :text[name='name']").val(vmConfig.getName()).off("change")
    .change(function() {
      const vmSettings = createObject("VmSettings");
      vmSettings.setName(this.value);
      socket.sendVmSettings(currentVmId, vmSettings);
    });

  $("#vm-settings :input[name='description']").val(vmConfig.getDescription()).off("change")
    .change(function() {
      const vmSettings = createObject("VmSettings");
      vmSettings.setDescription(this.value);
      socket.sendVmSettings(currentVmId, vmSettings);
    });

  initCheckbox($("#vm-settings :checkbox[name='safe-for-work']"),
    vmConfig.getSafeForWork(), function() {
        const vmSettings = createObject("VmSettings");
        vmSettings.setSafeForWork(this.checked);
        socket.sendVmSettings(currentVmId, vmSettings);
      });

  $("#vm-settings :text[name='operating-system']").val(vmConfig.getOperatingSystem()).off("change")
    .change(function() {
      const vmSettings = createObject("VmSettings");
      vmSettings.setOperatingSystem(this.value);
      socket.sendVmSettings(currentVmId, vmSettings);
    });

  $("#vm-settings :input[name='ram']").val(vmConfig.getRam()).off("change")
    .change(function() {
      const vmSettings = createObject("VmSettings");
      vmSettings.setRam(+this.value);
      socket.sendVmSettings(currentVmId, vmSettings);
    });

  $("#vm-settings :input[name='disk-space']").val(vmConfig.getDiskSpace()).off("change")
    .change(function() {
      const vmSettings = createObject("VmSettings");
      vmSettings.setDiskSpace(+this.value);
      socket.sendVmSettings(currentVmId, vmSettings);
    });

  $("#vm-settings :text[name='start-command']").val(vmConfig.getStartCommand()).off("change")
    .change(function() {
      const vmSettings = createObject("VmSettings");
      vmSettings.setStartCommand(this.value);
      socket.sendVmSettings(currentVmId, vmSettings);
    });

  $("#vm-settings :text[name='stop-command']").val(vmConfig.getStopCommand()).off("change")
    .change(function() {
      const vmSettings = createObject("VmSettings");
      vmSettings.setStopCommand(this.value);
      socket.sendVmSettings(currentVmId, vmSettings);
    });

  $("#vm-settings :text[name='restart-command']").val(vmConfig.getRestartCommand()).off("change")
    .change(function() {
      const vmSettings = createObject("VmSettings");
      vmSettings.setRestartCommand(this.value);
      socket.sendVmSettings(currentVmId, vmSettings);
    });

  initCheckbox($("#vm-settings :checkbox[name='disallow-guests']"),
    vmConfig.getDisallowGuests(), function() {
        const vmSettings = createObject("VmSettings");
        vmSettings.setDisallowGuests(this.checked);
        socket.sendVmSettings(currentVmId, vmSettings);
      });

  initCheckbox($("#vm-settings :checkbox[name='turns-enabled']"),
    vmConfig.getTurnsEnabled(), function() {
        const vmSettings = createObject("VmSettings");
        vmSettings.setTurnsEnabled(this.checked);
        socket.sendVmSettings(currentVmId, vmSettings);
      });

  $("#vm-settings :input[name='turn-time']").val(vmConfig.getTurnTime()).off("change")
    .change(function() {
      const vmSettings = createObject("VmSettings");
      vmSettings.setTurnTime(+this.value);
      socket.sendVmSettings(currentVmId, vmSettings);
    });

  initCheckbox($("#vm-settings :checkbox[name='votes-enabled']"),
    vmConfig.getVotesEnabled(), function() {
        const vmSettings = createObject("VmSettings");
        vmSettings.setVotesEnabled(this.checked);
        socket.sendVmSettings(currentVmId, vmSettings);
      });

  $("#vm-settings :input[name='vote-time']").val(vmConfig.getVoteTime()).off("change")
    .change(function() {
      const vmSettings = createObject("VmSettings");
      vmSettings.setVoteTime(+this.value);
      socket.sendVmSettings(currentVmId, vmSettings);
    });

  $("#vm-settings :input[name='vote-cooldown-time']").val(vmConfig.getVoteCooldownTime()).off("change")
    .change(function() {
      const vmSettings = createObject("VmSettings");
      vmSettings.setVoteCooldownTime(+this.value);
      socket.sendVmSettings(currentVmId, vmSettings);
    });

  initCheckbox($("#vm-settings :checkbox[name='uploads-enabled']"),
    vmConfig.getUploadsEnabled(), function() {
        const vmSettings = createObject("VmSettings");
        vmSettings.setUploadsEnabled(this.checked);
        socket.sendVmSettings(currentVmId, vmSettings);
      });

  $("#vm-settings :input[name='upload-cooldown-time']").val(vmConfig.getUploadCooldownTime()).off("change")
    .change(function() {
      const vmSettings = createObject("VmSettings");
      vmSettings.setUploadCooldownTime(+this.value);
      socket.sendVmSettings(currentVmId, vmSettings);
    });

  // TODO: set select to appropriate unit
  $("#vm-settings :input[name='max-upload-size']").val(vmConfig.getMaxUploadSize()).off("change")
    .change(function() {
      const vmSettings = createObject("VmSettings");
      const unitValue = this.nextElementSibling.value;
      vmSettings.setMaxUploadSize(this.value * Math.pow(2, 10 * unitValue));
      socket.sendVmSettings(currentVmId, vmSettings);
    }).next().off("change").change(() => $(this).prev().trigger("change"));

  $("#vm-settings :input[name='protocol']").val(vmConfig.getProtocol()).off("change")
    .change(function() {
      const vmSettings = createObject("VmSettings");
      vmSettings.setProtocol(+this.value);
      socket.sendVmSettings(currentVmId, vmSettings);
    });

  function sendGuacamoleParameters() {
    const guacParams = 
      guacTable.getData().filter(row => row["name"])
        .map(row => ({name: row.name, value: row.value || ""}))
        .toVector("GuacamoleParameters");
    const vmSettings = createObject("VmSettings");
    vmSettings.setGuacamoleParameters(guacParams);
    socket.sendVmSettings(currentVmId, vmSettings);
  }
  const guacTable = new Tabulator("#guac-table", {
    layout: "fitColumns",
    placeholder: $("#guac-table-placeholder")[0],
    movableRows: true,
    columns:[
      {title: "Name", field: "name", editor: "input"},
      {title: "Value", field: "value", editor: "input"},
      {formatter: "buttonCross", width:"5%", align:"center", headerSort: false,
        cellClick: (e, cell) => cell.getRow().delete()
      }
    ],
    data: Array.from(
      {length: vmConfig.getGuacamoleParameters().size()},
      (_, i) => vmConfig.getGuacamoleParameters().get(i)),
    footerElement: $("#guac-table-footer").children().click(async () =>
      {
        const row = await guacTable.addRow({});
        row.getCell("name").edit();
      }).end()[0],
    dataEdited: sendGuacamoleParameters,
    rowAdded: row => {
    },
    rowUpdated: row => {
    },
    rowDeleted: row => {
    },
    rowMoved: row => {
    }
  });

  $("#delete-vm-button").off("click").click(() => {
    $("#delete-vm-modal").modal({
      onApprove: () => {
        $("#vm-settings").hide("slow");
        socket.sendDeleteVm(currentVmId);
      },
    }).modal("show");
  });

  $("#vm-settings").show("slow");

  // The table must be redrawn after its parent becomes visible
  guacTable.redraw();
}

let currentVmId;
addMessageHandlers({
  onVmCreated: vmId => {
    currentVmId = vmId;
    showVmConfig(createObject("VmSettings"));
  },
  onAdminVms: vmVector => {
    const vms = Array.from(
      {length: vmVector.size()}, (_, i) => vmVector.get(i));
    const vmsList = new Tabulator("#vm-list", {
      layout: "fitColumns",
      placeholder: "No VMs",
      columns: [{title: "Name", field: "name"},
                {title: "Status", field: "status"}],
      data: vms,
      selectable: true,
      rowSelectionChanged: (data, rows) => {
        $("#settings-vm-button")
          .prop("disabled", data.length !== 1)
          .off("click").click(() => {
            currentVmId = data[0].id;
            getSocket().sendReadVmConfig(currentVmId);
          });
        $("#start-vm-button, #stop-vm-button, #restart-vm-button")
          .prop("disabled", !data.length);
        const vmIds = data.map(vm => vm.id).toVector("UInt32Vector");
        $("#start-vm-button").off("click").click(() => {
          getSocket().sendStartVmsRequest(vmIds);
        });
        $("#stop-vm-button").off("click").click(() => {
          getSocket().sendStopVmsRequest(vmIds);
        });
        $("#restart-vm-button").off("click").click(() => {
          getSocket().sendRestartVmsRequest(vmIds);
        });
      }
    });
  },
  onVmConfig: vmConfig => {
    showVmConfig(vmConfig);
  },
  onVmInfo: vmInfo => {
    displayVmList(
      Array.from({length: vmInfo.size()}, (x, i) => vmInfo.get(i)));
    $("#loading").hide();
  },
  onServerConfig: config => {
    showServerConfig(config);
  },
  onRegisterAccountSucceeded: (sessionId, username) => {
    $("#register-button").removeClass("loading");
    saveSessionInfo(sessionId, username);
  },
  onRegisterAccountFailed: error => {
    $("#register-button").removeClass("loading");
    console.error(error);
  },
  onLoginSucceeded: (sessionId, username) => {
    $("#login-button").removeClass("loading");
    saveSessionInfo(sessionId, username);
    getSocket().sendServerConfigRequest();
    getSocket().sendReadVmsRequest();
  },
  onLoginFailed: (error) => {
    $("#login-button").removeClass("loading");
    $("#login-status").text(error);
    showLoginForm();
  },
  onGuacInstr: (name, instr) => {
    collabVmTunnel.oninstruction(name, instr);
  },
  onChatMessage: (channelId, message) => {
    debugger;
  },
  onCreateInviteResult: id => {
    id = Array.from({length: id.size()}, (_, i) => id.get(i));
    if (!id.length) {
      console.error("Failed to create invite");
      return;
    }
    const base64Id = btoa(String.fromCharCode.apply(null, id));
    const link = `${window.location.origin}${__webpack_public_path__}invite/${base64Id}`;
    $("#user-invite-modal-link").attr("href", link).text(link);
    $("#user-invite-modal").modal("show");
  },
});

let twoFactorToken;

$("#validate-2fa-box").keypress(function(event) {
  if (event.which === 13) {
    $("#activate-2fa-modal .ui.ok.button").click();
    return true;
  }
  return this.value.length < 6 && event.which >= 48 && event.which <= 57;
}).on("input",
function() { $("#activate-2fa-modal .ui.ok.button").toggleClass("disabled", !this.value); })
.trigger("input");
$("#enable-2fa-toggle").checkbox({
  onChecked: () => {
    var totp;

    function generateKey() {
      totp = new OTPAuth.TOTP({
        issuer: "CollabVM",
//                            label: $("#username-box").val(),
      algorithm: "SHA1",
      digits: 6,
      period: 30
      });
      $("#qrcode").empty().qrcode(totp.toString());
    }

    if ($.fn.qrcode) {
      generateKey();
    } else {
      var script = document.createElement("script");
      script.type = "text/javascript";
      script.src =
      "https://cdnjs.cloudflare.com/ajax/libs/jquery.qrcode/1.0/jquery.qrcode.min.js";
      script.integrity =
      "sha384-0B/45e2to395pfnCkbfqwKFFwAa7zXdvd42eAFJa3Vm8KZ/jmHdn93XdWi//7MDS";
      script.crossOrigin = "anonymous";
      script.onload = generateKey;
      document.body.appendChild(script);
    }

    $("#activate-2fa-modal").modal({
      closable: false,
      onDeny: () => $("#enable-2fa-toggle").checkbox("set unchecked"),
      onApprove: function() {
        if (totp.validate({ token: $("#validate-2fa-box").val() }) === null) {
          alert("Wrong passcode");
          return false;
        }
        twoFactorToken = totp.secret.buffer;
      }
    }).modal("show");
  },
  onUnchecked: () => key = null
});

$("#account-registration-checkbox").change(function() {
  var config = window.serverConfig;
  config.setAllowAccountRegistration(this.checked);
  getSocket().sendServerConfigModifications(config);
});

$("#user-vms-enabled-checkbox").change(function() {
  var config = window.serverConfig;
  config.setUserVmsEnabled(this.checked);
  getSocket().sendServerConfigModifications(config);
});

$("#ban-ip-cmd-box").change(function() {
  const config = window.serverConfig;
  config.setBanIpCommand(this.value);
  getSocket().sendServerConfigModifications(config);
});

$("#unban-ip-cmd-box").change(function() {
  const config = window.serverConfig;
  config.setUnbanIpCommand(this.value);
  getSocket().sendServerConfigModifications(config);
});

$("#user-invite-create-button").click(() => {
  getSocket().createUserInvite(
    {
      id: "",
      inviteName: $("#user-invite-description-box").val(),
      username: $("#user-invite-username-box").val(),
      admin: $("#user-invite-admin-checkbox").prop("checked")
    });
});

$("#new-vm-button").click(() => getSocket().sendCreateVmRequest(createObject("VmSettings")));

function showLoginForm() {
  $("#loading").hide();
  $("#edit-account").hide();
  $("#view-vms").hide();
  $("#linked-servers").hide();
  $("#server-config").hide();
  $("#login-register-container").show();
  $("#register-form").hide();
  $("#linked-servers").hide();
  $("#login-form").show();

  if (RECAPTCHA_ENABLED) {
    $("#login-button").hide();
    grecaptcha.render(
      $("<div>").appendTo($("#captcha").empty())[0],
      {
        sitekey: RECAPTCHA_SITE_KEY,
        callback: token => {
          $("#login-status").text("");
          getSocket().sendLoginRequest($("#username-box").val(), $("#password-box").val(), token);
          showLoading();
        }
      });
  } else {
    $("#login-button").off("click").click(function() {
      $("#login-status").text("");
      getSocket().sendLoginRequest($("#username-box").val(), $("#password-box").val(), "");
      $(this).addClass("loading");
    });
  }
}

function showRegisterForm() {
  $("#loading").hide();
  $("#edit-account").hide();
  $("#view-vms").hide();
  $("#linked-servers").hide();
  $("#server-config").hide();
  $("#login-register-container").show();
  $("#register-form").show();
  $("#linked-servers").hide();
  $("#login-form").hide();
}

function showEditAccount() {
  $("#loading").hide();
  $("#login-register-container").hide();
  $("#edit-account").show();
  $("#view-vms").hide();
  $("#linked-servers").hide();
  $("#server-config").hide();
}

function showVms() {
  $("#loading").hide();
  $("#login-register-container").hide();
  $("#edit-account").hide();
  $("#view-vms").show();
  $("#linked-servers").hide();
  $("#server-config").hide();
}

$("#vm-settings .ui.form .close.button").click(() => $("#vm-settings").hide("slow"));

function showLinkedServers() {
  $("#loading").hide();
  $("#login-register-container").hide();
  $("#edit-account").hide();
  $("#view-vms").hide();
  $("#linked-servers").show();
  $("#server-config").hide();
}

function showLoading() {
  $("#loading").show();
  $("#login-register-container").hide();
  $("#edit-account").hide();
  $("#view-vms").hide();
  $("#linked-servers").hide();
  $("#server-config").hide();
}

function showServerConfig(config) {
  $("#loading").hide();
  $("#login-register-container").hide();
  $("#edit-account").hide();
  $("#view-vms").show();
  $("#linked-servers").hide();
  //$("#server-config").show();

  window.serverConfig = config.clone();
  $("#account-registration-checkbox").prop("checked", config.getAllowAccountRegistration());
  $("#user-vms-enabled-checkbox").prop("checked", config.getUserVmsEnabled());
  $("#ban-ip-cmd-box").val(config.getBanIpCommand());
  $("#unban-ip-cmd-box").val(config.getUnbanIpCommand());

  $("#max-connections-enabled-checkbox").prop("checked", config.getMaxConnectionsEnabled()).off("change").change(function() {
    const config = window.serverConfig;
    config.setMaxConnectionsEnabled(this.checked);
    getSocket().sendServerConfigModifications(config);
  });
  $("#max-connections-box").val(config.getMaxConnections()).off("change").change(function() {
    const config = window.serverConfig;
    config.setMaxConnections(+this.value);
    getSocket().sendServerConfigModifications(config);
  });

  const captcha = config.getCaptcha();
  $("#captchas-enabled-checkbox").prop("checked", captcha.enabled).off("change").change(function() {
    const config = window.serverConfig;
    config.setCaptcha(Object.assign(config.getCaptcha(), {enabled: this.checked}));
    getSocket().sendServerConfigModifications(config);
  });
  $("#captcha-verification-box").val((captcha.https ? "https://" : "http://") + captcha.urlHost + (captcha.urlPort === 443 && captcha.https || captcha.urlPort === 80 && !captcha.https ? "" : ":" + captcha.urlPort) + captcha.urlPath).off("change").change(function() {
    const config = window.serverConfig;
    const url = new URL(this.value);
    const https = url.protocol === "https:";
    config.setCaptcha(Object.assign(config.getCaptcha(), {
        https: https,
        urlHost: url.hostname,
        urlPort: url.port ? +url.port : [80, 443][+https],
        urlPath: url.pathname + url.search + url.hash
      }));
    getSocket().sendServerConfigModifications(config);
  });
  $("#captcha-post-parameters-box").val(captcha.postParams).off("change").change(function() {
    const config = window.serverConfig;
    config.setCaptcha(Object.assign(config.getCaptcha(), {postParams: this.value}));
    getSocket().sendServerConfigModifications(config);
  });
  $("#captcha-valid-json-variable-box").val(captcha.validJSONVariableName).off("change").change(function() {
    const config = window.serverConfig;
    config.setCaptcha(Object.assign(config.getCaptcha(), {validJSONVariableName: this.value}));
    getSocket().sendServerConfigModifications(config);
  });

  $("#require-captcha-checkbox").prop("checked", config.getCaptchaRequired()).off("change").change(function() {
    const config = window.serverConfig;
    config.setCaptchaRequired(this.checked);
    getSocket().sendServerConfigModifications(config);
  });
}
//showServerConfig();
function loadServerConfig() {
  showLoading();
  $("#loading").hide();
  //socket.getServerConfigRequest();
}
//loadServerConfig();
//showRegisterForm();
//showServerConfig();
//showVms();
