/*
 * Title:  ESP8266 Conveyor Counter Using Webserver
 * Author: Modified for Multi-Channel Backend (Node.js, ThingSpeak)
*/

#include <Arduino.h>
#ifdef ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>

// --- KONFIGURASI JARINGAN & BACKEND ---
// Ganti sesuai WiFi Anda
const char *ssid = "Ridho Wifi 4G";
const char *password = "20122016";

// Node.js Backend (MySQL Middleware)
// >>> PENTING: GANTI DENGAN IP LOKAL KOMPUTER ANDA!!! <<<
const char* backendHost = "192.168.18.8"; //
const int backendPort = 8080;

// ThingSpeak Configuration
const char* tsServerName = "api.thingspeak.com";
String tsApiKey = "DOGPVA4WIGD2J634"; // GANTI DENGAN API KEY THINGSPEAK ANDA
// ----------------------------------------

// // --- KONFIGURASI TELEGRAM ---
// // ⚠️ GANTI DENGAN TOKEN BOT ANDA ⚠️
// #define TELEGRAM_BOT_TOKEN "8545967610:AAGiTS0wAgkRt12PlqC7UlkLsAuGTeKopAU"
// // ⚠️ GANTI DENGAN ID CHAT ANDA ⚠️
// #define TELEGRAM_CHAT_ID "7460591519"
// const int TELEGRAM_THRESHOLD = 50; // Notifikasi setiap kelipatan 50
// // ----------------------------
// const char TELEGRAM_FINGERPRINT[] PROGMEM = "A2 0C 47 C4 72 3E D4 C9 6E 71 88 56 16 B6 B5 48 B8 53 C1 75"; 
// // ------------------------------------




// Webserver and Websockets setup
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Klien untuk ThingSpeak
WiFiClient espClient;

// Timer untuk ThingSpeak
unsigned long lastThingSpeakTime = 0;
const long THING_SPEAK_INTERVAL = 30000; // Kirim setiap 30 detik

// Pin Configuration
int IRSensor = D2;
int LED = LED_BUILTIN;
int ResetButton = D3; 

String localIPAddress = "";

// --- VARIABEL GLOBAL (HARUS ADA DI LUAR FUNGSI) ---
int counter = 0; 
bool isTriggered = false;
long lastTriggered = 0;
const long WAIT_FOR_NEXT_TRIGGER = 1000; // Debounce sensor IR
// --------------------------------------------------

// // --- FUNGSI TELEGRAM: MENGIRIM PESAN ---
// // --- FUNGSI TELEGRAM: MENGIRIM PESAN (PERBAIKAN FINAL) ---
// void sendTelegramMessage(const String& message) {
//     if (WiFi.status() != WL_CONNECTED) {
//         Serial.println("Telegram: WiFi tidak terhubung.");
//         return;
//     }
    
//     // URL Encoding (mengganti spasi dengan %20)
//     String encodedMessage = "";
//     for (int i = 0; i < message.length(); i++) {
//         char c = message.charAt(i);
//         if (c == ' ') {
//             encodedMessage += "%20";
//         } else {
//             encodedMessage += c;
//         }
//     }
    
//     const char* telegramHost = "api.telegram.org";
//     WiFiClientSecure telClient;

//     telClient.setFingerprint(TELEGRAM_FINGERPRINT); 

//     if (telClient.connect(telegramHost, 443)) { 
//         Serial.println("Telegram: Koneksi API OK via HTTPS. Mengirim pesan...");
        
//         // --- MEMBANGUN STRING REQUEST HTTP GET ---
//         String request = "GET /bot";
//         request += TELEGRAM_BOT_TOKEN;
//         request += "/sendMessage?chat_id=";
//         request += TELEGRAM_CHAT_ID;
//         request += "&text=";
//         request += encodedMessage;
//         request += " HTTP/1.1";

//         // Mengirim request dan headers
//         telClient.println(request); // Kirim baris permintaan GET LENGKAP
//         telClient.println("Host: api.telegram.org");
//         telClient.println("Connection: close");
//         telClient.println(); // BARIS KOSONG WAJIB: Menandai akhir header

//         // --- PEMBACAAN RESPON ---
//         unsigned long timeout = millis();
//         String response = "";
        
