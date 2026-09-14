admin@localhost:~# cat listenning_lora.py 
#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Listener local para o protocolo UDP Semtech
usado pelo Basic Packet Forwarder do MultiTech Conduit.

Compatível com Python 2.7.

Uso:
    python listenning_lora.py
    python listenning_lora.py --port 1700
    python listenning_lora.py --bind 0.0.0.0 --port 1700
"""

import argparse
import base64
import json
import socket
from datetime import datetime


# Identificadores do protocolo Semtech
PUSH_DATA = 0x00
PUSH_ACK = 0x01
PULL_DATA = 0x02
PULL_RESP = 0x03
PULL_ACK = 0x04
TX_ACK = 0x05


IDENT_NAMES = {
    PUSH_DATA: "PUSH_DATA",
    PUSH_ACK: "PUSH_ACK",
    PULL_DATA: "PULL_DATA",
    PULL_RESP: "PULL_RESP",
    PULL_ACK: "PULL_ACK",
    TX_ACK: "TX_ACK",
}


def now():
    """
    Retorna horario UTC.
    Python 2.7 nao possui datetime.timezone.
    """
    return datetime.utcnow().strftime("%H:%M:%S.%f")[:-3]


def bytes_to_hex(data):
    """
    Converte bytes para hexadecimal.
    Substitui bytes.hex(), que nao existe no Python 2.7.
    """
    return "".join("%02x" % ord(c) for c in data)


def make_byte(value):
    """
    Cria uma string contendo um byte.
    Necessario para Python 2.7.
    """
    return chr(value)


def print_rxpk(pkt):
    freq = pkt.get("freq")
    datr = pkt.get("datr")
    rssi = pkt.get("rssi")
    snr = pkt.get("lsnr")
    size = pkt.get("size")
    data_b64 = pkt.get("data", "")

    try:
        raw = base64.b64decode(data_b64)
        hexdump = bytes_to_hex(raw)
    except Exception:
        raw = ""
        hexdump = "<erro ao decodificar base64>"

    print "[%s] RX pacote LoRa" % now()

    print (
        "  Freq: %s MHz | DR: %s | RSSI: %s dBm | "
        "SNR: %s dB | Tamanho: %s bytes"
        % (freq, datr, rssi, snr, size)
    )

    print "  Payload (hex): %s" % hexdump

    try:
        ascii_payload = raw.decode("utf-8", "replace")
        print "  Payload (ascii, best-effort): %s" % ascii_payload
    except Exception:
        pass

    print "-" * 70


def handle_push_data(payload, addr, sock, token):
    """
    Processa PUSH_DATA enviado pelo gateway.
    """

    # Cabecalho Semtech:
    #
    # byte 0 = protocol version
    # byte 1-2 = token
    # byte 3 = identifier
    # byte 4-11 = gateway EUI
    #
    # JSON comeca no byte 12.

    if len(payload) < 12:
        print "[%s] PUSH_DATA invalido de %s" % (now(), addr)
        return

    body = payload[12:]

    try:
        obj = json.loads(body.decode("utf-8"))
    except Exception as e:
        print "[%s] Falha ao parsear JSON de %s: %s" % (
            now(),
            addr,
            e
        )

        print "  Raw: %r" % body
        return

    # Pacotes LoRa recebidos
    rxpk_list = obj.get("rxpk", [])

    if rxpk_list:
        for pkt in rxpk_list:
            print_rxpk(pkt)

    # Informacoes estatisticas do gateway
    if "stat" in obj:
        try:
            stat = json.dumps(obj["stat"])
        except Exception:
            stat = repr(obj["stat"])

        print "[%s] STAT do gateway %s: %s" % (
            now(),
            addr,
            stat
        )

        print "-" * 70

    # ACK:
    #
    # protocolo + token + PUSH_ACK
    #
    ack = payload[0] + token + make_byte(PUSH_ACK)

    sock.sendto(ack, addr)


def handle_pull_data(payload, addr, sock, token):
    """
    Processa PULL_DATA / keepalive do gateway.
    """

    if len(payload) < 12:
        print "[%s] PULL_DATA invalido de %s" % (
            now(),
            addr
        )
        return

    # Gateway EUI fica nos bytes 4-11
    gw_eui = bytes_to_hex(payload[4:12])

    print (
        "[%s] PULL_DATA (keepalive) "
        "do gateway %s @ %s"
        % (now(), gw_eui, addr)
    )

    # ACK:
    #
    # protocolo + token + PULL_ACK
    #
    ack = payload[0] + token + make_byte(PULL_ACK)

    sock.sendto(ack, addr)


def handle_tx_ack(payload, addr):
    """
    Processa TX_ACK.
    """

    if len(payload) < 12:
        print "[%s] TX_ACK invalido de %s" % (
            now(),
            addr
        )
        return

    body = payload[12:]

    try:
        message = body.decode("utf-8", "replace")
    except Exception:
        message = repr(body)

    print "[%s] TX_ACK de %s: %s" % (
        now(),
        addr,
        message
    )


def main():

    parser = argparse.ArgumentParser(
        description="Listener UDP protocolo Semtech"
    )

    parser.add_argument(
        "--port",
        type=int,
        default=1700,
        help="Porta UDP local para escutar (default: 1700)"
    )

    parser.add_argument(
        "--bind",
        default="0.0.0.0",
        help="Endereco para bind (default: 0.0.0.0)"
    )

    args = parser.parse_args()

    # Cria socket UDP
    sock = socket.socket(
        socket.AF_INET,
        socket.SOCK_DGRAM
    )

    # Permite reutilizar a porta
    sock.setsockopt(
        socket.SOL_SOCKET,
        socket.SO_REUSEADDR,
        1
    )

    sock.bind(
        (args.bind, args.port)
    )

    print "=" * 70
    print "Listener LoRa UDP - Semtech Packet Forwarder"
    print "=" * 70
    print "Escutando em %s:%d" % (
        args.bind,
        args.port
    )
    print "Aguardando pacotes do gateway..."
    print "=" * 70

    while True:

        try:
            payload, addr = sock.recvfrom(65535)

        except socket.error as e:
            print "[%s] Erro no socket: %s" % (
                now(),
                e
            )
            continue

        # Pacote minimo:
        #
        # byte 0 = protocolo
        # byte 1-2 = token
        # byte 3 = identificador
        #
        if len(payload) < 4:
            print "[%s] Pacote muito pequeno de %s" % (
                now(),
                addr
            )
            continue

        # Identificador do protocolo
        ident = ord(payload[3])

        # Token
        token = payload[1:3]

        if ident == PUSH_DATA:

            handle_push_data(
                payload,
                addr,
                sock,
                token
            )

        elif ident == PULL_DATA:

            handle_pull_data(
                payload,
                addr,
                sock,
                token
            )

        elif ident == TX_ACK:

            handle_tx_ack(
                payload,
                addr
            )

        elif ident == PUSH_ACK:

            print "[%s] PUSH_ACK recebido de %s" % (
                now(),
                addr
            )

        elif ident == PULL_ACK:

            print "[%s] PULL_ACK recebido de %s" % (
                now(),
                addr
            )

        elif ident == PULL_RESP:

            print "[%s] PULL_RESP recebido de %s" % (
                now(),
                addr
            )

        else:

            name = IDENT_NAMES.get(
                ident,
                hex(ident)
            )

            print (
                "[%s] Pacote nao tratado (%s) "
                "de %s: %r"
                % (
                    now(),
                    name,
                    addr,
                    payload
                )
            )


if __name__ == "__main__":

    try:
        main()

    except KeyboardInterrupt:
        print ""
        print "Listener encerrado."



admin@localhost:~# 



