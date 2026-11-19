# Vídeo 1920x1080 30fps: o Thief anda do ponto A ao ponto B em cada uma das 8 direções (octógono fechado).
# Escreve progresso "N/M frames X fps" no log (Painel de Render). Prioridade baixa, poucos núcleos.
import os, sys, math, time, subprocess
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
try:
    import psutil; psutil.Process().nice(psutil.BELOW_NORMAL_PRIORITY_CLASS)
except Exception: pass
ZOOM = "zoom" in sys.argv[2:]           # zoom: câmera perto, cada direção num trecho próprio no centro da tela
V3 = "C:/dr-sprites/sprites"; OUT = f"C:/dr-sprites/video/thief_{sys.argv[1] if len(sys.argv) > 1 else 'andar'}{'_zoom' if ZOOM else ''}_8_direcoes.mp4"
LOG = "C:/dr-sprites/video/render.log"
W, H, FPS = 1920, 1080, 30
K = 5.0 if ZOOM else 2.5                                  # px de vídeo por px do jogo (chão e personagem na MESMA escala)
ESC = K / 5.6; PE_Y = 470          # quadros têm o boneco 5,6x maior que o sprite original
ORDEM = ["E", "SE", "S", "SW", "W", "NW", "N", "NE"]
NOME = {"S": "Sul · de frente", "SW": "Sudoeste", "W": "Oeste", "NW": "Noroeste", "N": "Norte · de costas", "NE": "Nordeste", "E": "Leste", "SE": "Sudeste"}
VET = {"S": (0, 1), "SW": (-1, 1), "W": (-1, 0), "NW": (-1, -1), "N": (0, -1), "NE": (1, -1), "E": (1, 0), "SE": (1, 1)}
ANIM_FPS = 10                            # quadros de sprite por segundo
PASSO = {"andar": 13, "correr": 20}      # px do jogo por quadro de sprite (001AniTable.bin: 6/7 e 10 px por tique, 2 tiques por quadro)
MODO = sys.argv[1] if len(sys.argv) > 1 else "andar"
VEL = PASSO[MODO] * ANIM_FPS * K; WALK_FPS = ANIM_FPS
L = (3 if ZOOM else 4) * 32 * K; T_PARADO = 1.2 if ZOOM else 0.9
def carregar(p):
    im = Image.open(p).convert("RGBA"); return im.resize((round(im.width*ESC), round(im.height*ESC)), Image.LANCZOS)
SPR = {d: {"parado": [carregar(f"{V3}/{d}/parado_{i}.png") for i in (1, 2, 3, 2)],
           "andar": [carregar(f"{V3}/{d}/{MODO}_{i}.png") for i in range(1, 7)]} for d in ORDEM}
Image.MAX_IMAGE_PIXELS = None
CX, CY = 233*32 + 400, 106*32 + 60       # praça calçada de Helternt (mapa decodificado do jogo)
vw, vh = W / K, H / K
fundo = Image.open("C:/dr-sprites/video/chao_helternt_praca.png").convert("RGB")
if ZOOM:   # o recorte cobre 768x432 px do jogo (K=2,5); no zoom a tela mostra W/K x H/K px do jogo, do meio
    cw, ch = W / K, H / K; fundo = fundo.crop((round(384 - cw/2), round(216 - ch/2), round(384 + cw/2), round(216 + ch/2)))
fundo = fundo.resize((W, H), Image.BICUBIC).convert("RGBA")   # recorte 1:1 do helternt_FINAL.jpg
f_titulo = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 46); f_sub = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 28)
f_marca = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 30)
# octógono: pontos
lado = L * (1 + math.sqrt(2)); x0, y0 = W/2 - L/2, H/2 + 40 - lado/2
pts = [(x0, y0)]
for d in ORDEM:
    vx, vy = VET[d]; n = math.hypot(vx, vy); pts.append((pts[-1][0] + L*vx/n, pts[-1][1] + L*vy/n))
