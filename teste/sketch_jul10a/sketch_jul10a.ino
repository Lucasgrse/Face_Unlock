#include "esp_camera.h"
#include <WiFi.h>

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

const char* ssid = "Perdidao";
const char* password = "nemtefalo";
const char* server_ip = "192.168.2.108";
const int server_port = 8081;

// Pinos de hardware
#define RELAY_PIN 4
#define BUZZER_PIN 2

boolean matchFace = false;
boolean activeRelay = false;
unsigned long prevMillis = 0;
const int interval = 5000; // tempo que o relé permanece ligado

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- Inicializando ---");

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  // Configuração da câmera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Erro ao iniciar câmera: 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_QVGA);

  // Conexão WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado! IP: " + WiFi.localIP().toString());
}

void recognizeFace() {
  Serial.println("\nCapturando imagem...");
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Falha na captura da imagem.");
    return;
  }

  WiFiClient client;
  if (!client.connect(server_ip, server_port)) {
    Serial.println("Falha na conexão com o servidor");
    esp_camera_fb_return(fb);
    return;
  }

  Serial.println("Enviando imagem...");
  client.write(fb->buf, fb->len);

  Serial.println("Aguardando resposta...");
  unsigned long start = millis();
  while (!client.available() && millis() - start < 10000) { // <-- agora 10 segundos
    delay(10);
  }

  if (client.available()) {
    String response = client.readString();
    response.trim();
    if (response == "1") {
      Serial.println("ACESSO PERMITIDO!");
      matchFace = true;
    } else {
      Serial.println("ACESSO NEGADO!");
      matchFace = false;
    }
  } else {
    Serial.println("Timeout aguardando resposta.");
  }

  client.stop();
  esp_camera_fb_return(fb);
}

void loop() {
  if (!activeRelay) {
    recognizeFace();
  }

  if (matchFace && !activeRelay) {
    Serial.println("Liberando fechadura...");
    activeRelay = true;
    digitalWrite(RELAY_PIN, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(800);
    digitalWrite(BUZZER_PIN, LOW);
    prevMillis = millis();
    matchFace = false;
  }

  if (activeRelay && millis() - prevMillis > interval) {
    Serial.println("Fechando fechadura.");
    activeRelay = false;
    matchFace = false;
    digitalWrite(RELAY_PIN, LOW);
  }

  delay(100);
}
