"""Native-image packer and bounded serial client. Never resets or flashes a board.
Python stdlib for packing; pyserial is needed only for serial subcommands.
PNGs/JPEGs are optional via Pillow; P4/P5 Netpbm and native bin need no extras.
"""
from pathlib import Path
import argparse, binascii, struct, sys, time, zlib

W,H=128,296
BYTES=W*H//4
MAGIC=b'ET014 DRIVER 1.'

def pgm_tokens(data, count):
    pos=0; tokens=[]
    while len(tokens)<count:
        while pos<len(data) and data[pos] in b' \t\r\n': pos+=1
        if pos<len(data) and data[pos]==35:
            end=data.find(b'\n',pos)
            if end<0: raise ValueError('Unterminated PNM comment')
            pos=end+1; continue
        end=pos
        while end<len(data) and data[end] not in b' \t\r\n': end+=1
        if end==pos: raise ValueError('Truncated PNM header')
        tokens.append(data[pos:end]); pos=end
    if pos>=len(data) or data[pos] not in b' \t\r\n': raise ValueError('PNM header terminator missing')
    pos+=2 if data[pos:pos+2]==b'\r\n' else 1
    return tokens,pos

def read_gray(path):
    data=path.read_bytes()
    if data.startswith((b'P4',b'P5')):
        tokens,pos=pgm_tokens(data,3 if data[:2]==b'P4' else 4)
        w,h=int(tokens[1]),int(tokens[2])
        if (w,h)!=(W,H): raise ValueError('Image must be exactly 128x296; resize deliberately before upload')
        raw=data[pos:]
        if data[:2]==b'P5':
            if int(tokens[3])!=255 or len(raw)!=W*H: raise ValueError('Require P5 maxval255 and exact byte count')
            return raw
        if len(raw)!=W*H//8: raise ValueError('Wrong P4 payload length')
        return bytes(0 if raw[i//8]&(0x80>>(i%8)) else 255 for i in range(W*H))
    if path.suffix.lower()=='.bin':
        if len(data)==W*H//8:
            return bytes(0 if data[i//8]&(0x80>>(i%8)) else 255 for i in range(W*H))
        if len(data)==BYTES:
            return bytes(((data[i//4]>>(6-2*(i%4)))&3)*85 for i in range(W*H))
        raise ValueError('Native .bin must be 4736 bytes (1=black) or 9472 bytes (2bpp 0=black/3=white)')
    try:
        from PIL import Image
    except ImportError as exc:
        raise ValueError('PNG/JPEG import needs Pillow; P4/P5 or native .bin works without it') from exc
    with Image.open(path) as image:
        if image.size!=(W,H): raise ValueError('Image must be exactly 128x296')
        return image.convert('L').tobytes()

def pack(gray, method='gray'):
    if len(gray)!=W*H: raise ValueError('Wrong grayscale image length')
    bayer=((0,8,2,10),(12,4,14,6),(3,11,1,9),(15,7,13,5))
    result=bytearray(BYTES)
    for i,g in enumerate(gray):
        if method=='mono': v=3 if g>=128 else 0
        elif method=='bayer':
            v=3 if g*16 > bayer[(i//W)&3][i&3]*255+127 else 0
        else: v=(g*3+127)//255
        result[i//4]|=v<<(6-2*(i%4))
    return bytes(result)

def preview(data,path):
    gray=bytes(((data[i//4]>>(6-2*(i%4)))&3)*85 for i in range(W*H))
    def chunk(t,b): return struct.pack('>I',len(b))+t+b+struct.pack('>I',binascii.crc32(t+b)&0xffffffff)
    raw=b''.join(b'\0'+gray[y*W:(y+1)*W] for y in range(H))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',W,H,8,0,0,0,0))+
                     chunk(b'IDAT',zlib.compress(raw,9))+chunk(b'IEND',b''))

class Client:
    def __init__(self,port,log=None):
        import serial
        self.port=serial.Serial()
        self.port.port=port; self.port.baudrate=115200
        self.port.timeout=.1; self.port.write_timeout=3
        self.port.dtr=False; self.port.rts=False
        self.log=Path(log) if log else None; self.transcript=bytearray()
    def __enter__(self):
        self.port.open()
        try:
            self.port.write(b'help\n'); self.port.flush()
            self.read_until(MAGIC,3)
            self.drain(.25)
        except BaseException:
            self.__exit__(None,None,None); raise
        return self
    def __exit__(self,*_):
        self.port.close()
        if self.log:
            self.log.parent.mkdir(parents=True,exist_ok=True); self.log.write_bytes(self.transcript)
    def read_until(self,token,seconds,line=False):
        start=len(self.transcript); deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            block=self.port.read(min(max(self.port.in_waiting,1),4096))
            self.transcript.extend(block)
            if len(self.transcript)>262144: raise RuntimeError('Unexpected output volume')
            segment=self.transcript[start:]
            if b'ERR ' in segment: raise RuntimeError(segment.decode(errors='replace'))
            position=segment.find(token)
            if position>=0 and (not line or segment.find(b'\r\n',position+len(token))>=0):
                return bytes(segment)
        raise TimeoutError('Timed out waiting for '+repr(token)+'; captured: '+self.transcript[start:].decode(errors='replace'))
    def drain(self,seconds):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            block=self.port.read(min(max(self.port.in_waiting,1),4096)); self.transcript.extend(block)
    def command(self,cmd,timeout=20):
        if '\n' in cmd or '\r' in cmd or len(cmd)>120: raise ValueError('One bounded command only')
        self.port.write(cmd.encode('ascii')+b'\n'); self.port.flush()
        prefix=('DONE '+cmd+' result=').encode('ascii')
        response=self.read_until(prefix,timeout,line=True)
        matching=[row for row in response.splitlines() if row.startswith(prefix)]
        if not matching or matching[-1][len(prefix):]!=b'0':
            raise RuntimeError('Command did not complete successfully: '+response.decode(errors='replace'))
        return response
    def upload(self,data):
        if len(data)!=BYTES: raise ValueError('Wrong frame size')
        crc=binascii.crc32(data)&0xffffffff
        self.port.write(f'load {crc:08X}\n'.encode()); self.port.flush()
        self.read_until(b'READY 9472',3)
        self.port.write(data); self.port.flush()
        response=self.read_until(b'DONE load result=0\r\n',5)
        if f'LOAD OK {crc:08X}\r\n'.encode() not in response:
            raise RuntimeError('Upload completion missing matching CRC acknowledgement')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest='operation',required=True)
    sub.add_parser('ports')
    q=sub.add_parser('pack'); q.add_argument('image',type=Path); q.add_argument('output',type=Path)
    q.add_argument('--method',choices=['gray','mono','bayer'],default='gray'); q.add_argument('--preview',type=Path)
    q=sub.add_parser('command'); q.add_argument('--port',required=True); q.add_argument('--log'); q.add_argument('--timeout',type=float,default=20); q.add_argument('command')
    q=sub.add_parser('upload'); q.add_argument('--port',required=True); q.add_argument('--log'); q.add_argument('image',type=Path)
    q.add_argument('--method',choices=['gray','mono','bayer'],default='gray')
    q.add_argument('--refresh',choices=['auto','gc4','clean','gu4','a2'])
    a=p.parse_args()
    if a.operation=='ports':
        from serial.tools import list_ports
        for port in list_ports.comports(): print(port.device,port.description,port.hwid)
    elif a.operation=='pack':
        data=pack(read_gray(a.image),a.method); a.output.parent.mkdir(parents=True,exist_ok=True); a.output.write_bytes(data)
        if a.preview: preview(data,a.preview)
        print(f'{len(data)} bytes; CRC32={binascii.crc32(data)&0xffffffff:08X}')
    else:
        with Client(a.port,a.log) as client:
            if a.operation=='command':
                if not 0<a.timeout<=300: raise ValueError('Command timeout must be 0 < seconds <= 300')
                client.command(a.command,a.timeout)
            else:
                client.upload(pack(read_gray(a.image),a.method))
                if a.refresh: client.command('refresh '+a.refresh)
            print(client.transcript.decode(errors='replace'))

if __name__=='__main__':
    try: main()
    except (ValueError,RuntimeError,TimeoutError,OSError) as exc:
        print(str(exc),file=sys.stderr); sys.exit(1)
