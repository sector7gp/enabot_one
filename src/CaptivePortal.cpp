#include "CaptivePortal.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <string.h>
#include "config.h"
#include "AudioPlayer.h"

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);

static DNSServer dnsServer;
static WebServer server(80);

static File uploadFile;
static bool uploadOk = false;
static size_t uploadBytes = 0;

static String buildStatusHtml() {
    if (!LittleFS.exists(AUDIO_FILE_PATH)) {
        return "<p>No hay ningun audio cargado todavia.</p>";
    }
    File f = LittleFS.open(AUDIO_FILE_PATH, FILE_READ);
    WavInfo info;
    String out;
    if (f && parseWavHeader(f, info)) {
        float seconds = (float)info.dataSize / (info.sampleRate * (info.bitsPerSample / 8));
        out = "<p>Audio actual: " + String(f.size() / 1024) + " KB, " +
              String(info.sampleRate) + " Hz, " + String(info.bitsPerSample) +
              " bit, " + String(seconds, 1) + " s.</p>"
              // cache-busting con millis() para no escuchar una version vieja
              // cacheada por el navegador despues de subir un archivo nuevo.
              "<audio controls src='/audio.wav?t=" + String(millis()) + "'></audio>";
    } else {
        out = "<p>Hay un archivo pero no es un WAV valido; subi uno nuevo.</p>";
    }
    if (f) f.close();
    return out;
}

// Todo el trabajo de conversion pasa en el navegador: decodeAudioData()
// entiende m4a/aac/mp3/wav nativamente (es el mismo codec que usa <audio>),
// y OfflineAudioContext hace el resample + downmix a mono + recorte a
// AUDIO_CLIENT_MAX_SECONDS en un solo paso. De ahi se arma un WAV PCM a
// mano y se sube igual que antes por /upload. El ESP32 nunca decodifica
// nada: solo recibe un WAV que ya cumple lo que parseWavHeader() espera.
static const char PAGE_SCRIPT[] =
    "<script>"
    "const TARGET_RATE=" AUDIO_STR(AUDIO_CLIENT_SAMPLE_RATE) ";"
    "const MAX_SECONDS=" AUDIO_STR(AUDIO_CLIENT_MAX_SECONDS) ";"
    "function writeStr(v,o,s){for(let i=0;i<s.length;i++)v.setUint8(o+i,s.charCodeAt(i));}"
    "function encodeWav(pcm,rate){"
      "const dataSize=pcm.length*2;"
      "const buf=new ArrayBuffer(44+dataSize);"
      "const v=new DataView(buf);"
      "writeStr(v,0,'RIFF');v.setUint32(4,36+dataSize,true);writeStr(v,8,'WAVE');"
      "writeStr(v,12,'fmt ');v.setUint32(16,16,true);v.setUint16(20,1,true);"
      "v.setUint16(22,1,true);v.setUint32(24,rate,true);v.setUint32(28,rate*2,true);"
      "v.setUint16(32,2,true);v.setUint16(34,16,true);writeStr(v,36,'data');"
      "v.setUint32(40,dataSize,true);"
      "let o=44;for(let i=0;i<pcm.length;i++,o+=2)v.setInt16(o,pcm[i],true);"
      "return new Blob([buf],{type:'audio/wav'});"
    "}"
    "async function convertToWav(file){"
      "const arrayBuffer=await file.arrayBuffer();"
      "const AudioCtx=window.AudioContext||window.webkitAudioContext;"
      "const tempCtx=new AudioCtx();"
      "const decoded=await tempCtx.decodeAudioData(arrayBuffer);"
      "await tempCtx.close();"
      "const seconds=Math.min(decoded.duration,MAX_SECONDS);"
      "const length=Math.max(1,Math.ceil(seconds*TARGET_RATE));"
      "const offlineCtx=new OfflineAudioContext(1,length,TARGET_RATE);"
      "const src=offlineCtx.createBufferSource();"
      "src.buffer=decoded;src.connect(offlineCtx.destination);src.start(0);"
      "const rendered=await offlineCtx.startRendering();"
      "const samples=rendered.getChannelData(0);"
      "const pcm=new Int16Array(samples.length);"
      "for(let i=0;i<samples.length;i++){"
        "const s=Math.max(-1,Math.min(1,samples[i]));"
        "pcm[i]=s<0?s*32768:s*32767;"
      "}"
      "return encodeWav(pcm,TARGET_RATE);"
    "}"
    "document.getElementById('audioForm').addEventListener('submit',async function(e){"
      "e.preventDefault();"
      "const input=document.getElementById('audioFile');"
      "const status=document.getElementById('uploadStatus');"
      "if(!input.files.length)return;"
      "try{"
        "status.textContent='Convirtiendo...';"
        "const wav=await convertToWav(input.files[0]);"
        "status.textContent='Subiendo...';"
        "const fd=new FormData();"
        "fd.append('audio',wav,'audio.wav');"
        "const resp=await fetch('/upload',{method:'POST',body:fd});"
        "const text=await resp.text();"
        "status.innerHTML=text;"
      "}catch(err){"
        "status.textContent='Error al convertir/subir: '+err.message;"
      "}"
    "});"
    "</script>";

