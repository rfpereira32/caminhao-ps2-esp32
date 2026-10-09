#pragma once

#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_ota_ops.h>

// Separate port keeps the existing configuration server on port 80 unchanged.
WebServer firmwareServer(81);
Preferences firmwarePreferences;
String firmwareStaSsid;
String firmwareStaPassword;
String firmwareUploadError;
bool firmwareUploadStarted = false;
bool firmwareUploadFinished = false;
bool firmwareAudioWasRunning = false;
bool firmwareRollbackMarkerCreated = false;
bool otaRollbackPending = false;
bool otaRollbackNativePending = false;
uint8_t otaRollbackBackupSubtype = 0;

void clearFirmwareRollbackKeys(Preferences &preferences)
{
  preferences.remove("ota_pending");
  preferences.remove("ota_backup");
  preferences.remove("ota_booted");
}

void inspectFirmwareRollbackState()
{
  const esp_partition_t *runningPartition = esp_ota_get_running_partition();
  esp_ota_img_states_t imageState;
  const esp_err_t stateResult = runningPartition != nullptr
                                    ? esp_ota_get_state_partition(runningPartition, &imageState)
                                    : ESP_ERR_NOT_FOUND;
  otaRollbackNativePending = stateResult == ESP_OK && imageState == ESP_OTA_IMG_PENDING_VERIFY;

  Preferences rollbackPreferences;
  if (!rollbackPreferences.begin("truck-wifi", false))
  {
    otaRollbackPending = otaRollbackNativePending;
    return;
  }

  const bool storedPending = rollbackPreferences.getBool("ota_pending", false);
  otaRollbackBackupSubtype = rollbackPreferences.getUChar("ota_backup", 0);
  bool restartForRollback = false;

  if (storedPending && runningPartition != nullptr && runningPartition->subtype == otaRollbackBackupSubtype)
  {
    // The previous image booted again, so the update failed or the bootloader rolled it back.
    clearFirmwareRollbackKeys(rollbackPreferences);
  }
  else if (storedPending && stateResult == ESP_OK && imageState == ESP_OTA_IMG_VALID)
  {
    // Native rollback was confirmed, but cleanup of the fallback marker was interrupted.
    clearFirmwareRollbackKeys(rollbackPreferences);
  }
  else if (storedPending)
  {
    otaRollbackPending = true;
    if (!otaRollbackNativePending)
    {
      if (rollbackPreferences.getBool("ota_booted", false))
      {
        const esp_partition_t *fallbackPartition = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP,
            static_cast<esp_partition_subtype_t>(otaRollbackBackupSubtype),
            nullptr);
        if (fallbackPartition != nullptr && esp_ota_set_boot_partition(fallbackPartition) == ESP_OK)
          restartForRollback = true;
      }
      else
        rollbackPreferences.putBool("ota_booted", true);
    }
  }
  else if (otaRollbackNativePending)
  {
    // Supports a candidate installed before the NVS fallback marker was introduced.
    otaRollbackPending = true;
    const esp_partition_t *fallbackPartition = esp_ota_get_next_update_partition(nullptr);
    if (fallbackPartition != nullptr)
      otaRollbackBackupSubtype = fallbackPartition->subtype;
  }
  else
    otaRollbackPending = false;

  rollbackPreferences.end();
  if (restartForRollback)
    ESP.restart();
}

bool saveFirmwareRollbackMarker()
{
  const esp_partition_t *runningPartition = esp_ota_get_running_partition();
  if (runningPartition == nullptr)
    return false;

  otaRollbackBackupSubtype = runningPartition->subtype;
  const bool saved = firmwarePreferences.putUChar("ota_backup", otaRollbackBackupSubtype) > 0 &&
                     firmwarePreferences.putBool("ota_booted", false) > 0 &&
                     firmwarePreferences.putBool("ota_pending", true) > 0;
  firmwareRollbackMarkerCreated = saved;
  if (!saved)
    clearFirmwareRollbackKeys(firmwarePreferences);
  return saved;
}

