#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClient.h>

// --- Configurações da Câmera e Pinos ---
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// --- Configurações de Rede ---
const char* ssid = "Perdidao";
const char* password = "nemtefalo";

// --- Configurações do Servidor de Backend ---
const char* server_ip = "192.168.2.108"; 
const int server_port = 8081;

// --- Pinos de Hardware ---
#define LED_BUILTIN 4
#define relay 4
#define buzzer 2

// --- Variáveis Globais de Controle ---
boolean matchFace = false;
boolean activeRelay = false;
long prevMillis = 0;
int interval = 5000; // 5 segundos para o relé ficar ativo
boolean isRecognizing = false;

//====================================================================
// FUNÇÃO PARA RECONHECIMENTO FACIAL (Comunicação com o Backend)
//====================================================================
void recognizeFace() {
  isRecognizing = true;

  Serial.println("===================================");
  Serial.println("Iniciando novo ciclo de reconhecimento...");

  camera_fb_t * fb = NULL;
  
  fb = esp_camera_fb_get(); 
  if (!fb) {
    Serial.println("Falha ao capturar imagem da câmera");
    isRecognizing = false; // <-- Libera a flag em caso de erro
    return;
  }

  Serial.printf("Tamanho da imagem: %u bytes\n", fb->len);

  WiFiClient client;
  if (!client.connect(server_ip, server_port)) {
    Serial.println("Falha ao conectar ao servidor");
    esp_camera_fb_return(fb);
    isRecognizing = false; // <-- Libera a flag em caso de erro
    return;
  }

  Serial.println("Conectado ao servidor. Enviando imagem...");
  client.write(fb->buf, fb->len);
  Serial.println("Imagem enviada. Aguardando resposta...");
  esp_camera_fb_return(fb);

  unsigned long timeout = millis();
  // Use o timeout aumentado que testamos antes (ex: 20 segundos)
  while (!client.available() && millis() - timeout < 5000) { 
    delay(100);
  }
  
  if (client.available()) {
    String response = client.readString();
    Serial.print("Resposta do servidor: ");
    Serial.println(response);
    
    if (response == "1") {
      Serial.println("ACESSO PERMITIDO!");
      matchFace = true;
    } else {
      Serial.println("ACESSO NEGADO!");
      matchFace = false;
    }
  } else {
    Serial.println("Nenhuma resposta do servidor (timeout).");
  }

  client.stop();
  Serial.println("Ciclo de reconhecimento finalizado.");
  
  isRecognizing = false; 
}



//====================================================================
// SETUP - Executa uma vez na inicialização
//====================================================================
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  pinMode(relay, OUTPUT); 
  pinMode(buzzer, OUTPUT); 
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(relay, LOW);
  digitalWrite(buzzer, LOW);

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
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_QVGA);

  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado!");
  Serial.print("Endereço IP da ESP32: ");
  Serial.println(WiFi.localIP());
}


//====================================================================
// LOOP - Executa continuamente
//====================================================================
void loop() {
  unsigned long currentTime = millis();

  if (!activeRelay && !isRecognizing) {
    recognizeFace();
  }

  if (matchFace == true && activeRelay == false){
    activeRelay = true;
    digitalWrite (relay, HIGH);
    digitalWrite (buzzer, HIGH);
    delay(800);
    digitalWrite (buzzer, LOW);
    prevMillis = millis();
  }
  
  if(activeRelay == true && millis() - prevMillis > interval){
    activeRelay = false;
    matchFace = false; 
    digitalWrite(relay, LOW);
  }               
}
