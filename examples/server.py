import socket

HOST = '127.0.0.1'
PORT = 12345

s = socket.socket()

s.bind((HOST, PORT))

s.listen(1)
print("Жду клиента...")

client, addr = s.accept()
print("Клиент подключился")

data = client.recv(1024)
print("Получил:", data.decode())

client.send(b"Hello World!")

client.close()
s.close()