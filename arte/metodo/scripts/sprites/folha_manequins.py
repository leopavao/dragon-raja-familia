# Folha 16:9 com N quadros da mesma direção, em colunas iguais, todos na mesma linha de chão
# (alinhados pela âncora do jogo, então o sobe-e-desce do passo fica preservado).
import sys, json
from PIL import Image
sys.path.insert(0, r"C:/dr-sprites/jogo")
from spr_decode import load_spr, decode_frame
DIR = ["S","SW","W","NW","N","NE","E","SE"]
d, quadros, saida = sys.argv[1], [int(x) for x in sys.argv[2].split(",")], sys.argv[3]
b, offs = load_spr(r"C:/DragonRaja/Char/001000.spr")
fr = [decode_frame(b, int(offs[DIR.index(d)*75+q])) for q in quadros]
L = max(ax for _,ax,_ in fr); R = max(a.shape[1]-ax for a,ax,_ in fr)
T = max(ay for _,_,ay in fr); B = max(a.shape[0]-ay for a,_,ay in fr)
W, H = 1920, 1080; col = W // len(fr)
esc = float(sys.argv[4]) if len(sys.argv) > 4 else min((col*0.82)/(L+R), (H*0.80)/(T+B))
chao = int(H*0.90)
out = Image.new("RGB", (W, H), (200, 200, 200))
info = []
import numpy as np
dxm = float(np.median([(np.nonzero(a[..., 3].max(0))[0].min() + np.nonzero(a[..., 3].max(0))[0].max()) / 2 - ax for a, ax, _ in fr]))
for i, (a, ax, ay) in enumerate(fr):
    im = Image.fromarray(a); im = im.resize((round(im.width*esc), round(im.height*esc)), Image.LANCZOS)
    x = int(col*i + col/2 - (ax + dxm)*esc); y = int(chao - ay*esc)
    out.paste(im, (x, y), im); info.append({"quadro": quadros[i], "coluna": i})
out.save(saida); json.dump({"dir": d, "quadros": quadros, "col": col, "chao": chao, "esc": esc}, open(saida[:-4]+".json","w"))
print(saida, round(esc,1))
