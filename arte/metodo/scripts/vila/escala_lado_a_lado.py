# Régua de tamanho: mesmo trecho e mesma escala, original (mosaico dos prints + boneco do jogo) x HD (vila do Lucas + Thief HD).
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, r"C:/dr-sprites/jogo")
from spr_decode import load_spr, decode_frame
B_, O_ = load_spr(r"C:/DragonRaja/Char/001000.spr")
DIRS = ["S","SW","W","NW","N","NE","E","SE"]
MOS = "C:/dr-sprites/vila/helternt_mosaico_v2_1a4.png"
MX0, MY0 = 2053 - 240, 640 - 135               # canto do trecho no mosaico (1 px = 4 px do jogo)
HD = Image.open("C:/dr-sprites/vila/vila_lucas_4x.png").convert("RGB"); G = HD.width / 1920
S = 1.25                                        # px da imagem por px do jogo
cx, cy = 1467, 649                              # cruzamento, em px do jogo
VW, VH = 768, 432
x0, y0 = cx - VW/2, cy - VH/2
mos = Image.open(MOS).convert("RGB").crop((int(MX0 + x0/4), int(MY0 + y0/4), int(MX0 + (x0+VW)/4), int(MY0 + (y0+VH)/4))).resize((int(VW*S), int(VH*S)), Image.LANCZOS).convert("RGBA")
hd = HD.crop((int(x0*G), int(y0*G), int((x0+VW)*G), int((y0+VH)*G))).resize((int(VW*S), int(VH*S)), Image.LANCZOS).convert("RGBA")
# boneco original do jogo (corpo do Char/001000.spr, SE parado), pés no cruzamento
fr = decode_frame(B_, int(O_[DIRS.index("SE")*75 + 1]))[0]
sp = Image.fromarray(fr.astype(np.uint8), "RGBA"); bb = sp.getbbox(); sp = sp.crop(bb)
sp = sp.resize((round(sp.width*S), round(sp.height*S)), Image.NEAREST)
pe = (int(VW/2*S), int(VH/2*S))
mos.alpha_composite(sp, (pe[0] - sp.width//2, pe[1] - sp.height))
# Thief HD na mesma escala (quadros têm o boneco 5,6x o sprite)
q = Image.open("C:/dr-sprites/sprites/SE/parado_1.png").convert("RGBA"); q = q.crop(q.getbbox())
q = q.resize((round(q.width*S/5.6), round(q.height*S/5.6)), Image.LANCZOS)
hd.alpha_composite(q, (pe[0] - q.width//2, pe[1] - q.height))
f = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 26)
out = Image.new("RGB", (mos.width, mos.height*2 + 8), (20, 20, 20))
out.paste(mos.convert("RGB"), (0, 0)); out.paste(hd.convert("RGB"), (0, mos.height + 8))
d = ImageDraw.Draw(out)
d.text((14, 10), f"ORIGINAL: mapa dos prints + boneco do jogo ({bb[3]-bb[1]} px de altura)", font=f, fill=(255, 230, 150), stroke_width=3, stroke_fill=(0, 0, 0))
d.text((14, mos.height + 18), "HD: vila do Lucas + Thief na mesma escala", font=f, fill=(255, 230, 150), stroke_width=3, stroke_fill=(0, 0, 0))
out.save("C:/dr-sprites/vila/regua_tamanho.png"); print("ok", bb, q.size, sp.size)
