# Trecho da vila de Helternt (mapa original, 1:1) -> remaster HD estilo Unreal no Flow, mesmo layout e mesma câmera.
import sys, os, json
sys.path.insert(0, "C:/dr-sprites/sprites")
import importlib.util
spec = importlib.util.spec_from_file_location("pl", "C:/dr-sprites/sprites/pipeline.py")
src = open("C:/dr-sprites/sprites/pipeline.py", encoding="utf-8").read().split("sys.path.insert(0, r\"C:/dr-sprites/jogo\")")[0]
exec(src)
from PIL import Image
AQUI = "C:/dr-sprites/vila"
MOS = "C:/dr-sprites/vila/helternt_mosaico_v2_1a4.png"   # mosaico dos prints, 1 px = 4 px do jogo
X0, Y0, LW, LH = (2053 - 240, 640 - 135, 480, 270) if "cruzamento" in sys.argv else (1660 - 96, 1270 - 54, 192, 108) if "perto" in sys.argv else (1733 - 160, 1253 - 90, 320, 180)   # perto: 768x432 px do jogo (escala do vídeo da praça)
TAG = "_cruzamento" if "cruzamento" in sys.argv else ("_perto" if "perto" in sys.argv else "")
orig = f"{AQUI}/vila_mosaico_original{TAG}.png"
if not os.path.exists(orig):
    Image.open(MOS).convert("RGB").crop((X0, Y0, X0 + LW, Y0 + LH)).resize((1376, 774), Image.LANCZOS).save(orig)
ref = subir(orig)
P = ("Image 1 is a low-resolution screenshot of a village from a classic 2D isometric RPG (military oblique projection, camera looking down at about 60 degrees). "
     "Remaster it as a photorealistic Unreal Engine 5 render of the SAME place: keep EXACTLY the same layout and composition, every house, roof, "
     "wall, street lamp, market tent, tree, the paved stone road, the cobblestone areas, dirt borders and grass patches in the same position, same size, same shape and the same "
     "camera angle and projection. Medieval fantasy European village. Rich detail: individual roof tiles, worn stone slabs, moss between cobbles, "
     "grass blades, small pebbles, warm afternoon sun from the upper left with soft shadows. No people, no characters, no text, no UI.")
if "cruzamento" in sys.argv:   # prompt do Lucas puro (23:37: a trava de layout segurava demais)
    P = ("Recreate this dark fantasy mmorpg village on unreal engine 3d render high quality. Maintain same perspective, same building architecture. "
         "Improve quality of materials and textures, ambient illumination, shadows and ambient occlusion.")
elif "perto" in sys.argv:   # prompt do Lucas (18/11) + travas de layout
    P = ("Recreate this dark fantasy mmorpg village on unreal engine 3d render high quality. Maintain same perspective, same building architecture. "
         "Improve quality of materials and textures, ambient illumination, shadows and ambient occlusion. Keep every element exactly in the same place and size "
         "(road, street lamp, market tent, house, grass). No people, no characters, no text.")
for k in range(int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 2):
    out = f"{AQUI}/vila_hd{TAG}_{k+1}.png"
    if os.path.exists(out): continue
    mid = gerar(P, [ref], out); print(out, mid, flush=True)
