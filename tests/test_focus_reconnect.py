# Run against the freshly built sibling library; uses a fake local daemon.
import ctypes, importlib.util, os, socket, struct, tempfile, threading
from pathlib import Path
base=Path(__file__).resolve().parents[1]
helper=base/'contrib/gnome/spnav-focus-gnome.py'
spec=importlib.util.spec_from_file_location('helper',helper);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def recv_all(s,n):
 data=b''
 while len(data)<n:
  piece=s.recv(n-len(data))
  if not piece:raise EOFError()
  data+=piece
 return data
with tempfile.TemporaryDirectory() as d:
 path=d+'/spnav.sock';listener=socket.socket(socket.AF_UNIX);listener.bind(path);listener.listen();listener.settimeout(5)
 os.environ['SPNAV_SOCKET']=path
 closed=threading.Event();errors=[];seen=[]
 def server():
  try:
   for attempt in range(2):
    conn,_=listener.accept();conn.settimeout(5)
    with conn:
     hello=recv_all(conn,4);conn.sendall(hello)
     for expected in [0x1003,0x3f07]:
      raw=recv_all(conn,32);values=list(struct.unpack('=8i',raw));assert values[0]&0xffff==expected
      if expected==0x3f07:seen.append(raw[4:28].split(b'\0')[0].decode())
      values[7]=0;conn.sendall(struct.pack('=8i',*values))
    if attempt==0:closed.set()
  except BaseException as e:errors.append(e);closed.set()
 thread=threading.Thread(target=server,daemon=True);thread.start()
 c=object.__new__(m.SpnavClient);c.lib=ctypes.CDLL(os.environ.get('SPNAV_TEST_LIBRARY', str(base.parent/'libspnav/libspnav.so.0.4')));c.lib.spnav_set_focus.argtypes=[ctypes.c_char_p];c.connected=False
 f=m.Forwarder(c);f.update(True,'blender.desktop');assert closed.wait(5)
 f.update(True,'blender.desktop');assert not c.connected
 f.update(True,'blender.desktop');assert c.connected
 c.close();thread.join(5);listener.close()
 assert not thread.is_alive() and not errors,(errors,thread.is_alive())
 assert seen==['blender.desktop','blender.desktop'],seen
 print('Real library + helper reconnect after daemon disconnect: PASS')
