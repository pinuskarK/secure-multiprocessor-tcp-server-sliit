#!/usr/bin/env python3
"""
client_2410.py - IE2102 Network Programming Assignment
Registration No: IT24102410
"""

import socket
import sys

import time
import threading

HOST   = "127.0.0.1"
PORT   = 50410
BUFFER = 4096

def send_request(sock, payload):
    message = f"LEN:{len(payload)}\n{payload}"
    try:
        sock.sendall(message.encode())
        response = sock.recv(BUFFER)
        if not response:
            print("[!] Server closed the connection.")
            return False
        print(f"  Server >> {response.decode().strip()}")
        return True
    except Exception as e:
        print(f"[ERROR] Send failed: {e}")
        return False

def print_banner():
    print("=" * 55)
    print("  IE2102 Network Programming - TCP Client")
    print("  Reg No : IT24102410  |  Port : 50410")
    print("=" * 55)

def print_help():
    print("""
  Available Commands:
  -------------------
  REGISTER <username> <password>  - Create a new account
  LOGIN    <username> <password>  - Login to your account
  LOGOUT                          - Logout current session
  help                            - Show this help message
  exit                            - Disconnect and quit
""")










def main():
    print_banner()
    print(f"\n[*] Connecting to {HOST}:{PORT} ...")
     // ... connection setup ...

    char *cmd = "STATUS";
    send(sock, cmd, strlen(cmd), 0);

    char buffer[1024] = {0};
    read(sock, buffer, 1024);
    printf("Server Response: %s\n", buffer);

    return 0;
    

    client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        client.connect((HOST, PORT))
        print(f"[+] Connected to server at {HOST}:{PORT}\n")
    except Exception as e:
        print(f"[!] Connection failed: {e}")
        sys.exit(1)

    # Ask for username and password only - no auto REGISTER/LOGIN
    print("-" * 55)
    username = input("  Enter username : ").strip()
    password = input("  Enter password : ").strip()
    print("-" * 55)
    print(f"\n  [*] Credentials saved. Use REGISTER or LOGIN command.")
    print(f"  [*] Type 'help' to see available commands.\n")

    try:
        while True:
            try:
                cmd = input(">> ").strip()
            except (EOFError, KeyboardInterrupt):
                print("\n[*] Interrupted. Disconnecting...")
                break

            if not cmd:
                continue

            if cmd.lower() == "exit":
                print("[*] Goodbye!")
                break

            if cmd.lower() == "help":
                print_help()
                continue

            # Allow shortcut: if user just types REGISTER or LOGIN
            # automatically fill in the username and password they entered
            if cmd.upper() == "REGISTER":
                cmd = f"REGISTER {username} {password}"
            elif cmd.upper() == "LOGIN":
                cmd = f"LOGIN {username} {password}"

   
            if not send_request(client, cmd):
                break

    finally:
        client.close()
        print("[*] Client disconnected.")

if __name__ == "__main__":
    main()
