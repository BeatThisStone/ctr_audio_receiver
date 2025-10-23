import socket
import soundcard as sc

HOST = '10.123.224.192'
PORT = 9999

# Get default speaker (loopback)
speaker = sc.default_speaker()
mic = sc.get_microphone(sc.default_speaker().name, include_loopback=True)

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
    sock.connect((HOST, PORT))
    print(f"Connected to {HOST}:{PORT}")

    with mic.recorder(samplerate=22050, channels=2) as rec:
        try:
            while True:
                data = rec.record(numframes=4096)
                sock.sendall(data.tobytes())
        except KeyboardInterrupt:
            print("Stopping...")