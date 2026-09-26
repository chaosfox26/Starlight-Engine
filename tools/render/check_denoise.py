"""Offline image/guide validation. GPU readback occurs only in this test program."""
import argparse, ctypes as c, json, math, struct, os
from pathlib import Path
import numpy as np
from PIL import Image

root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir',type=Path,default=root/'out/audit-engine')
parser.add_argument('--config',default='Release')
parser.add_argument('--packet',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
os.environ['STARLIGHT_D3D_DEBUG']='1'
os.environ['STARLIGHT_NATIVE_LOG']=str(out/'debug.log')
u=c.WinDLL('user32',use_last_error=True)
u.CreateWindowExW.argtypes=[c.c_uint,c.c_wchar_p,c.c_wchar_p,c.c_uint,c.c_int,c.c_int,c.c_int,c.c_int,c.c_void_p,c.c_void_p,c.c_void_p,c.c_void_p];u.CreateWindowExW.restype=c.c_void_p
u.DestroyWindow.argtypes=[c.c_void_p]
d=c.CDLL(str(args.build_dir.resolve()/args.config/'Starlight.Viewport.dll'))
def api(name,result,*args):
 f=getattr(d,name);f.restype=result;f.argtypes=list(args);return f
ptr=c.c_void_p;I=c.c_int;F=c.c_float
create=api('sl_create',ptr,ptr,I,I);scene=api('sl_scene',I,ptr,ptr,I);frame=api('sl_frame',I,ptr,c.c_double,F)
camera=api('sl_camera',None,ptr,F,F,F,F,F,F);destroy=api('sl_destroy',None,ptr)
rt=api('sl_rt',None,ptr,I,I);freeze=api('sl_freeze',None,ptr,I);mode=api('sl_denoiser',None,ptr,I)
capture=api('sl_denoise_capture',I,ptr,I,c.c_wchar_p);history=api('sl_denoise_history_index',I,ptr)
rr=api('sl_rr_active',I,ptr);error=api('sl_error',c.c_char_p);shot=api('sl_shot',None,ptr,c.c_wchar_p)
checks=[];metrics={}
def require(ok,label):
 if not ok:raise AssertionError(label)
 checks.append(label)
def dump(ctx,index,label):
 path=out/(label+'.bin');require(capture(ctx,index,str(path))==1,'capture '+label)
 data=path.read_bytes();w,h,fmt,row=struct.unpack_from('<4I',data);dtype=np.float32 if fmt in (2,41) else np.float16;channels=row//w//np.dtype(dtype).itemsize
 a=np.frombuffer(data,offset=16,dtype=dtype).reshape(h,w,channels).astype(np.float32)
 require(np.isfinite(a).all(),'finite '+label);return a
def advance(ctx,n,seconds=0):
 for _ in range(n):require(frame(ctx,seconds,13)==1,'frame')
def luminance(a):return a[...,:3]@np.array([.2126,.7152,.0722])

packet=args.packet.read_bytes()
def prop_offset(data):
 version,count=struct.unpack_from('<2I',data);offset=8
 lights=struct.unpack_from('<I',data,offset)[0];offset+=4+lights*48
 offset+=16;smoke=struct.unpack_from('<I',data,offset)[0];offset+=4+smoke*16+12
 if version>=0x37564c53:offset+=4
 for i in range(count):
  start=offset;offset+=40;routes=struct.unpack_from('<I',data,offset)[0];offset+=4+routes*8+64
  vertices=struct.unpack_from('<I',data,offset)[0];offset+=4;kind=struct.unpack_from('<f',data,offset+36)[0]
  if kind==10:return start+24,i
  offset+=vertices*40;indices=struct.unpack_from('<I',data,offset)[0];offset+=4+indices*4
 raise ValueError('Chaos rock missing from test packet')
def preference_offset(data):
 lights=struct.unpack_from('<I',data,8)[0];offset=12+lights*48+16
 smoke=struct.unpack_from('<I',data,offset)[0]
 return offset+4+smoke*16+12
result={'success':False}
try:
 for m in (0,1):
  hwnd=u.CreateWindowExW(0,'STATIC','Starlight denoiser validation',0,0,0,640,360,None,None,None,None)
  ctx=create(hwnd,640,360);require(bool(ctx),'create context')
  try:
   buf=c.create_string_buffer(packet);require(scene(ctx,buf,len(packet))==1,'scene V7 accepted');rt(ctx,4,3);mode(ctx,m);freeze(ctx,1);camera(ctx,0,9.36,13,0,0,0);advance(ctx,72)
   raw=[];filtered=[]
   for i in range(8):
    advance(ctx,2);raw.append(dump(ctx,0,f'{m}-raw-{i}'));filtered.append(dump(ctx,23,f'{m}-filtered-{i}'))
   normals=dump(ctx,1,f'{m}-normals');emission=dump(ctx,6,f'{m}-emission');depth=dump(ctx,4,f'{m}-depth')
   mask=(normals[...,1]>.85)&(depth[...,0]<399)&(luminance(emission)<.01)
   require(mask.sum()>100,'ground mask populated')
   rv=np.var(np.stack([luminance(a) for a in raw]),axis=0);fv=np.var(np.stack([luminance(a) for a in filtered]),axis=0)
   ratio=float(fv[mask].mean()/max(rv[mask].mean(),1e-10));metrics[f'{m}-groundTemporalVarianceRatio']=ratio
   require(ratio<.8,f'mode {m} reduces static ground temporal variance')
   require(bool(rr(ctx))==bool(m),f'mode {m} actual RR telemetry')
   # Camera motion: verify native per-pixel history rejection against the actual prior guide.
   oldGuide=dump(ctx,history(ctx)+3,'old-guide') if m==0 else None
   oldId=dump(ctx,history(ctx)+1,'old-id')[...,3] if m==0 else None
   camera(ctx,1.5,9.36,13,1.5,0,0);advance(ctx,1)
   motion=dump(ctx,5,f'{m}-motion');moving=dump(ctx,23,f'{m}-camera-output')
   require(float(np.max(np.abs(motion[...,:2])))>1,'camera produces nonzero pixel motion')
   if m==0:
    h,w=motion.shape[:2];yy,xx=np.indices((h,w));qx=np.floor(xx+motion[...,0]+.5).astype(int);qy=np.floor(yy+motion[...,1]+.5).astype(int)
    inside=(qx>=0)&(qx<w)&(qy>=0)&(qy<h);qx=np.clip(qx,0,w-1);qy=np.clip(qy,0,h-1)
    currentNormal=dump(ctx,1,'moving-normal')[...,:3];g=oldGuide[qy,qx]
    valid=inside&(motion[...,3]>.5)&(oldId[qy,qx]==motion[...,3])&(abs(g[...,3]-motion[...,2])<np.maximum(.025,motion[...,2]*.012))&(((g[...,:3]*currentNormal).sum(-1)>.92)|((depth[...,0]>=399)&(g[...,3]>=399)))
    lengths=dump(ctx,history(ctx),'moving-history')[...,3];invalid=~valid
    require(invalid.sum()>100,'camera reveals rejected history pixels')
    ratio=float(np.mean(lengths[invalid]==1));metrics['native-disocclusionRejectedFraction']=ratio
    require(ratio>.995,'native invalid history starts at length one')
   # Actual animated lighting + camera sweep, with no frame-history reset API calls.
   freeze(ctx,0)
   for i in range(40):camera(ctx,1.5+math.sin(i*.05),9.36,13,1.5,0,0);advance(ctx,1,i/60)
   dump(ctx,23,f'{m}-live-output');shot(ctx,str(out/f'{m}-live.bmp'));advance(ctx,1)
   require(bool(rr(ctx))==bool(m),'live sequence retains requested denoiser')
   # Large cut invalidates temporal history rather than dragging previous lighting across views.
   camera(ctx,-7,6,8,-4,0,0);advance(ctx,1);dump(ctx,23,f'{m}-cut-output')
   if m==0:require(np.max(dump(ctx,history(ctx),'cut-history')[...,3])==1,'camera cut resets native history')
   # Translate a real prop through scene packets, preserving its geometry/instance identity.
   camera(ctx,0,9.36,13,0,0,0);freeze(ctx,1);advance(ctx,5)
   offset,objectId=prop_offset(packet);x=struct.unpack_from('<f',packet,offset)[0]
   for step in range(6):
    changed=bytearray(packet);struct.pack_into('<f',changed,offset,x+(step+1)*.1);struct.pack_into('<i',changed,preference_offset(packet),m);changedBuffer=c.create_string_buffer(bytes(changed))
    require(scene(ctx,changedBuffer,len(changed))==1,'moving prop packet accepted');advance(ctx,1)
   propMotion=dump(ctx,5,f'{m}-prop-motion')
   encodedId=np.array([0x3c00+objectId],dtype=np.uint16).view(np.float16)[0]
   propMask=propMotion[...,3]==encodedId
   require(propMask.sum()>8,'moving prop guide pixels found')
   require(float(np.linalg.norm(propMotion[propMask,:2],axis=-1).mean())>.1,'dynamic transform produces object motion vectors')
   dump(ctx,23,f'{m}-prop-output')
   # Toggle paths without disabling rays or reallocating the context.
   mode(ctx,1-m);advance(ctx,4);require(bool(rr(ctx))!=bool(m),'runtime denoiser switch succeeds')
  finally:destroy(ctx);u.DestroyWindow(hwnd)
 result={'success':True,'metrics':metrics,'checks':list(dict.fromkeys(checks)),'scope':'640x360 fixed scene, image variance, camera reprojection/disocclusion/cut, live shader animation and runtime switching; not a universal zero-ghosting claim'}
except Exception as exc:
 result={'success':False,'error':str(exc),'nativeError':str(error()),'metrics':metrics,'checks':list(dict.fromkeys(checks))}
finally:
 (out/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
raise SystemExit(0 if result['success'] else 1)
