"""A stand-in daemon that never finishes a reply: after a client connects it sends one
byte every 200 ms and never a newline. `tarn doctor` must give up on it in bounded time.
Usage: python3 -I trickle_daemon.py <socket path>"""
import os
import socket
import sys
import threading
import time

path = sys.argv[1]
os.makedirs(os.path.dirname(path), exist_ok=True)
if os.path.exists(path):
    os.unlink(path)
server = socket.socket(socket.AF_UNIX)
server.bind(path)
server.listen(4)


def trickle(conn):
    try:
        while True:
            conn.sendall(b"x")
            time.sleep(0.2)
    except OSError:
        pass


while True:
    conn, _ = server.accept()
    threading.Thread(target=trickle, args=(conn,), daemon=True).start()