if ZOOM:   # cada direção: A e B simétricos em volta do meio da tela (pés), pra caber tudo
    cx0, cy0 = W/2, H/2 + 200; PT = []
    for d in ORDEM:
        vx, vy = VET[d]; n = math.hypot(vx, vy); PT.append(((cx0 - L*vx/n/2, cy0 - L*vy/n/2), (cx0 + L*vx/n/2, cy0 + L*vy/n/2)))
# linha do tempo: para cada direção, parado (já virado) e depois anda
seg = []; t = 0.6
for i, d in enumerate(ORDEM):
    dur = L / VEL
    seg.append((d, i, t, t + T_PARADO, t + T_PARADO + dur)); t += T_PARADO + dur
TOTAL = t + 1.6
if ZOOM: TOTAL -= 0.8
SW_, SH_ = int(110 * K / 2.5), int(36 * K / 2.5)
sombra = Image.new("RGBA", (SW_, SH_), (0, 0, 0, 0)); ImageDraw.Draw(sombra).ellipse((5, 5, SW_ - 5, SH_ - 5), fill=(0, 0, 0, 110))
sombra = sombra.filter(ImageFilter.GaussianBlur(6))
def marca(dr, p, txt, ativo):
    x, y = p; r = 16 * (1.8 if ZOOM else 1); cor = (255, 214, 120, 235) if ativo else (200, 200, 200, 110)
    dr.ellipse((x-r, y-r*0.55, x+r, y+r*0.55), outline=cor, width=3)
    dr.text((x, y + 22), txt, font=f_marca, fill=cor, anchor="mt")
nf = int(TOTAL * FPS)
ff = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-",
                       "-c:v", "libx264", "-preset", "medium", "-crf", "18", "-pix_fmt", "yuv420p", "-threads", "2", "-movflags", "+faststart", OUT],
                      stdin=subprocess.PIPE)
log = open(LOG, "w"); t0 = time.time()
for k in range(nf):
    tt = k / FPS
    atual = seg[0]
    for s in seg:
        if tt >= s[2]: atual = s
    d, i, ti, tw, tf = atual
    a, b = PT[i] if ZOOM else (pts[i], pts[i+1])
    if tt < tw:   # parado no ponto A, já virado pra direção
        pos = a; quadro = SPR[d]["parado"][int((tt - ti) / 0.26) % 4]
    elif tt < tf:
        u = (tt - tw) / (tf - tw); pos = (a[0] + (b[0]-a[0])*u, a[1] + (b[1]-a[1])*u)
        quadro = SPR[d]["andar"][int((tt - tw) * WALK_FPS) % 6]
    else:         # chegou no fim do último trecho
        pos = b; quadro = SPR[d]["parado"][int((tt - tf) / 0.26) % 4]
    fr = fundo.copy(); dr = ImageDraw.Draw(fr)
    andando = tw <= tt < tf
    marca(dr, a, "A", True); marca(dr, b, "B", True)
    fr.alpha_composite(sombra, (int(pos[0] - SW_/2), int(pos[1] - SH_/2)))
    fr.alpha_composite(quadro, (int(pos[0] - quadro.width/2), int(pos[1] - PE_Y*ESC)))
    dr = ImageDraw.Draw(fr)
    dr.text((70, 60), f"{i+1}/8  {NOME[d]}", font=f_titulo, fill=(255, 236, 200, 255), stroke_width=3, stroke_fill=(0, 0, 0, 200))
    dr.text((72, 120), ("correndo" if MODO == "correr" else "andando") + " de A até B" if andando else "parado, respirando", font=f_sub, fill=(230, 230, 230, 255), stroke_width=2, stroke_fill=(0, 0, 0, 200))
    ff.stdin.write(fr.convert("RGB").tobytes())
    if k % 15 == 0 or k == nf - 1:
        log.write(f"{k+1}/{nf} frames {((k+1)/max(time.time()-t0, 1e-3)):.1f} fps\n"); log.flush()
ff.stdin.close(); ff.wait()
log.write(f"fim ({nf} frames in {time.time()-t0:.1f}s)\n"); log.close(); print("ok", OUT, nf)
