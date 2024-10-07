import json
import time
import socket

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect('/tmp/unix-hostqueue-0')

with open('delay-test.csv', 'w') as file:
    file.write('start, end, timestamp, control delay, sync delay\n')
    while True:
        start = time.time()
        sock.sendall("\n".encode())
        status = json.loads(sock.recv(2048).decode())
        end = time.time()
        file.write(f'{start}, {end}, {status["timestamp"] / 1000}, {end - start}, {status["timestamp"] / 1000 - start}\n')