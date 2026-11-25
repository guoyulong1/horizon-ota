#!/usr/bin/env python3
"""本地 HTTPS 测试服务器，支持 Range（206），供集成测试使用。

用法: serve.py <port> <cert.pem> <key.pem> <root-dir>
"""
import http.server
import os
import ssl
import sys


def main():
    port = int(sys.argv[1])
    cert = sys.argv[2]
    key = sys.argv[3]
    root = sys.argv[4]
    os.chdir(root)

    class Handler(http.server.SimpleHTTPRequestHandler):
        def do_GET(self):
            path = self.translate_path(self.path)
            if not os.path.isfile(path):
                self.send_error(404)
                return
            with open(path, 'rb') as handle:
                data = handle.read()
            size = len(data)
            range_header = self.headers.get('Range')
            if range_header and range_header.startswith('bytes='):
                spec = range_header[len('bytes='):]
                start_text, _, end_text = spec.partition('-')
                start = int(start_text) if start_text else 0
                end = int(end_text) if end_text else size - 1
                end = min(end, size - 1)
                if start > end or start >= size:
                    self.send_response(416)
                    self.send_header('Content-Range', 'bytes */%d' % size)
                    self.end_headers()
                    return
                body = data[start:end + 1]
                self.send_response(206)
                self.send_header('Content-Range',
                                 'bytes %d-%d/%d' % (start, end, size))
                self.send_header('Accept-Ranges', 'bytes')
                self.send_header('Content-Length', str(len(body)))
                self.send_header('Content-Type', 'application/octet-stream')
                self.end_headers()
                self.wfile.write(body)
            else:
                self.send_response(200)
                self.send_header('Accept-Ranges', 'bytes')
                self.send_header('Content-Length', str(size))
                self.send_header('Content-Type', 'application/octet-stream')
                self.end_headers()
                self.wfile.write(data)

        def log_message(self, *args):
            pass

    server = http.server.HTTPServer(('127.0.0.1', port), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    server.serve_forever()


if __name__ == '__main__':
    main()
