#!/usr/bin/python3

import argparse
import http.server as http_server
import os
import ssl


class RequestHandler(http_server.BaseHTTPRequestHandler):
    def do_GET(self):
        has_client_cert = self.connection.getpeercert() is not None

        if self.path == "/require-client-cert":
            response = 200 if has_client_cert else 403
        elif self.path == "/forbid-client-cert":
            response = 403 if has_client_cert else 200
        else:
            response = 404

        self.send_response(response)
        self.send_header("Content-Length", "0")
        self.end_headers()


def run(cert, key, client_ca):
    RequestHandler.protocol_version = "HTTP/1.0"
    httpd = http_server.HTTPServer(("127.0.0.1", 0), RequestHandler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    context.load_verify_locations(client_ca)
    context.verify_mode = ssl.CERT_OPTIONAL
    httpd.socket = context.wrap_socket(httpd.socket, server_side=True)

    host, port = httpd.socket.getsockname()[:2]
    with open("httpd-port", "w") as file:
        file.write(str(port))
    try:
        os.write(3, b"Started\n")
    except OSError:
        pass
    httpd.serve_forever()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--cert", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--client-ca", required=True)
    args = parser.parse_args()
    run(args.cert, args.key, args.client_ca)
