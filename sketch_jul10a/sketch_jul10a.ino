#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>   // <-- NOVO
#include <HTTPClient.h>  // <-- NOVO
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/rtc_io.h"

void startCameraServer();


#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// --- Configurações de Rede ---
const char* ssid = "Perdidao";
const char* password = "nemtefalo";

const char* server_ip = "192.168.2.108";
const int server_port_socket = 8081;
const int server_port_http = 5000;

// --- Pinos ---
#define RELAY_PIN 4
#define BUZZER_PIN 2

// --- Variáveis de Estado ---
boolean matchFace = false;
boolean activeRelay = false;
unsigned long prevMillis = 0;
const int interval = 5000;

WebServer server(80); // <-- NOVO: Cria o objeto do servidor web na porta 80


void captureAndSendForRegistration() {
  Serial.println("Comando de registro recebido. Capturando imagem...");

  // Feedback para o usuário (opcional, mas recomendado)
  digitalWrite(BUZZER_PIN, HIGH);
  delay(150);
  digitalWrite(BUZZER_PIN, LOW);
  delay(150);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(150);
  digitalWrite(BUZZER_PIN, LOW);
  
  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Falha na captura da câmera para registro.");
    server.send(500, "text/plain", "Falha ao capturar imagem");
    return;
  }
  
  // Envia a resposta de sucesso para o Flask, confirmando que o comando foi recebido
  server.send(200, "text/plain", "Comando de captura recebido. Processando...");
  Serial.println("Enviando imagem para o endpoint de registro...");

  HTTPClient http;
  String server_url = "http://" + String(server_ip) + ":" + String(server_port_http) + "/receber-imagem-esp";
  http.begin(server_url);
  http.addHeader("Content-Type", "image/jpeg");

  int httpResponseCode = http.POST(fb->buf, fb->len);

  if (httpResponseCode > 0) {
    Serial.printf("Registro - Resposta HTTP: %d, Resposta: %s\n", httpResponseCode, http.getString().c_str());
  } else {
    Serial.printf("Registro - Erro no POST HTTP: %s\n", http.errorToString(httpResponseCode).c_str());
  }

  http.end();
  esp_camera_fb_return(fb); // Sempre libere o buffer
}

void setup() {
  // Desativa o Brownout Detector, que pode causar resets com o WiFi e a câmera ligados
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println("\n--- Inicializando Fechadura com Streaming ---");

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  // Configuração da câmera (seu código original, sem alterações)
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
  
  // Para streaming, uma qualidade menor com um tamanho de frame menor funciona melhor
  config.frame_size = FRAMESIZE_VGA; // VGA (640x480) é um bom equilíbrio
  config.jpeg_quality = 12; // 10-12 é bom para streaming
  config.fb_count = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Erro ao iniciar câmera: 0x%x", err);
    return;
  }
  
  // Conexão WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado!");
  Serial.print("IP da ESP32-CAM: ");
  Serial.println(WiFi.localIP());

  // --- NOVO: Inicia o servidor web da câmera ---
  // Isso cria os endpoints /capture, /stream, etc.
  startCameraServer();

  Serial.println("Servidor de Câmera iniciado.");
  Serial.println("Para ver o stream, acesse: http://" + WiFi.localIP().toString() + "/stream");
  Serial.println("Para capturar uma foto, acesse: http://" + WiFi.localIP().toString() + "/capture");
}

// Função de reconhecimento (seu código original, sem alterações)
void recognizeFace() {
  Serial.println("\n(Reconhecimento) Capturando imagem...");
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { Serial.println("Falha na captura da imagem."); return; }

  WiFiClient client;
  if (!client.connect(server_ip, server_port_socket)) {
    Serial.println("Falha na conexão com o servidor de socket");
    esp_camera_fb_return(fb);
    return;
  }
  
  client.write(fb->buf, fb->len);
  unsigned long start = millis();
  while (!client.available() && millis() - start < 10000) { delay(10); }

  if (client.available()) {
    String response = client.readString();
    response.trim();
    if (response == "1") { Serial.println("ACESSO PERMITIDO!"); matchFace = true; } 
    else { Serial.println("ACESSO NEGADO!"); matchFace = false; }
  } else { Serial.println("Timeout aguardando resposta do socket."); }
  client.stop();
  esp_camera_fb_return(fb);
}


void loop() {
  server.handleClient(); // <-- NOVO: Processa requisições HTTP recebidas

  if (!activeRelay) {
    recognizeFace(); // O reconhecimento continua rodando normalmente
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