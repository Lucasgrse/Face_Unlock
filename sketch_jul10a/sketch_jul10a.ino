#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClient.h>

// --- Configurações da Câmera e Pinos ---
// ATENÇÃO: Verifique se o modelo da sua câmera está correto.
//          AI-THINKER é o mais comum.
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// --- Configurações de Rede ---
const char* ssid = "Perdidao";
const char* password = "nemtefalo";

// --- Configurações do Servidor de Backend ---
// Coloque o IP do seu computador onde o servidor Python está rodando
const char* server_ip = "192.168.2.108"; 
const int server_port = 8081;

// --- Pinos de Hardware ---
#define LED_BUILTIN 4 // Em algumas placas pode ser outro pino
#define relay 4       // Pino para o relé
#define buzzer 2      // Pino para o buzzer

// --- Variáveis Globais de Controle ---
boolean matchFace = false;
boolean activeRelay = false;
long prevMillis = 0;
int interval = 5000; // 5 segundos para o relé ficar ativo

//====================================================================
// FUNÇÃO PARA RECONHECIMENTO FACIAL (Comunicação com o Backend)
//====================================================================
void recognizeFace() {
  Serial.println("Capturando imagem para reconhecimento...");
  camera_fb_t * fb = NULL;
  
  // Captura um frame da câmera
  fb = esp_camera_fb_get(); 
  if (!fb) {
    Serial.println("Falha ao capturar imagem da câmera");
    return;
  }

  Serial.printf("Tamanho da imagem: %u bytes\n", fb->len);

  WiFiClient client;
  // Conecta ao servidor de socket Python
  if (!client.connect(server_ip, server_port)) {
    Serial.println("Falha ao conectar ao servidor");
    esp_camera_fb_return(fb); // Libera o buffer da imagem
    return;
  }

  Serial.println("Conectado ao servidor. Enviando imagem...");
  
  // Envia os bytes da imagem para o servidor
  client.write(fb->buf, fb->len);
  // É importante chamar stop() para fechar a conexão e sinalizar ao servidor que o envio terminou.
  client.stop(); 

  Serial.println("Imagem enviada. Aguardando resposta...");
  
  // Libera o buffer da imagem o mais rápido possível
  esp_camera_fb_return(fb);

  // Aguarda a resposta do servidor por um tempo
  unsigned long timeout = millis();
  while (!client.available() && millis() - timeout < 5000) {
    delay(100);
  }
  
  if (client.available()) {
    String response = client.readStringUntil('\r');
    Serial.print("Resposta do servidor: ");
    Serial.println(response);
    
    if (response.indexOf("MATCH") != -1) {
      Serial.println("ACESSO PERMITIDO!");
      matchFace = true; // Ativa a variável global para acionar o relé
    } else {
      Serial.println("ACESSO NEGADO!");
      matchFace = false;
    }
  } else {
    Serial.println("Nenhuma resposta do servidor (timeout).");
  }

  client.stop();
}


//====================================================================
// SETUP - Executa uma vez na inicialização
//====================================================================
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // Configuração dos Pinos
  pinMode(relay, OUTPUT); 
  pinMode(buzzer, OUTPUT); 
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(relay, LOW);
  digitalWrite(buzzer, LOW);

  // Configuração da Câmera
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

  // Inicialização da Câmera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_QVGA); // Usa uma resolução menor para envios mais rápidos

  // Conexão Wi-Fi
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
  // --- Lógica de Gatilho para Reconhecimento ---
  // Para este exemplo, vamos tentar reconhecer um rosto a cada 10 segundos,
  // mas apenas se o relé não estiver já ativo.
  static unsigned long lastRecognitionTime = 0;
  unsigned long currentTime = millis();

  if (!activeRelay && (currentTime - lastRecognitionTime > 10000)) {
    recognizeFace(); // Chama a função para capturar e enviar a foto
    lastRecognitionTime = currentTime;
  }

  // --- Lógica de Ativação do Relé e Buzzer ---
  // Esta parte do código é a sua lógica original e funciona com base
  // no resultado da variável 'matchFace'.
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