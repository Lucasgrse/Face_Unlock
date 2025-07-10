import socket

with open("Victor.jpg", "rb") as f:
    data = f.read()

s = socket.socket()
s.connect(("192.168.2.108", 8081))
s.sendall(data)

response = s.recv(1024)
print("Resposta:", response)