//         // Tunggu respons atau timeout
//         while (telClient.available() == 0) {
//             if (millis() - timeout > 5000) {
//                 Serial.println(">>> Telegram Server Timeout!");
//                 telClient.stop();
//                 return;
//             }
//         }
        
//         // Baca semua respons dari server
//         while(telClient.available()){
//             response += telClient.readStringUntil('\n');
//         }
//         telClient.stop();
        
//         // Print hasil
//         Serial.print("Telegram Response:\n");
//         Serial.println(response);
//         Serial.println("\n---");

//         // Analisis status (untuk debugging)
//         if (response.startsWith("HTTP/1.1 200 OK")) {
//              Serial.println("✅ Notifikasi Telegram BERHASIL dikirim!");
//         } else {
//              Serial.println("❌ Notifikasi Telegram GAGAL. Cek Token/ID Chat atau Log Respons.");
//         }
//         // -------------------------

//     } else {
//         Serial.println("Koneksi ke Telegram API gagal! (Cek Fingerprint/Koneksi/Firewall)");
//     }
// }
// -----------------------------------------------------
// ---------------------------------------

void notFound(AsyncWebServerRequest *request)
{
    request->send(404, "text/plain", "Not found");
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    // Memperbaiki format os_printf agar tidak terjadi warning kompilasi
    if (type == WS_EVT_CONNECT) { os_printf("ws[%s][%u] connect\n", server->url(), client->id()); client->ping(); }
    else if (type == WS_EVT_DISCONNECT) { os_printf("ws[%s][%u] disconnect: %u\n", server->url(), client->id(), *((uint16_t *)arg)); } // FIXED CASTING ERROR
    else if (type == WS_EVT_ERROR) { os_printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t *)arg), (char *)data); }
    else if (type == WS_EVT_PONG) { os_printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len) ? (char *)data : ""); }
    else if (type == WS_EVT_DATA) { os_printf("ws[%s][%u] data received\n", server->url(), client->id()); }
}

String indexPageProcessor(const String &var)
{
    return localIPAddress;
}


// --- FUNGSI MENGIRIM DATA KE SERVER NODE.JS ---
void sendBatchToDatabase(int count, const String& itemType) {
  // --- PING SERVER ---
    Serial.println("Pinging server...");

    WiFiClient pingClient;
    if (pingClient.connect(backendHost, backendPort)) {
        pingClient.println("GET /ping HTTP/1.1");
        pingClient.println("Host: " + String(backendHost));
        pingClient.println("Connection: close");
        pingClient.println();
    } else {
        Serial.println("Ping failed.");
    }

    pingClient.stop();   
    
    WiFiClient client;
    if (client.connect(backendHost, backendPort)) { 
        String postData = "count=" + String(count) + "&item=" + itemType; 
        
        client.println("POST /api/save_batch HTTP/1.1"); 
        client.println("Host: " + String(backendHost) + ":" + String(backendPort)); 
        client.println("Connection: close");
        client.println("Content-Type: application/x-www-form-urlencoded");
        client.print("Content-Length: ");
        client.println(postData.length());
        client.println(); 
        
        client.print(postData);
        
        // Membaca respon dari server Node.js 
        unsigned long timeout = millis();
        while (client.available() == 0) {
          if (millis() - timeout > 5000) { 
            Serial.println(">>> Node.js Server Timeout!");
            client.stop();
            return;
          }
        }
        
        Serial.printf("Batch sent to Node.js: %s (%u items). Response:\n", itemType.c_str(), count);
        while(client.available()){
          String line = client.readStringUntil('\n');
          Serial.print(line);
            }
            client.stop();
        } else {
            Serial.println("Connection to Node.js backend failed! Check IP/Port/Firewall.");
        }
    
}


// -----------------------------------------------------

