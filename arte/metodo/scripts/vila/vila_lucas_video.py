# Thief na vila que o Lucas gerou no Flow (vila_lucas.png = 1920x1072 px do jogo; ampliada 4x por Real-ESRGAN).
# Câmera segue o Thief com zoom K=2,5 (mesma escala do vídeo da praça). Anda, corre, para e respira.
# Sombra: silhueta do quadro achatada na direção das sombras da imagem (sol baixo, vindo da esquerda/cima).
import math, subprocess
from PIL import Image, ImageFilter
try:
    import psutil; psutil.Process().nice(psutil.BELOW_NORMAL_PRIORITY_CLASS)
except Exception: pass
V3 = "C:/dr-sprites/sprites"; AQUI = "C:/dr-sprites/vila"
W, H, FPS = 1920, 1080, 30
K = 2.0      # zoom do cenário (px de vídeo por px do jogo)
CK = K       # Thief na escala real do jogo (conferido em regua_tamanho.png: igual ao boneco original no mesmo trecho)
ESC = CK / 5.6; PE_Y = 470
MUNDO = Image.open(f"{AQUI}/vila_lucas_4x.png").convert("RGB")
G = MUNDO.width / 1920.0                       # px da imagem 4x por px do jogo
F = 1920 / 1080                                # grade medida na imagem a 1080 px de largura
rua = lambda x: (x * F, (0.945 * (x - 459) + 20) * F)  # eixo da rua principal (bordas medidas)
A, M, P = rua(560), rua(700), (825 * F, 365 * F)
B = ((825 - 175) * F, (365 + 175) * F)          # desce pela rua da diagonal esquerda
def carregar(p):
    im = Image.open(p).convert("RGBA"); return im.resize((round(im.width*ESC), round(im.height*ESC)), Image.LANCZOS)
SPR = {d: {"parado": [carregar(f"{V3}/{d}/parado_{i}.png") for i in (1, 2, 3, 2)],
           "andar": [carregar(f"{V3}/{d}/andar_{i}.png") for i in range(1, 7)],
           "correr": [carregar(f"{V3}/{d}/correr_{i}.png") for i in range(1, 7)]} for d in ("SE", "SW")}
SA, SB = 1.5, 0.47          # sombra: cada px de altura vira 1,5 px pra direita e 0,47 px pra baixo (medido nos postes da imagem)
_sombras = {}
def sombra(q):
    if id(q) in _sombras: return _sombras[id(q)]
    pe = PE_Y * ESC; hmax = pe
    ow, oh = int(q.width + SA * hmax + 20), int(pe + SB * hmax + 20)
    a = q.split()[3].point(lambda v: 255 if v > 90 else 0)
    # saída (X,Y) -> origem (x,y): h = (Y-pe)/SB ; y = pe - h ; x = X - SA*h
    data = (1, -SA / SB, SA / SB * pe, 0, -1 / SB, (1 + 1 / SB) * pe)
    s = a.transform((ow, oh), Image.AFFINE, data, resample=Image.BILINEAR)
    s = s.filter(ImageFilter.GaussianBlur(2.5)).point(lambda v: int(v * 0.58))
    preto = Image.new("RGBA", (ow, oh), (10, 8, 6, 0)); preto.putalpha(s)
    _sombras[id(q)] = preto; return preto
v_andar, v_correr = 13 * 10 * CK / K, 20 * 10 * CK / K   # px do jogo por segundo (001AniTable.bin)
# contato com o chão (ambient occlusion dos pés): mancha escura, achatada e desfocada sob os pés
AOW, AOH = int(30 * CK), int(10 * CK)
ao = Image.new("L", (AOW + 40, AOH + 40), 0)
from PIL import ImageDraw
ImageDraw.Draw(ao).ellipse((20, 20, 20 + AOW, 20 + AOH), fill=150)
ao = ao.filter(ImageFilter.GaussianBlur(5)); AO = Image.new("RGBA", ao.size, (8, 6, 4, 0)); AO.putalpha(ao)
seg = [("parado", 1.2, A, A, "SE"), ("andar", math.dist(A, M) / v_andar, A, M, "SE"), ("correr", math.dist(M, P) / v_correr, M, P, "SE"),
       ("andar", math.dist(P, B) / v_andar, P, B, "SW"), ("parado", 3.5, B, B, "SW")]
nf = int(sum(s[1] for s in seg) * FPS)
VW, VH = W / K, H / K                          # janela da câmera em px do jogo
ff = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-",
                       "-c:v", "libx264", "-preset", "medium", "-crf", "18", "-pix_fmt", "yuv420p", "-threads", "2", "-movflags", "+faststart",
                       f"{AQUI}/thief_vila_lucas.mp4"], stdin=subprocess.PIPE)
cam = None
for k in range(nf):
    tt = k / FPS; t0 = 0
    for i, (acao, dur, a, b, d) in enumerate(seg):
        if tt < t0 + dur or i == len(seg) - 1: break
        t0 += dur
    u = min((tt - t0) / dur, 1); pos = (a[0] + (b[0]-a[0])*u, a[1] + (b[1]-a[1])*u)
    q = SPR[d][acao][int((tt - t0) / 0.26) % 4] if acao == "parado" else SPR[d][acao][int((tt - t0) * 10) % 6]
    alvo = (pos[0], pos[1] - 30)
    cam = alvo if cam is None else (cam[0] + (alvo[0]-cam[0])*0.08, cam[1] + (alvo[1]-cam[1])*0.08)
    cx = min(max(cam[0], VW/2), 1920 - VW/2); cy = min(max(cam[1], VH/2), MUNDO.height / G - VH/2)
    x0, y0 = cx - VW/2, cy - VH/2
    fr = MUNDO.crop((round(x0*G), round(y0*G), round((x0+VW)*G), round((y0+VH)*G))).resize((W, H), Image.BICUBIC).convert("RGBA")
    px, py = (pos[0] - x0) * K, (pos[1] - y0) * K
    fr.alpha_composite(AO, (int(px - AO.width/2), int(py - AO.height/2)))
    s = sombra(q); fr.alpha_composite(s, (int(px - q.width/2), int(py - PE_Y*ESC)))
    fr.alpha_composite(q, (int(px - q.width/2), int(py - PE_Y*ESC)))
    ff.stdin.write(fr.convert("RGB").tobytes())
ff.stdin.close(); ff.wait(); print("ok", nf)
