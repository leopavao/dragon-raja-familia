# -*- coding: utf-8 -*-
"""Decodificador do formato .spr do Dragon Raja (cursor, personagens, NPCs).

Formato (primeira documentacao conhecida; parente do chunk comprimido do .sou):

  arquivo:  u16 n_quadros
            u32 offset[n_quadros]          (absolutos, crescentes)
  quadro:   u32 tamanho_restante
            u16 W, u16 H                   (canvas do quadro)
            u16 ax, u16 ay                 (ancora: pe do personagem / ponta do cursor)
            u32 tam_dados
            H linhas RLE: repete [u16 pula_BYTES, u16 copia_BYTES, pixels...]
            terminadas por u16 0xFFFF
  pixels:   RGB565 little-endian; 0xFFFF e transparencia (nao ha pixel branco puro)
  quadro vazio: tamanho_restante == 12 (placeholder)

Uso:
  python spr_decode.py <arquivo.spr> <saida.png> [--grid N] [--first K] [--scale S]
    --grid N   monta folha de contato com N colunas
    --first K  so os K primeiros quadros
    --scale S  amplia S vezes (vizinho mais proximo)
"""
import os, sys, struct, argparse
import numpy as np
from PIL import Image


def load_spr(path):
    b = open(path, 'rb').read()
    n = struct.unpack_from('<H', b, 0)[0]
    offs = np.frombuffer(b, dtype='<u4', count=n, offset=2).astype(np.int64)
    return b, offs


def decode_frame(b, off):
    """-> (rgba HxWx4, ax, ay) ou None se quadro vazio."""
    restante = struct.unpack_from('<I', b, off)[0]
    if restante <= 12:
        return None
    W, H, ax, ay = struct.unpack_from('<4H', b, off + 4)
    dados = struct.unpack_from('<I', b, off + 12)[0]
    p = off + 16
    fim = p + dados

    img = np.zeros((H, W, 4), np.uint8)
    for row in range(H):
        x = 0
        while p + 2 <= fim:
            v = struct.unpack_from('<H', b, p)[0]; p += 2
            if v == 0xFFFF:
                break
            x += v // 2
            if p + 2 > fim:
                break
            cl = struct.unpack_from('<H', b, p)[0]; p += 2
            npx = cl // 2
            # corta o que passa da borda (runs chegam a W+1); antes o run inteiro
            # era descartado e o quadro saia com faixas vazias (18/11/2026)
            k = max(0, min(npx, W - x))
            if k and p + cl <= fim:
                px = np.frombuffer(b, dtype='<u2', count=k, offset=p)
                a = px.astype(np.uint32)
                img[row, x:x + k, 0] = ((a >> 11) & 0x1F) * 255 // 31
                img[row, x:x + k, 1] = ((a >> 5) & 0x3F) * 255 // 63
                img[row, x:x + k, 2] = (a & 0x1F) * 255 // 31
                img[row, x:x + k, 3] = np.where(px == 0xFFFF, 0, 255)
            p += cl
            x += npx
    return img, ax, ay


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('spr'); ap.add_argument('out')
    ap.add_argument('--grid', type=int, default=10)
    ap.add_argument('--first', type=int, default=0)
    ap.add_argument('--scale', type=int, default=1)
    a = ap.parse_args()

    b, offs = load_spr(a.spr)
    n = len(offs) if not a.first else min(a.first, len(offs))
    quadros = []
    vazios = 0
    for i in range(n):
        q = decode_frame(b, int(offs[i]))
        if q is None:
            vazios += 1
            quadros.append(None)
        else:
            quadros.append(q)
    print('%s: %d quadros (%d vazios)' % (os.path.basename(a.spr), n, vazios))

    cw = max((q[0].shape[1] for q in quadros if q), default=8)
    ch = max((q[0].shape[0] for q in quadros if q), default=8)
    cols = a.grid
    rows = (n + cols - 1) // cols
    folha = Image.new('RGBA', (cols * (cw + 2), rows * (ch + 2)), (24, 24, 30, 255))
    for i, q in enumerate(quadros):
        if q is None:
            continue
        img = Image.fromarray(q[0])
        folha.paste(img, ((i % cols) * (cw + 2) + 1, (i // cols) * (ch + 2) + 1), img)
    if a.scale > 1:
        folha = folha.resize((folha.width * a.scale, folha.height * a.scale), Image.NEAREST)
    folha.save(a.out)
    print('folha: %s  (%dx%d por quadro)' % (a.out, cw, ch))


if __name__ == '__main__':
    main()
