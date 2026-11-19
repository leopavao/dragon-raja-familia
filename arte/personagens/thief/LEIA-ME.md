# Thief (arte HD)

Thief redesenhado em HD por cima do corpo masculino original do jogo (`Char/001000.spr`). As poses e o ritmo de cada animação seguem o sprite original (tabela `001AniTable.bin`).

## O que tem aqui

- `quadros/<direção>/`: PNGs com fundo transparente, 320x510, todos com o chão na mesma altura. São 8 direções (S, SW, W, NW, N, NE, E, SE).
  - `parado_1..3`: no jogo a ordem é 1, 2, 3, 2.
  - `andar_1..6`: equivale à animação 1 do jogo, quadros 4 a 9.
  - `correr_1..6`: equivale à animação 12 do jogo, quadros 44 a 49.
- `gifs/`: prévia de cada animação por direção, em metade do tamanho.
- `videos/thief_andar_e_correr_8_direcoes.mp4`: ele anda e corre nas 8 direções, com zoom.
- `videos/thief_na_vila_helternt.mp4`: ele andando numa versão HD de Helternt, já na escala do jogo e com sombra nos pés.
- `referencia/regua_tamanho.png`: mostra o mesmo cruzamento no mapa original, com o boneco do jogo (67 px), e na vila HD, com o Thief. Os dois têm o mesmo tamanho, então essa é a escala certa.
- `referencia/vila_helternt_hd.png`: o cenário HD usado no vídeo.

## Escala

No jogo, o boneco mede uns 65 a 67 px de altura. Se o quadro HD for usado junto com o cenário original, é preciso reduzir até o corpo ficar com essa altura.
