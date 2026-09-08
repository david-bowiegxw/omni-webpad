from http.server import BaseHTTPRequestHandler,HTTPServer
from pathlib import Path
from urllib.parse import urlsplit,parse_qs
import json,time
root=Path(__file__).resolve().parents[1]
class Handler(BaseHTTPRequestHandler):
 def do_GET(self):
  p=urlsplit(self.path)
  if p.path in ['/state','/status']:
   m=int(parse_qs(p.query).get('m',['0'])[0]);data=json.dumps(dict(version='preview',mask=m,usb=True,ready=True,operational=False,reports=123,gets=0,sets=0,events=[])).encode();ctype='application/json'
   if m: print('MASK',m,flush=True)
  else:data=(root/'src/index.html').read_bytes();ctype='text/html; charset=utf-8'
  self.send_response(200);self.send_header('Content-Type',ctype);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
 def log_message(self,*args):pass
HTTPServer(('127.0.0.1',8787),Handler).serve_forever()
