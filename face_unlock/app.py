import socket
import threading
import os
import io
from flask import Flask, render_template, request, jsonify
from deepface import DeepFace
import logging
from datetime import datetime
import time

# --- Configurações ---
HOST = '0.0.0.0'
PORT = 8081
KNOWN_FACES_DIR = 'known_faces'
RECEIVED_IMAGES_DIR = 'received_images'

# Garante que as pastas existam
os.makedirs(KNOWN_FACES_DIR, exist_ok=True)
os.makedirs(RECEIVED_IMAGES_DIR, exist_ok=True)

# Desativa logs detalhados do TensorFlow
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '3'
logging.getLogger('tensorflow').setLevel(logging.FATAL)

# Inicializa o Flask
app = Flask(__name__)


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
        image_data = b''
        client_socket.settimeout(5.0)  # evita travamento eterno na leitura

        while True:
            try:
                chunk = client_socket.recv(4096)
                if not chunk:
                    break
                image_data += chunk
            except socket.timeout:
                print("Timeout na leitura da imagem (encerrando leitura).")
                break

        if image_data:
            print("Imagem recebida. Iniciando verificação...")
            start_time = time.time()

            image_stream = io.BytesIO(image_data)
            name = verify_face(image_stream)

            duration = time.time() - start_time
            print(f"DeepFace levou {duration:.2f} segundos.")

            response_data = b'1' if name else b'0'
            if name:
                print(f"Rosto reconhecido: {name}. Enviando resposta '1'.")
            else:
                print("Nenhuma correspondência. Enviando resposta '0'.")

            # Envia a resposta
            client_socket.sendall(response_data)
            print("Resposta enviada para o buffer de rede.")

            # Fecha a escrita, mas mantém a leitura por um curto período
            try:
                client_socket.shutdown(socket.SHUT_WR)
            except Exception as e:
                print("Erro ao dar shutdown no socket:", e)

            time.sleep(0.1)  # permite o envio total antes de fechar

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
        client_thread = threading.Thread(
            target=handle_client_connection, args=(client_socket, address)
        )
        client_thread.start()


# --- Endpoints Web Flask ---

@app.route('/register', methods=['POST'])
def register_face():
    if 'image' not in request.files or 'name' not in request.form:
        return jsonify({'status': 'error', 'message': 'Faltando imagem ou nome.'}), 400

    image_file = request.files['image']
    name = request.form['name'].strip()

    if not name:
        return jsonify({'status': 'error', 'message': 'O nome não pode estar vazio.'}), 400

    filename = f"{name}.jpg"
    filepath = os.path.join(KNOWN_FACES_DIR, filename)
    image_file.save(filepath)
    print(f"Rosto de {name} registrado em {filepath}")
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
    return render_template('webcam.html')


@app.route('/')
def index():
    return "Servidor de Reconhecimento Facial (DeepFace) no ar. Acesse /webcam_test para testar."


if __name__ == '__main__':
    print("Inicializando o servidor...")
    socket_thread = threading.Thread(target=start_socket_server)
    socket_thread.daemon = True
    socket_thread.start()
    print("Iniciando servidor Flask. Acesse http://127.0.0.1:5000/webcam_test no seu navegador.")
    app.run(host='0.0.0.0', port=5000)
