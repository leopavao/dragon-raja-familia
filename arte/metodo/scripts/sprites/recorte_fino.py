# Recorte fino: inundação a partir da borda acha o fundo; na faixa da borda o alfa vira suave
# (distância da cor ao cinza do fundo) e a cor é "descontaminada" (tira o cinza misturado),
# o que elimina o contorno claro. Depois corta N janelas fixas por coluna.
import sys
import numpy as np, cv2
def recortar(bgr):
    h, w = bgr.shape[:2]
    mask = np.zeros((h+2, w+2), np.uint8)
    for x, y in [(0,0),(w-1,0),(0,h-1),(w-1,h-1),(w//2,0),(w//2,h-1),(0,h//2),(w-1,h//2)]:
        if mask[y+1, x+1] == 0:
            cv2.floodFill(bgr, mask, (x, y), 0, (4,4,4), (4,4,4), 4 | cv2.FLOODFILL_MASK_ONLY | (255 << 8))
    fundo = mask[1:-1, 1:-1] > 0
    hsv = cv2.cvtColor(bgr, cv2.COLOR_BGR2HSV)
    # fundo de verdade varia um pouco (degradê): estimativa local por desfoque grande só do fundo
    f32 = bgr.astype(np.float32)
    peso = cv2.GaussianBlur(fundo.astype(np.float32), (0,0), 25) + 1e-4
    bgl = cv2.GaussianBlur(f32 * fundo[..., None], (0,0), 25) / peso[..., None]
    perto = (np.abs(f32 - bgl).max(axis=2) < 10) & (hsv[..., 1] < 20)
    k, lab, st, _ = cv2.connectedComponentsWithStats(perto.astype(np.uint8), 4)
    for i in range(1, k):
        if st[i, cv2.CC_STAT_AREA] > 150: fundo |= lab == i
    fg = (~fundo).astype(np.uint8)
    fg = cv2.morphologyEx(fg, cv2.MORPH_OPEN, np.ones((3,3), np.uint8))
    nucleo = cv2.erode(fg, np.ones((5,5), np.uint8))           # 2 px pra dentro: certeza de personagem
    faixa = (cv2.dilate(fg, np.ones((3,3), np.uint8)) > 0) & (nucleo == 0)
    dist = np.linalg.norm(f32 - bgl, axis=2)
    alfa = np.where(nucleo > 0, 1.0, 0.0)
    alfa[faixa] = np.clip((dist[faixa] - 12) / 45, 0, 1)
    alfa = np.minimum(alfa, cv2.GaussianBlur(alfa, (3,3), 0) + 0.15) # some com pixel isolado
    a = np.maximum(alfa, 1e-3)[..., None]
    cor = np.clip((f32 - (1 - a) * bgl) / a, 0, 255)                # tira o cinza misturado na borda
    # na borda, não deixa clarear além do vizinho de dentro (mata o halo claro restante)
    dentro = cv2.erode(f32 * (nucleo[..., None] > 0), np.ones((1,1)))
    viz = cv2.dilate(cv2.GaussianBlur(f32 * nucleo[..., None], (0,0), 2) / (cv2.GaussianBlur(nucleo.astype(np.float32), (0,0), 2)[..., None] + 1e-4), np.ones((3,3), np.uint8))
    cor[faixa] = np.minimum(cor[faixa], viz[faixa] + 25)
    out = np.dstack([cor, alfa * 255]).astype(np.uint8)
    out[alfa <= 0.02] = 0
    return out
if __name__ == "__main__":
    folha, n, prefixo = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    rgba = recortar(cv2.imread(folha))
    cv2.imwrite(f"{prefixo}_folha_recortada.png", rgba)
    print("ok")
