import socket

s = socket.socket()

s.connect(('127.0.0.1', 12345))

s.send(b"Hello!")

data = s.recv(1024)

print("Ответ от сервера:", data.decode())

s.close()