static String buildIndexHtml() {
    String html =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>EnaBot Audio Setup</title>"
        "<style>body{font-family:sans-serif;max-width:420px;margin:32px auto;padding:0 16px}"
        "input[type=submit]{padding:8px 16px}</style></head><body>"
        "<h2>EnaBot &mdash; Subir audio</h2>"
        "<p>Elegí una grabacion de voz del celular (m4a, mp3, wav, lo que sea) de "
        "hasta " + String(AUDIO_CLIENT_MAX_SECONDS) + " segundos. Se convierte sola a mono "
        + String(AUDIO_CLIENT_SAMPLE_RATE) + " Hz en el navegador antes de subirla.</p>"
        "<form id='audioForm' enctype='multipart/form-data'>"
        // accept amplio a proposito: en varios celulares un .m4a (Voice
        // Memos de iPhone, etc.) no aparece seleccionable si el filtro es
        // solo 'audio/*', porque el picker lo clasifica como MIME
        // audio/mp4 o incluso video/mp4 (comparte contenedor con MP4).
        "<input id='audioFile' type='file' name='audio' "
        "accept='audio/*,video/mp4,audio/mp4,audio/x-m4a,.m4a,.mp3,.wav,.aac,.ogg,.caf' "
        "required><br><br>"
        "<input type='submit' value='Convertir y subir'></form>"
        "<p id='uploadStatus'></p>";
    html += buildStatusHtml();
    html += PAGE_SCRIPT;
    html += "</body></html>";
    return html;
}

static void handleRoot() {
    server.send(200, "text/html", buildIndexHtml());
}

static void handleUploadResult() {
    if (uploadOk) {
        server.send(200, "text/html", "Listo, audio guardado. <a href='/'>Volver</a>");
    } else {
        server.send(400, "text/html",
                     "Error: la conversion no dio un WAV mono PCM valido "
                     "(o el archivo es demasiado grande). <a href='/'>Volver</a>");
    }
}

static void handleFileUpload() {
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        uploadBytes = 0;
        uploadOk = false;
        if (LittleFS.exists(AUDIO_FILE_PATH)) LittleFS.remove(AUDIO_FILE_PATH);
        uploadFile = LittleFS.open(AUDIO_FILE_PATH, FILE_WRITE);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) {
            uploadBytes += upload.currentSize;
            if (uploadBytes > AUDIO_MAX_BYTES) {
                uploadFile.close();
                LittleFS.remove(AUDIO_FILE_PATH);
                return; // la validacion final en UPLOAD_FILE_END lo marcara invalido
            }
            uploadFile.write(upload.buf, upload.currentSize);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) uploadFile.close();

        File check = LittleFS.open(AUDIO_FILE_PATH, FILE_READ);
        WavInfo info;
        uploadOk = check && parseWavHeader(check, info);
        if (check) check.close();
        if (!uploadOk && LittleFS.exists(AUDIO_FILE_PATH)) LittleFS.remove(AUDIO_FILE_PATH);
    }
}

static void handleServeAudio() {
    if (!LittleFS.exists(AUDIO_FILE_PATH)) {
        server.send(404, "text/plain", "No hay audio cargado");
        return;
    }
    File f = LittleFS.open(AUDIO_FILE_PATH, FILE_READ);
    server.streamFile(f, "audio/wav");
    f.close();
}

// Endpoints que distintos sistemas operativos usan para detectar si hay
// "internet real" detras del AP. Redirigir todo a "/" es lo que dispara el
// popup nativo de portal cautivo en iOS/Android/Windows.
static void handleNotFound() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}

void captivePortalBegin() {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, AP_SUBNET);
    WiFi.softAP(AP_SSID, (strlen(AP_PASSWORD) > 0) ? AP_PASSWORD : nullptr);

    dnsServer.start(53, "*", AP_IP);

    server.on("/", HTTP_GET, handleRoot);
    server.on("/upload", HTTP_POST, handleUploadResult, handleFileUpload);
    server.on("/audio.wav", HTTP_GET, handleServeAudio);
    server.onNotFound(handleNotFound);
    server.begin();
}

void captivePortalLoop() {
    dnsServer.processNextRequest();
    server.handleClient();
}
