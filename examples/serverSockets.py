import zmq

context = zmq.Context()
socket = context.socket(zmq.REP)

socket.bind("tcp://*:5555")
print("Сервер запущен на порту 5555")
print("Ожидаю подключения Android...")

count = 0
log_file = "android_data.txt"


while True:
    data = socket.recv_string()
    count += 1

    print(f"Получено от Android: {data}")
    print(f"Пакет номер: {count}")

    file = open("android_log.txt", "a")
    file.write(f"{count}: {data}\n")
    file.close()

    reply = "Hello from Server!"
    socket.send_string(reply)
    print(f"Отправлен ответ: {reply}")