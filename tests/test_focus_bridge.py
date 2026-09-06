import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('bridge',Path(__file__).resolve().parents[1]/'contrib/gnome/spnav-focus-gnome.py')
m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
class Client:
 def __init__(self):self.ids=[];self.closed=0;self.fail=False
 def publish(self,id):
  if self.fail:raise OSError('disconnected')
  self.ids.append(id)
 def close(self):self.closed+=1
class Tests(unittest.TestCase):
 def test_focus_and_unavailable(self):
  c=Client();b=m.Forwarder(c)
  b.update(True,'blender');b.update(True,'');b.update(False,'stale')
  self.assertEqual(c.ids,['blender','']);self.assertEqual(c.closed,1)
 def test_disconnect_recovery(self):
  c=Client();b=m.Forwarder(c);c.fail=True;b.update(True,'cad')
  self.assertEqual(c.closed,1)
  c.fail=False;b.update(True,'cad');self.assertEqual(c.ids,['cad'])
 def test_signal_handlers_without_deprecation(self):
  import warnings
  from gi.repository import Gio, GLib  # Match the helper runtime import order.
  with warnings.catch_warnings():
   warnings.simplefilter('error')
   ids=m.install_signal_handlers(GLib.MainLoop())
  self.assertEqual(len(ids),2)
  for id in ids:GLib.source_remove(id)
if __name__=='__main__':unittest.main()
