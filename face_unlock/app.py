import socket
import threading
import os
import io
from flask import Flask, render_template, request, jsonify
from deepface import DeepFace
import logging
import time
import requests  # <-- NOVO: Importado para fazer requisições para a ESP

# --- Configurações ---
HOST = '0.0.0.0'
PORT = 8081
KNOWN_FACES_DIR = 'known_faces'
RECEIVED_IMAGES_DIR = 'received_images'
ESP32_CAM_IP = "192.168.2.152"
ESP32_CAM_STREAM_PORT = "81" # <-- Porta padrão do stream
ESP32_CAM_CONTROL_PORT = "80" # <-- Porta padrão dos comandos


os.makedirs(KNOWN_FACES_DIR, exist_ok=True)
os.makedirs(RECEIVED_IMAGES_DIR, exist_ok=True)

os.environ['TF_CPP_MIN_LOG_LEVEL'] = '3'
logging.getLogger('tensorflow').setLevel(logging.FATAL)

# Inicializa o Flask
app = Flask(__name__)

nome_pendente_registro = None
lock = threading.Lock()


def verify_face(image_stream):
    try:
        temp_image_path = "temp_image.jpg"
        with open(temp_image_path, 'wb') as f:
            f.write(image_stream.read())

        dfs = DeepFace.find(
            img_path=temp_image_path,
            db_path=KNOWN_FACES_DIR,
            model_name="VGG-Face",
            enforce_detection=False,
            silent=True
        )

        os.remove(temp_image_path)

        if dfs and not dfs[0].empty:
            best_match_path = dfs[0].iloc[0]['identity']
            match_name = os.path.splitext(os.path.basename(best_match_path))[0]
            return match_name
        return None
    except Exception as e:
        if "Face could not be detected" in str(e):
            print("Nenhum rosto detectado na imagem recebida.")
        else:
            print(f"Erro durante a verificação com DeepFace: {e}")
        if os.path.exists("temp_image.jpg"):
            os.remove("temp_image.jpg")
        return None

def handle_client_connection(client_socket, address):
    print(f"Conexão via Socket aceita de {address}")
    try:
        # ... seu código de socket continua igual ...
        image_data = b''
        client_socket.settimeout(5.0)
        while True:
            try:
                chunk = client_socket.recv(4096)
                if not chunk: break
                image_data += chunk
            except socket.timeout:
                break
        if image_data:
            name = verify_face(io.BytesIO(image_data))
            response_data = b'1' if name else b'0'
            if name: print(f"Rosto reconhecido: {name}. Enviando resposta '1'.")
            else: print("Nenhuma correspondência. Enviando resposta '0'.")
            client_socket.sendall(response_data)
            client_socket.shutdown(socket.SHUT_WR)
    except Exception as e:
        print(f"Ocorreu um erro na thread do cliente: {e}")
    finally:
        print(f"Fechando conexão com {address}")
        client_socket.close()

def start_socket_server():
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind((HOST, PORT))
    server_socket.listen()
    print(f"Servidor de Socket para ESP32 escutando em {HOST}:{PORT}")
    while True:
        client_socket, address = server_socket.accept()
        client_thread = threading.Thread(target=handle_client_connection, args=(client_socket, address))
        client_thread.start()

@app.route('/iniciar-registro', methods=['POST'])
def iniciar_registro():
    global nome_pendente_registro
    data = request.get_json()
    nome = data.get('name')

    if not nome:
        return jsonify({'status': 'error', 'message': 'Nome é obrigatório.'}), 400

    with lock:
        if nome_pendente_registro:
            return jsonify({'status': 'error', 'message': 'Outro registro já está em andamento.'}), 409
        nome_pendente_registro = nome
    
    try:
        print(f"Enviando comando de captura para a ESP32-CAM para o usuário: {nome}")
        capture_url = f'http://{ESP32_CAM_IP}:{ESP32_CAM_CONTROL_PORT}/capture'
        
        response = requests.get(capture_url, timeout=10)
        response.raise_for_status()

        image_data = response.content
        filename = f"{nome_pendente_registro}.jpg"
        filepath = os.path.join(KNOWN_FACES_DIR, filename)
        
        with open(filepath, 'wb') as f:
            f.write(image_data)

        print(f"Rosto de {nome_pendente_registro} registrado com sucesso em {filepath}")
        
        with lock:
            nome_pendente_registro = None
        
        return jsonify({'status': 'success', 'message': f'Rosto de {nome} registrado com sucesso!'})

    except requests.exceptions.RequestException as e:
        print(f"Erro ao comunicar com a ESP32-CAM: {e}")
        with lock:
            nome_pendente_registro = None
        return jsonify({'status': 'error', 'message': 'Não foi possível comunicar com a câmera da fechadura.'}), 500

@app.route('/receber-imagem-esp', methods=['POST'])
def receber_imagem_esp():
    global nome_pendente_registro
    with lock:
        if not nome_pendente_registro:
            return "Erro: Nenhum registro pendente.", 400
        
        # Recebe os dados brutos da imagem
        image_data = request.get_data()
        if not image_data:
            return "Erro: Corpo da requisição vazio.", 400

        # Salva o arquivo
        filename = f"{nome_pendente_registro}.jpg"
        filepath = os.path.join(KNOWN_FACES_DIR, filename)
        
        with open(filepath, 'wb') as f:
            f.write(image_data)

        print(f"Rosto de {nome_pendente_registro} registrado com sucesso em {filepath}")
        
        # Limpa o nome para o próximo registro
        nome_pendente_registro = None
    
    return "Imagem registrada com sucesso!", 200

#opção de registrar rosto via webcam do computador
@app.route('/register', methods=['POST'])
def register_face_from_webcam():
    if 'image' not in request.files or 'name' not in request.form:
        return jsonify({'status': 'error', 'message': 'Faltando imagem ou nome.'}), 400
    image_file = request.files['image']
    name = request.form['name'].strip()
    filename = f"{name}.jpg"
    filepath = os.path.join(KNOWN_FACES_DIR, filename)
    image_file.save(filepath)
    return jsonify({'status': 'success', 'message': f'Rosto de {name} registrado com sucesso!'})

@app.route('/recognize_webcam', methods=['POST'])
def recognize_webcam():
    if 'image' not in request.files:
        return jsonify({'status': 'error', 'message': 'Nenhuma imagem enviada'}), 400
    image_file = request.files['image']
    name = verify_face(image_file.stream)
    if name:
        return jsonify({'status': 'success', 'match': name})
    else:
        return jsonify({'status': 'no_match', 'message': 'Nenhuma correspondência encontrada.'})

@app.route('/webcam_test')
def webcam_test():
    # Passamos as informações da ESP para o HTML poder construir as URLs corretas
    return render_template('index.html', 
                           esp_cam_ip=ESP32_CAM_IP, 
                           esp_cam_stream_port=ESP32_CAM_STREAM_PORT,
                           esp_cam_control_port=ESP32_CAM_CONTROL_PORT)

@app.route('/')
def index():
    return "Servidor de Reconhecimento Facial no ar. Acesse /webcam_test para a interface."


if __name__ == '__main__':
    print("Inicializando o servidor...")
    socket_thread = threading.Thread(target=start_socket_server)
    socket_thread.daemon = True
    socket_thread.start()
    print("Iniciando servidor Flask. Acesse http://127.0.0.1:5000/webcam_test no seu navegador.")
    app.run(host='0.0.0.0', port=5000)