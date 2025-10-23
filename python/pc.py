import socket
import subprocess

# 3DS IP and port
HOST = '10.123.224.192'  # change to your 3DS IP
PORT = 9999

# FFmpeg command to capture audio and output raw PCM 16-bit stereo at 22050 Hz
ffmpeg_cmd = [
    'ffmpeg',
    '-f', 'dshow',                # Windows audio input
    '-i', 'audio=Missaggio stereo (Realtek(R) Audio)',  # your audio device name
    '-ac', '2',
    '-ar', '22050',
    '-f', 's16le',
    '-acodec', 'pcm_s16le',
    '-'
]

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))
    print(f"Connected to {HOST}:{PORT}")

    ffmpeg = subprocess.Popen(ffmpeg_cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)

    try:
        while True:
            data = ffmpeg.stdout.read(4096)
            if not data:
                break
            sock.sendall(data)
    except KeyboardInterrupt:
        print("Stopping...")
    finally:
        ffmpeg.terminate()
        sock.close()

if __name__ == '__main__':
    main()