void clearFirmwareRollbackMarker()
{
  if (!firmwareRollbackMarkerCreated)
    return;

  const bool audioWasRunning = pauseAudioTimersForFirmwareUpdate();
  clearFirmwareRollbackKeys(firmwarePreferences);
  if (audioWasRunning)
    resumeAudioTimersAfterFirmwareUpdate();
  firmwareRollbackMarkerCreated = false;
  otaRollbackPending = false;
  otaRollbackNativePending = false;
}

void restoreFirmwareUpdateAudio()
{
  if (firmwareAudioWasRunning)
    resumeAudioTimersAfterFirmwareUpdate();
  firmwareAudioWasRunning = false;
}

const char FIRMWARE_UPDATE_PAGE[] PROGMEM = R"rawliteral(
<!doctype html><html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Atualização do caminhão RC</title>
<style>body{font:16px Arial,sans-serif;max-width:620px;margin:24px auto;padding:0 16px;color:#222}section{border:1px solid #bbb;border-radius:10px;padding:18px;margin:18px 0}input,button{box-sizing:border-box;width:100%;padding:12px;margin:7px 0;font-size:16px}button{cursor:pointer;background:#1769aa;color:white;border:0;border-radius:6px}progress{width:100%;height:22px}small{color:#555}</style></head><body>
<h1>Atualização do caminhão RC</h1>
<p id="status">Ponto de acesso ativo. Conecte ao roteador para usar o Wi-Fi STA, se desejar.</p>
<section><h2>Conectar ao Wi-Fi do roteador</h2><form id="wifiForm"><label>Nome da rede Wi-Fi (2,4 GHz)<input name="ssid" maxlength="32" required></label><label>Senha<input name="password" type="password" maxlength="64"></label><button>Salvar e conectar</button></form><small>A conexão STA vale até reiniciar o controlador. O ponto de acesso My_Truck permanece ativo.</small></section>
<section id="rollbackSection" hidden><h2>Teste o firmware novo</h2><p>Esta versão ainda está provisória. Se o ESP reiniciar antes da confirmação, voltará à versão anterior. Depois de testar o caminhão, confirme o funcionamento ou reverta agora.</p><button id="confirmFirmware" type="button">Confirmar funcionamento</button><button id="rollbackFirmware" type="button">Reverter para a versão anterior</button></section>
<section><h2>Enviar firmware</h2><p>Selecione o arquivo .bin compilado para esta placa.</p><form id="updateForm"><input type="file" name="firmware" accept=".bin,application/octet-stream" required><button>Enviar e instalar firmware</button></form><progress id="progress" value="0" max="100"></progress><p id="result"></p></section>
<script>
const statusText=document.getElementById('status');
async function refreshStatus(){try{const r=await fetch('/status');const s=await r.json();statusText.textContent=s.connected?'Wi-Fi STA conectado. Endereço no roteador: '+s.ip:'Ponto de acesso My_Truck ativo em 192.168.4.1. Wi-Fi STA desconectado.';document.getElementById('rollbackSection').hidden=!s.rollbackPending;}catch(e){}}
refreshStatus();setInterval(refreshStatus,3000);
document.getElementById('wifiForm').addEventListener('submit',async e=>{e.preventDefault();const r=await fetch('/connect',{method:'POST',body:new URLSearchParams(new FormData(e.target))});document.getElementById('result').textContent=await r.text();});
async function firmwareAction(path){try{const r=await fetch(path,{method:'POST'});document.getElementById('result').textContent=await r.text();await refreshStatus();}catch(e){document.getElementById('result').textContent='O ESP está reiniciando para voltar à versão anterior.';}}
document.getElementById('confirmFirmware').addEventListener('click',()=>firmwareAction('/confirm'));
document.getElementById('rollbackFirmware').addEventListener('click',()=>firmwareAction('/rollback'));
document.getElementById('updateForm').addEventListener('submit',e=>{e.preventDefault();const form=e.target;const data=new FormData(form);const request=new XMLHttpRequest();request.open('POST','/update');request.upload.onprogress=event=>{if(event.lengthComputable)document.getElementById('progress').value=event.loaded*100/event.total;};request.onload=()=>{document.getElementById('result').textContent=request.responseText;};request.onerror=()=>{document.getElementById('result').textContent='Falha de comunicação durante o envio.';};document.getElementById('result').textContent='Enviando firmware…';request.send(data);});
</script></body></html>
)rawliteral";

