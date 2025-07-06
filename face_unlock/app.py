import socket
import threading
import os
import io
from flask import Flask, render_template, request, jsonify
from deepface import DeepFace
import logging

# --- Configurações ---
HOST = '0.0.0.0'
PORT = 8081
KNOWN_FACES_DIR = 'known_faces'
# Garante que a pasta de rostos conhecidos exista
os.makedirs(KNOWN_FACES_DIR, exist_ok=True)

# Desativa logs muito detalhados do TensorFlow que podem poluir o console
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '3'
logging.getLogger('tensorflow').setLevel(logging.FATAL)

# Inicializa o Flask
app = Flask(__name__)


def verify_face(image_stream):
    """
    Verifica se um rosto na imagem recebida corresponde a algum rosto
    na pasta `KNOWN_FACES_DIR`.
    Usa a função DeepFace.find() que é otimizada para busca em banco de dados.
    """
    try:
        # DeepFace precisa salvar o arquivo temporariamente para processar
        temp_image_path = "temp_image.jpg"
        with open(temp_image_path, 'wb') as f:
            f.write(image_stream.read())

        # Procura pelo rosto na pasta de rostos conhecidos
        # model_name: Modelo de IA a ser usado. VGG-Face é um bom padrão.
        # distance_metric: Como medir a similaridade.
        # dfs: dataframe result
        dfs = DeepFace.find(
            img_path=temp_image_path,
            db_path=KNOWN_FACES_DIR,
            model_name="VGG-Face",
            enforce_detection=False,  # Não falha se não encontrar rosto, apenas retorna vazio
            silent=True  # Suprime as barras de progresso no console
        )

        os.remove(temp_image_path)  # Limpa o arquivo temporário

        # O resultado 'dfs' é uma lista de DataFrames do pandas.
        # Se a lista não estiver vazia e o primeiro DataFrame não estiver vazio, encontramos uma correspondência.
        if dfs and not dfs[0].empty:
            # Pega o caminho da imagem que mais correspondeu
            best_match_path = dfs[0].iloc[0]['identity']
            # Extrai o nome do arquivo (sem extensão)
            match_name = os.path.splitext(os.path.basename(best_match_path))[0]
            print(f"CORRESPONDÊNCIA ENCONTRADA: {match_name}")
            return match_name

        print("Nenhuma correspondência encontrada.")
        return None

    except Exception as e:
        # Se DeepFace não encontrar nenhum rosto na imagem de entrada, ele levanta uma exceção
        if "Face could not be detected" in str(e):
            print("Nenhum rosto detectado na imagem recebida.")
        else:
            print(f"Erro durante a verificação com DeepFace: {e}")

        # Garante que o arquivo temporário seja removido em caso de erro
        if os.path.exists("temp_image.jpg"):
            os.remove("temp_image.jpg")

        return None


# --- Servidor de Socket para ESP32 ---
def handle_client_connection(client_socket, address):
    print(f"Conexão via Socket aceita de {address}")
    try:
        image_data = b''
        while True:
            chunk = client_socket.recv(4096)
            if not chunk: break
            image_data += chunk
        if image_data:
            image_stream = io.BytesIO(image_data)
            name = verify_face(image_stream)
            client_socket.sendall(b'MATCH' if name else b'NO_MATCH')
    finally:
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


# --- Endpoints da Aplicação Web ---

@app.route('/register', methods=['POST'])
def register_face():
    """Endpoint para registrar um novo rosto. Agora, apenas salva a imagem."""
    if 'image' not in request.files or 'name' not in request.form:
        return jsonify({'status': 'error', 'message': 'Faltando imagem ou nome.'}), 400

    image_file = request.files['image']
    name = request.form['name']

    if not name.strip():
        return jsonify({'status': 'error', 'message': 'O nome não pode estar vazio.'}), 400

    # Salva o arquivo de imagem diretamente na pasta
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
    return f"Servidor de Reconhecimento Facial (DeepFace) no ar. Acesse /webcam_test para testar."


if __name__ == '__main__':
    # Na primeira vez que você executar uma verificação,
    # o deepface irá baixar automaticamente os modelos de reconhecimento facial.
    # Isso pode levar alguns minutos e requer conexão com a internet.
    print("Inicializando o servidor...")

    socket_thread = threading.Thread(target=start_socket_server)
    socket_thread.daemon = True
    socket_thread.start()

    print("Iniciando servidor Flask. Acesse http://127.0.0.1:5000/webcam_test no seu navegador.")
    app.run(host='0.0.0.0', port=5000)
