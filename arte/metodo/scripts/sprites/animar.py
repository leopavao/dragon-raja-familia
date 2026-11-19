# GIFs: por direção (parado, andando, os dois) + TODAS lado a lado + volta completa (vira e anda em cada direção)
import os
from PIL import Image
DIR = ["S","SW","W","NW","N","NE","E","SE"]; FUNDO = (46, 52, 46, 255); ESC = 1.0
def bg(f):
    b = Image.new("RGBA", f.size, FUNDO); b.alpha_composite(f); b = b.convert("RGB")
    return b.resize((int(b.width*ESC), int(b.height*ESC)), Image.LANCZOS)
def salvar(seq, nome):
    seq[0][0].save(nome, save_all=True, append_images=[s for s, _ in seq[1:]], duration=[t for _, t in seq], loop=0)
os.makedirs("gifs", exist_ok=True)
prontas = [d for d in DIR if os.path.exists(f"{d}/andar_6.png")]
P = {d: [Image.open(f"{d}/parado_{i}.png") for i in (1,2,3,2)] for d in prontas}
A = {d: [Image.open(f"{d}/andar_{i}.png") for i in range(1,7)] for d in prontas}
for d in prontas:
    salvar([(bg(f), 260) for f in P[d]], f"gifs/{d}_parado.gif")
    salvar([(bg(f), 120) for f in A[d]], f"gifs/{d}_andando.gif")
    salvar([(bg(f), 260) for f in P[d]*3] + [(bg(f), 120) for f in A[d]*4], f"gifs/{d}_parado_e_andando.gif")
def lado(seqs, k):
    W, H = seqs[0][0].size; f = Image.new("RGBA", (W*len(seqs), H), FUNDO)
    for i, s in enumerate(seqs): f.alpha_composite(s[k % len(s)], (i*W, 0))
    return f
if prontas:
    m = lambda im: im.resize((int(im.width*0.6), int(im.height*0.6)), Image.LANCZOS)
    salvar([(m(bg(lado([P[d] for d in prontas], k))), 260) for k in range(8)], "gifs/TODAS_parado.gif")
    salvar([(m(bg(lado([A[d] for d in prontas], k))), 120) for k in range(12)], "gifs/TODAS_andando.gif")
    volta = []
    for d in prontas:
        volta += [(bg(f), 220) for f in P[d][:2]] + [(bg(f), 120) for f in A[d]*3]
    salvar(volta, "gifs/VOLTA_COMPLETA.gif")
print("gifs:", prontas)