void setup()
{

    Serial.begin(115200);

    if (!LittleFS.begin())
    {
        Serial.println("Cannot load LittleFS Filesystem!");
        return;
    }

    // Connect to WIFI
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
        Serial.printf("WiFi Failed! Trying again...\n");
        WiFi.begin(ssid, password);
        delay(5000);
    }
    localIPAddress = WiFi.localIP().toString();
    Serial.print("IP Address: ");
    Serial.println(localIPAddress);

    // attach AsyncWebSocket
    ws.onEvent(onEvent);
    server.addHandler(&ws);

    // Setup Webserver Routes
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
    { request->send(LittleFS, "/index.html", String(), false, indexPageProcessor); });

    // server.on("/index.css", HTTP_GET, [](AsyncWebServerRequest *request)
    // { request->send(LittleFS, "/index.css", "text/css"); });

    // server.on("/entireframework.min.css", HTTP_GET, [](AsyncWebServerRequest *request)
    // { request->send(LittleFS, "/entireframework.min.css", "text/css"); });


    // --- ROUTE: SAVE BATCH & RESET (Dipanggil dari Web) ---
    server.on("/save_batch", HTTP_GET, [](AsyncWebServerRequest *request)
    { 
        String itemType = "UNKNOWN"; 
        if (request->hasParam("item_type")) {
            itemType = request->getParam("item_type")->value();
            itemType.replace("_", " "); 
        }
        
        int countToSave = counter; 
        
        // Kirim data ke Node.js Backend
        sendBatchToDatabase(countToSave, itemType); 
        
        // Reset counter
        counter = 0; 

        // // --- LOGIKA TELEGRAM: RESET KONFIRMASI ---
        // String resetMsg = "✅ BATCH TERSIMPAN: " + String(countToSave) + 
        //                   " item (" + itemType + ") telah disimpan ke DB dan penghitung di-reset.";
        // sendTelegramMessage(resetMsg);
        // // -----------------------------------------
        
        // Update Web Clients via WebSocket
        ws.printfAll("0"); 
        
        request->send(200, "text/plain", "Batch saved: " + String(countToSave) + " items of type " + itemType + " and counter reset.");
    });

    server.onNotFound(notFound);

    server.begin();

    pinMode(IRSensor, INPUT); 
    pinMode(LED, OUTPUT); 
    pinMode(ResetButton, INPUT_PULLUP); 
}


void loop()
{
    // Logika MQTT DIHAPUS

    int sensorStatus = digitalRead(IRSensor);
    if (sensorStatus == 1) // Tidak Ada Objek
    {
        digitalWrite(LED, HIGH);
        isTriggered = false;
    }
    else // Objek Terdeteksi
    {
        if (!isTriggered)
        {
            long timeElapsed = millis() - lastTriggered;
            Serial.printf("timeElapsed :: %lu\n", timeElapsed); 
            if (timeElapsed < WAIT_FOR_NEXT_TRIGGER)
            {
                return;
            }

            isTriggered = true;
            counter++;
            digitalWrite(LED, LOW);

            // // --- LOGIKA TELEGRAM: KELIPATAN 50 ---
            // // Cek jika counter mencapai kelipatan yang ditentukan (mis. 50, 100, 150, dst.)
            // if (counter > 0 && (counter % TELEGRAM_THRESHOLD) == 0) {
            //     String msg = "🎉 BATAS KELIPATAN TERCAPAI: Hitungan saat ini mencapai " + String(counter) + "!";
            //     sendTelegramMessage(msg);
            // }
            // ------------------------------------
            
            // --- UPDATE REAL-TIME ---
            Serial.printf("counter :: %u\n", counter);
            ws.printfAll("%u", counter); // Kirim ke Web UI (WebSocket)
            
            lastTriggered = millis();
        }
    }

    // --- PENGIRIMAN DATA KE THINGSPEAK BERKALA (TIMER) ---
    if (millis() - lastThingSpeakTime >= THING_SPEAK_INTERVAL) {
        WiFiClient tsClient;
        if (tsClient.connect(tsServerName, 80)) {
            String postStr = "api_key=" + tsApiKey + "&field1=" + String(counter);
            tsClient.println("POST /update HTTP/1.1");
            tsClient.println("Host: api.thingspeak.com");
            tsClient.println("Connection: close");
            tsClient.println("Content-Type: application/x-www-form-urlencoded");
            tsClient.print("Content-Length: ");
            tsClient.println(postStr.length());
            tsClient.println();
            tsClient.print(postStr);
            Serial.println("Data sent to ThingSpeak: " + String(counter));
            tsClient.stop();
        } else {
            Serial.println("Connection to ThingSpeak failed!");
        }
        lastThingSpeakTime = millis();
    }
    
    // cleanup websocket clients
    ws.cleanupClients();
}