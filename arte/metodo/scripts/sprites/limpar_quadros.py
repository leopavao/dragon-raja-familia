# Limpa bolsões de fundo nos quadros finais: mancha cinza, lisa, perto da cor do fundo da folha.
# --ver: salva folha com as marcas em vermelho. Sem --ver: apaga e guarda o original em _v1.
import sys, os, glob
import numpy as np, cv2
ver = "--ver" in sys.argv
def cor_fundo(d, acao):
    bgr = cv2.imread(f"{d}/{acao}_thief.png"); h, w = bgr.shape[:2]
    borda = np.concatenate([bgr[:20].reshape(-1, 3), bgr[-20:].reshape(-1, 3), bgr[:, :20].reshape(-1, 3), bgr[:, -20:].reshape(-1, 3)])
    return np.median(borda, axis=0)
def bolsoes(rgba, cf):
    bgr = rgba[..., :3]; al = rgba[..., 3]
    g = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY).astype(np.float32)
    m = cv2.blur(g, (3, 3)); dv = np.sqrt(np.maximum(cv2.blur(g*g, (3, 3)) - m*m, 0))
    sat = cv2.cvtColor(bgr, cv2.COLOR_BGR2HSV)[..., 1]
    perto = np.abs(bgr.astype(np.float32) - cf).max(axis=2) < 32
    cand = ((al > 0) & (sat < 34) & (dv < 9) & perto).astype(np.uint8)
    k, lab, st, _ = cv2.connectedComponentsWithStats(cand, 8)
    out = np.zeros(al.shape, bool)
    ys = np.nonzero(al.max(1) > 0)[0]; topo, base = ys.min(), ys.max()
    limite = topo + 0.30 * (base - topo)        # só cabeça, chapéu, borla e flechas
    for i in range(1, k):
        cy = st[i, 1] + st[i, 3] / 2
        if st[i, cv2.CC_STAT_AREA] >= 6 and cy < limite: out |= lab == i
    return cv2.dilate(out.astype(np.uint8), np.ones((3, 3), np.uint8)) > 0
D = [a for a in sys.argv[1:] if not a.startswith("--")]
folha = []
for d in D:
    for acao, n in [x for x in (("parado", 3), ("andar", 6), ("correr", 6)) if os.path.exists(f"{d}/{x[0]}_1.png")]:
        cf = cor_fundo(d, acao)
        for i in range(1, n + 1):
            p = f"{d}/{acao}_{i}.png"; v1 = p.replace(".png", "_v1.png")
            rgba = cv2.imread(p, cv2.IMREAD_UNCHANGED)
            b = bolsoes(rgba, cf)
            if ver:
                v = rgba.copy(); v[b] = (0, 0, 255, 255); folha.append(v)
            else:
                pass
                rgba[b] = 0; cv2.imwrite(p, rgba)
        print(d, acao, flush=True)
if ver:
    W, H = folha[0].shape[1], folha[0].shape[0]; cols = 9
    s = np.full((H * ((len(folha)+cols-1)//cols), W * cols, 4), (46, 52, 46, 255), np.uint8)
    for j, f in enumerate(folha):
        y, x = (j // cols) * H, (j % cols) * W; a = f[..., 3:4] / 255.0
        s[y:y+H, x:x+W, :3] = (f[..., :3] * a + s[y:y+H, x:x+W, :3] * (1 - a)).astype(np.uint8)
    cv2.imwrite("_ver_quadros.png", cv2.resize(s[..., :3], None, fx=0.5, fy=0.5, interpolation=cv2.INTER_AREA))
