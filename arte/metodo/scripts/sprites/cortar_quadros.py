# Corte final por boneco: acha cada personagem na folha recortada (manchas grandes), em ordem da esquerda
# pra direita, e centraliza uma janela de largura fixa no tronco dele. A altura (chão) segue a da folha,
# então o sobe-e-desce do passo fica preservado. Parado: usa os 3 primeiros bonecos.
import sys, os
import numpy as np, cv2
from PIL import Image
sys.path.insert(0, r"C:/dr-sprites/jogo")
from spr_decode import load_spr, decode_frame
_B, _O = load_spr(r"C:/DragonRaja/Char/001000.spr")
DIRS = ["S","SW","W","NW","N","NE","E","SE"]; PX = 5.6; CHAO = 560
def alvo(d, quadros):
    hs = []
    for q in quadros:
        a = decode_frame(_B, int(_O[DIRS.index(d)*75+q]))[0][..., 3]; ys = np.nonzero(a.max(1) > 0)[0]; hs.append(ys.max()-ys.min())
    return float(np.median(hs)) * PX
L = 320
def bonecos(arq, h_alvo):
    im = Image.open(arq).convert("RGBA"); a = np.array(im)[..., 3] > 100
    k0, lab0, st0, _ = cv2.connectedComponentsWithStats(cv2.dilate(a.astype(np.uint8), np.ones((9, 9), np.uint8)), 8)
    big = [i for i in range(1, k0) if st0[i, cv2.CC_STAT_AREA] > 3000]
    hs = [st0[i, cv2.CC_STAT_HEIGHT] - 9 for i in big]; pes = [st0[i, 1] + st0[i, 3] - 5 for i in big]
    esc = h_alvo / float(np.median(hs)); pe = float(np.median(pes)) * esc
    im = im.resize((round(im.width*esc), round(im.height*esc)), Image.LANCZOS)
    tela = Image.new("RGBA", (im.width, CHAO + 120), (0, 0, 0, 0)); tela.alpha_composite(im, (0, int(CHAO - pe))) if CHAO - pe >= 0 else tela.alpha_composite(im.crop((0, int(pe - CHAO), im.width, im.height)), (0, 0))
    im = tela; a = np.array(im)[..., 3] > 100
    m = cv2.dilate(a.astype(np.uint8), np.ones((9, 9), np.uint8))   # junta pedaços (aljava, flechas)
    k, lab, st, _ = cv2.connectedComponentsWithStats(m, 8)
    bs = [i for i in range(1, k) if st[i, cv2.CC_STAT_AREA] > 3000]
    bs.sort(key=lambda i: st[i, cv2.CC_STAT_LEFT])
    out = []
    for i in bs:
        x, y, w, h = st[i, :4]
        sub = a[y:y + int(h * 0.45), x:x + w] & (lab[y:y + int(h * 0.45), x:x + w] == i)
        cx = x + np.nonzero(sub)[1].mean()
        so = np.array(im).copy(); so[..., 3] = np.where(lab == i, so[..., 3], 0)
        out.append((int(cx), Image.fromarray(so)))
    return im, out
for d in sys.argv[1:]:
    jan = {}
    for acao, n, qs in [x for x in (("parado", 3, [1,2,3]), ("andar", 6, [4,5,6,7,8,9]), ("correr", 6, [44,45,46,47,48,49])) if os.path.exists(f"{d}/{x[0]}_recortada.png")]:
        import json
        jf = f"{d}/{d}_{acao}_manequim_baixa.json"
        n_folha = len(json.load(open(jf))["quadros"]) if os.path.exists(jf) else (3 if acao == "parado" else 6)
        im, cxs = bonecos(f"{d}/{acao}_recortada.png", alvo(d, qs))
        larg = [int((np.array(c)[..., 3] > 100).any(0).sum()) for _, c in cxs]
        grudados = len(cxs) < n or (len(larg) and max(larg) > 1.6 * float(np.median(larg)))
        if len(cxs) != n_folha and grudados:   # bonecos encostados: divide por colunas fixas, centro = massa dentro da coluna
            print("AVISO", d, acao, "achei", len(cxs), "bonecos -> colunas"); arr = np.array(im)[..., 3] > 100; cw = im.width / n_folha; cxs = []
            colsum = arr.sum(0); lim = [0]
            for j in range(1, n_folha):
                c = int(cw*j); r = int(cw*0.3); lim.append(c - r + int(np.argmin(colsum[c-r:c+r])))
            lim.append(im.width)
            for j in range(n_folha):
                x0, x1 = lim[j], lim[j+1]; sub = arr[:, x0:x1]; xs = np.nonzero(sub)[1]
                so = np.array(im).copy(); m = np.zeros(arr.shape, bool); m[:, x0:x1] = True
                # dentro da coluna, fica só o maior pedaço (o boneco dela); sobras do vizinho saem
                k2, lab2, st2, _ = cv2.connectedComponentsWithStats(cv2.dilate((arr & m).astype(np.uint8), np.ones((15, 15), np.uint8)), 8)
                if k2 > 1:
                    big2 = 1 + int(np.argmax(st2[1:, cv2.CC_STAT_AREA])); m &= lab2 == big2
                    xs = np.nonzero(arr & m)[1]
                so[..., 3] = np.where(m, so[..., 3], 0)
                cxs.append((int(xs.mean()) if len(xs) else int(x0 + cw/2), Image.fromarray(so)))
        crops = [so.crop((cx - L // 2, 0, cx - L // 2 + L, im.height)) for cx, so in cxs]
        if acao == "parado" and len(crops) >= 4:   # folha 1,2,3,1,2,3: fica a cópia mais inteira de cada
            area = lambda c: int((np.array(c)[..., 3] > 100).sum())
            crops = [max([crops[j] for j in (k, k + 3) if j < len(crops)], key=area) for k in range(3)]
        jan[acao] = crops[:n]
    y0, y1 = CHAO - 470, CHAO + 40     # mesma janela em todas as direções
    for acao, fs in jan.items():
        for i, f in enumerate(fs): f.crop((0, y0, L, y1)).save(f"{d}/{acao}_{i+1}.png")
    print("cortado", d, {a: len(v) for a, v in jan.items()})
