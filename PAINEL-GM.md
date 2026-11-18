# Painel GM local

No cliente `local-client/DragonRajaLocal.exe`, entre com a conta `leo`, selecione o personagem e pressione **Ctrl+F12** dentro do mapa. O painel depende dos servidores locais em execução (`start-local.ps1`).

- **Personagem:** STR, DEX, CON, WIS, INT, CHA, MOVP, ENDU, MOR, LUCK, WSPS, nível, experiência e pontos. O botão de desfazer restaura a última edição desses atributos na sessão.
- **Itens:** pesquise pelo nome ou ID, escolha quantidade, durabilidade, grau, efeitos e High. Gere a prévia antes de adicionar ao inventário. Alterar a configuração invalida a prévia. Os efeitos disponíveis vêm do catálogo do servidor; efeitos novos exigem implementação adicional no jogo.
- **Moedas:** os campos definem o saldo final de ouro, banco ou RE. Ouro ocupa espaços no inventário; a operação pode ser recusada por falta de espaço.
- **Dual / Symbol:** escolha a classe secundária e a etapa dual (0-5), além de fama, ladder e pontos de combate. A classe base é preservada. O symbol é criado/atualizado diretamente no espaço próprio do personagem, conforme a combinação de classes; grau 0 remove e graus 1-5 criam. Remova o symbol antes de retornar à etapa dual 0.
- **Progressões:** edite as 13 táticas, habilidades e grandmasters, profissões/fabricação (nível, progresso e rare), magias Wizard/Priest por ID, e as 36 habilidades de combate por nome. Há ainda ghost quest, fama dual de quest, honor, faith e etapa de magia religiosa. Cada aplicação altera somente a entrada selecionada. Táticas recalculam experiência e etapa para o nível escolhido. Profissões aceitam níveis 0-100; combate aceita 0-3, respeitando o total de pontos permitido no nível atual do personagem. Magias e habilidades comuns usam 0/1 para remover/aprender.

Os valores de ghost quest e honor são campos de progressão dos scripts. Alterá-los não executa automaticamente todos os eventos ou recompensas associados às quests.

- **Monstros:** pesquise nome ou ID do catálogo do servidor e informe uma quantidade de 1 a 20. O botão **Invocar monstros** cria NPCs livres em posições transitáveis perto do personagem, no mapa atual. Há intervalo de 3 segundos entre comandos. A resposta informa a quantidade efetivamente criada; não há respawn automático desses NPCs.

Os invocados usam a geração normal de loot da espécie e as regras do mapa. Após essa geração, equipamentos compatíveis que seriam comuns são convertidos em raros usando os efeitos válidos do servidor, sem sorteio adicional de chance. A conversão preserva o ID, quantidade, durabilidade e demais atributos do item. Raros e lendários existentes mantêm seus graus; equipamentos comuns convertidos recebem grau 1. A chance máxima de raridade não garante que o monstro sorteie um equipamento, nem aumenta a quantidade de itens. Consumíveis e outros itens incompatíveis continuam normais. Isso vale para novas invocações após instalar a atualização.

O início dos mapas usa `local-server/maps-enabled.json`. West Glade usa o diretório `local-server/map-w_glade` e a porta local 5770. O catálogo em `local-maps.json` identifica 97 mapas com arquivo `.map` e sete sem esse arquivo no pacote: BATTLE, KAELUNE, FIGHT2, HOUSE, HILL, mon_arena2 e ITEM_LOG.

As alterações são processadas pelo servidor para o personagem conectado e enviadas ao mecanismo normal de salvamento. A mensagem de salvamento indica envio, não confirmação do banco. Saia normalmente do personagem antes de desligar o servidor.

O servidor autoriza a conta configurada em `[local_admin]` nos arquivos `local-server/map*/MapServer.ini`. Após ativar o modo família, também autoriza as três contas de `local-server/family-gm.ini`. O cliente habilita a janela por `[local] AdminPanel=1` em `local-client/dragon.ini`.

Backup anterior à instalação: `C:/ProgramData/DragonRajaLocal/backups/BeforeLocalAdmin_20261117_163651.bak`.