void beginFirmwareUpdate()
{
  firmwarePreferences.begin("truck-wifi", false);
  firmwareStaSsid = firmwarePreferences.getString("ssid", "");
  firmwareStaPassword = firmwarePreferences.getString("password", "");

  firmwareServer.on("/", HTTP_GET, []() {
    firmwareServer.send_P(200, "text/html; charset=utf-8", FIRMWARE_UPDATE_PAGE);
  });

  firmwareServer.on("/status", HTTP_GET, []() {
    const bool connected = WiFi.status() == WL_CONNECTED;
    const String ip = connected ? WiFi.localIP().toString() : String("192.168.4.1");
    String response = String("{\"connected\":") + (connected ? "true" : "false") +
                      ",\"ip\":\"" + ip + "\",\"rollbackPending\":" +
                      (otaRollbackPending ? "true" : "false") + "}";
    firmwareServer.send(200, "application/json", response);
  });

  firmwareServer.on("/confirm", HTTP_POST, []() {
    if (!otaRollbackPending)
    {
      firmwareServer.send(409, "text/plain; charset=utf-8", "Não há firmware aguardando confirmação.");
      return;
    }

    const bool audioWasRunning = pauseAudioTimersForFirmwareUpdate();
    const esp_err_t result = otaRollbackNativePending
                                 ? esp_ota_mark_app_valid_cancel_rollback()
                                 : ESP_OK;
    if (result == ESP_OK)
      clearFirmwareRollbackKeys(firmwarePreferences);
    if (audioWasRunning)
      resumeAudioTimersAfterFirmwareUpdate();

    if (result == ESP_OK)
    {
      otaRollbackPending = false;
      otaRollbackNativePending = false;
      firmwareServer.send(200, "text/plain; charset=utf-8", "Firmware confirmado. A versão anterior foi liberada para futuras atualizações.");
    }
    else
      firmwareServer.send(500, "text/plain; charset=utf-8", String("Não foi possível confirmar o firmware: ") + esp_err_to_name(result));
  });

  firmwareServer.on("/rollback", HTTP_POST, []() {
    if (!otaRollbackPending)
    {
      firmwareServer.send(409, "text/plain; charset=utf-8", "Não há firmware provisório para reverter.");
      return;
    }

    const esp_partition_t *fallbackPartition = otaRollbackBackupSubtype != 0
                                                   ? esp_partition_find_first(
                                                         ESP_PARTITION_TYPE_APP,
                                                         static_cast<esp_partition_subtype_t>(otaRollbackBackupSubtype),
                                                         nullptr)
                                                   : esp_ota_get_next_update_partition(nullptr);
    if (fallbackPartition == nullptr)
    {
      firmwareServer.send(500, "text/plain; charset=utf-8", "A partição anterior não foi encontrada.");
      return;
    }

    const bool audioWasRunning = pauseAudioTimersForFirmwareUpdate();
    const esp_err_t result = esp_ota_set_boot_partition(fallbackPartition);
    if (result == ESP_OK)
    {
      firmwareServer.send(200, "text/plain; charset=utf-8", "Revertendo para o firmware anterior e reiniciando.");
      delay(250);
      ESP.restart();
    }
    if (audioWasRunning)
      resumeAudioTimersAfterFirmwareUpdate();

    firmwareServer.send(500, "text/plain; charset=utf-8", String("Não foi possível reverter: ") + esp_err_to_name(result));
  });

  firmwareServer.on("/connect", HTTP_POST, []() {
    if (!firmwareServer.hasArg("ssid") || firmwareServer.arg("ssid").length() == 0 ||
        firmwareServer.arg("ssid").length() > 32 || firmwareServer.arg("password").length() > 64)
    {
      firmwareServer.send(400, "text/plain; charset=utf-8", "Informe uma rede Wi-Fi válida (2,4 GHz).");
      return;
    }

    firmwareStaSsid = firmwareServer.arg("ssid");
    firmwareStaPassword = firmwareServer.arg("password");
    const bool audioWasRunning = pauseAudioTimersForFirmwareUpdate();
    firmwarePreferences.putString("ssid", firmwareStaSsid);
    firmwarePreferences.putString("password", firmwareStaPassword);
    if (audioWasRunning)
      resumeAudioTimersAfterFirmwareUpdate();
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(firmwareStaSsid.c_str(), firmwareStaPassword.c_str());
    firmwareServer.send(200, "text/plain; charset=utf-8", "Dados salvos. Tentando conectar; o endereço aparecerá aqui quando a conexão for concluída.");
  });

  firmwareServer.on("/update", HTTP_POST, []() {
    if (firmwareUploadError.length() > 0 || !firmwareUploadStarted || !firmwareUploadFinished)
    {
      if (firmwareUploadError.length() == 0)
        firmwareUploadError = "Envio incompleto ou inválido.";
      clearFirmwareRollbackMarker();
      restoreFirmwareUpdateAudio();
      firmwareServer.send(500, "text/plain; charset=utf-8", "Falha ao instalar o firmware: " + firmwareUploadError);
      firmwareUploadError = "";
      firmwareUploadStarted = false;
      firmwareUploadFinished = false;
      return;
    }

    firmwareServer.send(200, "text/plain; charset=utf-8", "Firmware instalado. O controlador vai reiniciar agora.");
    delay(700);
    ESP.restart();
  }, []() {
    HTTPUpload &upload = firmwareServer.upload();
    if (upload.status == UPLOAD_FILE_START)
    {
      firmwareUploadError = "";
      firmwareUploadFinished = false;
      if (otaRollbackPending)
      {
        firmwareUploadStarted = false;
        firmwareUploadError = "Confirme ou reverta o firmware provisório antes de enviar outra versão.";
        return;
      }
      firmwareAudioWasRunning = pauseAudioTimersForFirmwareUpdate();
      if (!saveFirmwareRollbackMarker())
      {
        firmwareUploadStarted = false;
        firmwareUploadError = "Não foi possível preservar a partição de recuperação.";
        restoreFirmwareUpdateAudio();
        return;
      }
      firmwareUploadStarted = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
      if (!firmwareUploadStarted)
      {
        firmwareUploadError = Update.errorString();
        clearFirmwareRollbackMarker();
        restoreFirmwareUpdateAudio();
      }
    }
    else if (upload.status == UPLOAD_FILE_WRITE && firmwareUploadStarted && firmwareUploadError.length() == 0)
    {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
      {
        firmwareUploadError = Update.errorString();
        clearFirmwareRollbackMarker();
        restoreFirmwareUpdateAudio();
      }
    }
    else if (upload.status == UPLOAD_FILE_END && firmwareUploadStarted && firmwareUploadError.length() == 0)
    {
      firmwareUploadFinished = Update.end(true);
      if (!firmwareUploadFinished)
      {
        firmwareUploadError = Update.errorString();
        clearFirmwareRollbackMarker();
        restoreFirmwareUpdateAudio();
      }
    }
    else if (upload.status == UPLOAD_FILE_ABORTED)
    {
      Update.abort();
      firmwareUploadError = "Envio cancelado.";
      clearFirmwareRollbackMarker();
      restoreFirmwareUpdateAudio();
    }
  });

  firmwareServer.begin();
  Serial.println("Firmware update page: http://192.168.4.1:81/");
  Serial.println("Wi-Fi STA will only connect when requested from the update page.");
}

void handleFirmwareUpdate()
{
  firmwareServer.handleClient();
}
