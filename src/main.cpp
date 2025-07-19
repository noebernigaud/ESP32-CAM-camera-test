#include <esp32cam.h>
#include <WiFi.h>
#include <HTTPClient.h>

static const char* WIFI_SSID = "Livebox-B530";
static const char* WIFI_PASS = "YpTGM99n3nHhsAM9JA";
static const char* SERVER_HOST = "192.168.1.34";
static const int SERVER_PORT = 5000;
static const char* UPLOAD_URL = "http://192.168.1.34:5000/upload/picture";
static const char* PING_URL = "http://192.168.1.34:5000/ping";

bool testServerConnection() {
  HTTPClient http;
  Serial.println("Testing server connection...");
  
  http.begin(PING_URL);
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    Serial.println("Server is reachable!");
    http.end();
    return true;
  } else {
    Serial.printf("Server ping failed, error: %d\n", httpCode);
    http.end();
    return false;
  }
}

bool captureAndSendImage() {
  auto img = esp32cam::capture();
  if (img == nullptr) {
    Serial.println("Failed to capture image");
    return false;
  }

  Serial.printf("Image captured successfully! Size: %d bytes\n", img->size());

  String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
  String head = "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"picture\"; filename=\"esp32cam.jpg\"\r\n";
  head += "Content-Type: image/jpeg\r\n\r\n";

  String tail = "\r\n--" + boundary + "--\r\n";

  // Allocate buffer for the full body
  size_t bodyLen = head.length() + img->size() + tail.length();
  uint8_t *body = (uint8_t*)malloc(bodyLen);
  if (!body) {
    Serial.println("Failed to allocate memory for POST body");
    return false;
  }

  // Copy head, image, tail into buffer
  memcpy(body, head.c_str(), head.length());
  memcpy(body + head.length(), img->data(), img->size());
  memcpy(body + head.length() + img->size(), tail.c_str(), tail.length());

  HTTPClient http;
  http.begin(UPLOAD_URL);
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

  Serial.printf("Sending POST request, total size: %d bytes\n", bodyLen);
  int httpCode = http.POST(body, bodyLen);

  free(body);

  Serial.printf("HTTP Response code: %d\n", httpCode);
  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("Server response: " + response);
  }

  http.end();
  return (httpCode == 200);
}

bool captureAndSendVideo(int frameCount = 50, int intervalMs = 200) {
  WiFiClient client;
  if (!client.connect(SERVER_HOST, SERVER_PORT)) {
    Serial.println("Failed to connect to server for video upload");
    return false;
  }

  String boundary = "----ESP32VideoBoundary";
  String contentType = "multipart/form-data; boundary=" + boundary;

  // Compose HTTP POST header
  String request = "POST /upload/mjpeg HTTP/1.1\r\n";
  request += "Host: " + String(SERVER_HOST) + ":" + String(SERVER_PORT) + "\r\n";
  request += "Content-Type: " + contentType + "\r\n";
  request += "Connection: close\r\n";
  request += "Transfer-Encoding: chunked\r\n\r\n";
  client.print(request);

  Serial.printf("Free heap (RAM): %u bytes\n", ESP.getFreeHeap());

  for (int i = 0; i < frameCount; ++i) {
    unsigned long captureStart = millis();
    auto img = esp32cam::capture();   
    unsigned long t1 = millis();
    Serial.printf("Capture time: %lu ms\n", t1 - captureStart);
    Serial.printf("Free heap (RAM): %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Free PSRAM: %u bytes\n", ESP.getFreePsram());

    if (img == nullptr) {
      //Serial.printf("Failed to capture frame %d\n", i);
      continue;
    }
    Serial.printf("Captured frame %d, size: %d bytes\n", i, img->size());

    // Multipart part header for this image
    String partHead = "--" + boundary + "\r\n";
    partHead += "Content-Disposition: form-data; name=\"picture\"; filename=\"frame" + String(i) + ".jpg\"\r\n";
    partHead += "Content-Type: image/jpeg\r\n\r\n";

    // Send chunk: partHead
    size_t headLen = partHead.length();
    client.printf("%X\r\n", headLen);
    client.print(partHead);
    client.print("\r\n");

    // Send chunk: image data
    size_t imgLen = img->size();
    client.printf("%X\r\n", imgLen);
    client.write(img->data(), imgLen);
    client.print("\r\n");

    unsigned long captureEnd = millis();

    // Log time between captures
    static unsigned long lastCaptureTime = 0;
    if (lastCaptureTime != 0) {
      unsigned long elapsed = captureStart - lastCaptureTime;
      Serial.printf("Time for capture and send: %lu ms\n", elapsed);
      if (elapsed < intervalMs) {
        unsigned long waitTime = intervalMs - elapsed;
        Serial.printf("Waiting %lu ms to maintain interval\n", waitTime);
        delay(waitTime);
      }
    }
    lastCaptureTime = captureStart;
  }

  // Send final boundary
  String endBoundary = "--" + boundary + "--\r\n";
  client.printf("%X\r\n", endBoundary.length());
  client.print(endBoundary);
  client.print("\r\n");

  // Send zero-length chunk to end chunked transfer
  client.print("0\r\n\r\n");

  // Read server response
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line.length() == 0) break;
    Serial.println(line);
  }
  Serial.println("Video upload finished.");
  client.stop();
  return true;
}

void setup() {
  Serial.begin(9600);
  Serial.println("Démarrage ESP32-CAM !");

  // Camera initialization with error handling
  auto res = esp32cam::Resolution::find(1280, 720);
  esp32cam::Config cfg;
  cfg.setPins(esp32cam::pins::AiThinker)
     .setResolution(res)
     .setJpeg(70);

  bool ok = esp32cam::Camera.begin(cfg);
  if (!ok) {
    Serial.println("Camera initialization failed!");
    while (true) {
      delay(100);
    }
  }
  Serial.println("Camera initialization successful!");

  // WiFi connection with timeout
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setSleep(false);
  
  Serial.print("WiFi connecting");
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 30) {
    delay(500);
    Serial.print(".");
    timeout++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection failed!");
    while (true) {
      delay(100);
    }
  }

  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Test server connection first
  if (!testServerConnection()) {
    Serial.println("Cannot reach server, restarting...");
    delay(5000);
    ESP.restart();
  }
  
  // Only try to send image if server is reachable
  if (captureAndSendVideo()) {
    Serial.println("Image sent successfully!");
  } else {
    Serial.println("Failed to send image");
  }

  // Go to deep sleep
  Serial.println("Going to sleep...");
  esp_deep_sleep_start();
}

void loop() {
  // Nothing here as we're using deep sleep
}