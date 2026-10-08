#pragma once

#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>

// Separate port keeps the existing configuration server on port 80 unchanged.
WebServer firmwareServer(81);
Preferences firmwarePreferences;
String firmwareStaSsid;
String firmwareStaPassword;
String firmwareUploadError;
bool firmwareUploadStarted = false;
bool firmwareUploadFinished = false;

const char FIRMWARE_UPDATE_PAGE[] PROGMEM = R"rawliteral(
<!doctype html><html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Atualização do caminhão RC</title>
<style>body{font:16px Arial,sans-serif;max-width:620px;margin:24px auto;padding:0 16px;color:#222}section{border:1px solid #bbb;border-radius:10px;padding:18px;margin:18px 0}input,button{box-sizing:border-box;width:100%;padding:12px;margin:7px 0;font-size:16px}button{cursor:pointer;background:#1769aa;color:white;border:0;border-radius:6px}progress{width:100%;height:22px}small{color:#555}</style></head><body>
<h1>Atualização do caminhão RC</h1>
<p id="status">Ponto de acesso ativo. Conecte ao roteador para usar o Wi-Fi STA, se desejar.</p>
<section><h2>Conectar ao Wi-Fi do roteador</h2><form id="wifiForm"><label>Nome da rede Wi-Fi (2,4 GHz)<input name="ssid" maxlength="32" required></label><label>Senha<input name="password" type="password" maxlength="64"></label><button>Salvar e conectar</button></form><small>A conexão STA vale até reiniciar o controlador. O ponto de acesso My_Truck permanece ativo.</small></section>
<section><h2>Enviar firmware</h2><p>Selecione o arquivo .bin compilado para esta placa.</p><form id="updateForm"><input type="file" name="firmware" accept=".bin,application/octet-stream" required><button>Enviar e instalar firmware</button></form><progress id="progress" value="0" max="100"></progress><p id="result"></p></section>
<script>
const statusText=document.getElementById('status');
async function refreshStatus(){try{const r=await fetch('/status');const s=await r.json();statusText.textContent=s.connected?'Wi-Fi STA conectado. Endereço no roteador: '+s.ip:'Ponto de acesso My_Truck ativo em 192.168.4.1. Wi-Fi STA desconectado.';}catch(e){}}
refreshStatus();setInterval(refreshStatus,3000);
document.getElementById('wifiForm').addEventListener('submit',async e=>{e.preventDefault();const r=await fetch('/connect',{method:'POST',body:new URLSearchParams(new FormData(e.target))});document.getElementById('result').textContent=await r.text();});
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
                      ",\"ip\":\"" + ip + "\"}";
    firmwareServer.send(200, "application/json", response);
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
    firmwarePreferences.putString("ssid", firmwareStaSsid);
    firmwarePreferences.putString("password", firmwareStaPassword);
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(firmwareStaSsid.c_str(), firmwareStaPassword.c_str());
    firmwareServer.send(200, "text/plain; charset=utf-8", "Dados salvos. Tentando conectar; o endereço aparecerá aqui quando a conexão for concluída.");
  });

  firmwareServer.on("/update", HTTP_POST, []() {
    if (firmwareUploadError.length() > 0 || !firmwareUploadStarted || !firmwareUploadFinished)
    {
      if (firmwareUploadError.length() == 0)
        firmwareUploadError = "Envio incompleto ou inválido.";
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
      firmwareUploadStarted = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
      if (!firmwareUploadStarted)
        firmwareUploadError = Update.errorString();
    }
    else if (upload.status == UPLOAD_FILE_WRITE && firmwareUploadStarted && firmwareUploadError.length() == 0)
    {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
        firmwareUploadError = Update.errorString();
    }
    else if (upload.status == UPLOAD_FILE_END && firmwareUploadStarted && firmwareUploadError.length() == 0)
    {
      firmwareUploadFinished = Update.end(true);
      if (!firmwareUploadFinished)
        firmwareUploadError = Update.errorString();
    }
    else if (upload.status == UPLOAD_FILE_ABORTED)
    {
      Update.abort();
      firmwareUploadError = "Envio cancelado.";
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
