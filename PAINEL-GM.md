# Painel GM local

No cliente `local-client/DragonRajaLocal.exe`, entre com a conta `leo`, selecione o personagem e pressione **Ctrl+F12** dentro do mapa. O painel depende dos servidores locais em execução (`start-local.ps1`).

- **Personagem:** STR, DEX, CON, WIS, INT, CHA, MOVP, ENDU, MOR, LUCK, WSPS, nível, experiência e pontos. O botão de desfazer restaura a última edição desses atributos na sessão.
- **Itens:** pesquise pelo nome ou ID, escolha quantidade, durabilidade, grau, efeitos e High. A prévia é opcional: **Adicionar ao inventario** envia a configuração diretamente ao servidor. Se gerar uma prévia, a criação usa exatamente o item validado nela; alterar a configuração invalida essa prévia. Os efeitos disponíveis vêm do catálogo do servidor; efeitos novos exigem implementação adicional no jogo.
- **Reviver:** em forma de fantasma, o modo família mostra **Reviver instantaneamente** na própria tela do jogo, mesmo sem abrir Ctrl+F12. O servidor exige a autorização do painel GM e usa o mesmo efeito de ressurreição completa do item de revival/Easter Egg, sem consumir item. O botão não cura personagens vivos.

Previa opcional e reviver exigem o servidor atualizado. Esta integracao usa o protocolo GM v5 da main: atualize cliente, MapServer e RajaDB em conjunto. Os IDs existentes do v5 foram preservados e LA_REVIVE foi acrescentado como operacao 30. Um servidor v3 da release 1.0.0 nao atende ao painel v5.
- **Moedas:** os campos definem o saldo final de ouro, banco ou RE. Ouro ocupa espaços no inventário; a operação pode ser recusada por falta de espaço.
- **Dual / Symbol:** escolha a classe secundária e a etapa dual (0-5), além de fama, ladder e pontos de combate. A classe base é preservada. O symbol é criado/atualizado diretamente no espaço próprio do personagem, conforme a combinação de classes; grau 0 remove e graus 1-5 criam. Remova o symbol antes de retornar à etapa dual 0.
- **Progressões:** edite as 13 táticas, habilidades e grandmasters, profissões/fabricação (nível, progresso e rare), magias Wizard/Priest por ID, e as 36 habilidades de combate por nome. Há ainda ghost quest, fama dual de quest, honor, faith e etapa de magia religiosa. Cada aplicação altera somente a entrada selecionada. Táticas recalculam experiência e etapa para o nível escolhido. Profissões aceitam níveis 0-100; combate aceita 0-3, respeitando o total de pontos permitido no nível atual do personagem. Magias e habilidades comuns usam 0/1 para remover/aprender.

Os valores de ghost quest e honor são campos de progressão dos scripts. Alterá-los não executa automaticamente todos os eventos ou recompensas associados às quests.

- **Monstros:** pesquise nome ou ID do catálogo do servidor e informe uma quantidade de 1 a 20. O botão **Invocar monstros** cria NPCs livres em posições transitáveis perto do personagem, no mapa atual. Há intervalo de 3 segundos entre comandos. A resposta informa a quantidade efetivamente criada; não há respawn automático desses NPCs.

Os invocados usam a geração normal de loot da espécie e as regras do mapa. Após essa geração, equipamentos compatíveis que seriam comuns são convertidos em raros usando os efeitos válidos do servidor, sem sorteio adicional de chance. A conversão preserva o ID, quantidade, durabilidade e demais atributos do item. Raros e lendários existentes mantêm seus graus; equipamentos comuns convertidos recebem grau 1. A chance máxima de raridade não garante que o monstro sorteie um equipamento, nem aumenta a quantidade de itens. Consumíveis e outros itens incompatíveis continuam normais. Isso vale para novas invocações após instalar a atualização.

- **Legend:** marque **Legend** na aba Criar itens, escolha uma base que aceite Legend e gere a prévia. O jogo sorteia grau, efeito e variante conforme suas tabelas. A entrega usa o resultado da prévia exibida.
- **Diagnóstico de táticas:** a aba Progressões exibe nível, XP e etapa vindos do MapServer. Ao editar ou desfazer a última tática, o RajaDB relê XP e etapas para confirmar o salvamento. A aba Histórico mantém os resultados recentes e registra os níveis e XP anteriores e novos.
- **Presets:** capture e reaplique um preset do personagem na sessão. A confirmação lista os campos afetados: atributos, nível, XP, pontos livres, táticas e magias.
- **Mapa / Sessão:** busque pelo nome ou código do mapa. No mapa atual, informe as coordenadas em tiles. Para outro mapa, a transferência usa o ponto de entrada configurado. É possível marcar uma posição e voltar a ela quando estiver no mesmo mapa. A lista de jogadores, os avisos e a reunião de jogadores abrangem apenas o mapa atual.
- **Ondas:** defina de uma a cinco ondas e um intervalo de 3 a 60 segundos. As ondas seguintes dependem do painel aberto. Use o ID exibido na lista ou no histórico para remover um grupo de monstros vivos do mapa.

### Ferramentas e eventos

- **Eventos salvos:** marque a posição em Mapa / Sessão, digite o aviso e configure monstro, quantidade, ondas e intervalo em Monstros. Salve a arena pelo nome e use **Iniciar salvo** no mapa dessa arena. **Encerrar evento** cancela as ondas restantes e remove os monstros vivos do grupo da rodada. As configurações ficam em `gm-events.ini` no cliente. As ondas são programadas pelo painel aberto.
- **Laboratório Legend:** selecione a base e clique em **Consultar variantes**. A aba Ferramentas mostra o grupo, os efeitos e os valores-base pelos graus 3–8, além do equipamento atual no mesmo encaixe. Informe a variante e o grau na aba Criar itens. Variante 0 mantém o sorteio. A comparação mostra o efeito-base e não simula combinações de conjuntos.
- **Inspeção e combate:** use o nome de um jogador do mapa atual ou deixe o campo vazio para se inspecionar. A tela mostra equipamento, atributos efetivos, táticas e indicadores de estado. Os controles de restauração, limpeza e ressurreição agem no próprio GM. Para criar um alvo de treino, selecione seu modelo em Monstros e use o botão da aba Ferramentas. A medição mostra contagem, últimos oito golpes, total e média acumulados por alvo. Remova o alvo pelo grupo em Monstros.
- **Sessão inteira:** **Mapas e jogadores** consulta os MapServers conectados. Os nomes aparecem conforme os mapas respondem. Para enviar um aviso global, preencha o texto em Mapa / Sessão e clique em **Avisar todos os mapas** na aba Ferramentas.
- **Auditoria:** **Histórico do servidor** lê as ações recentes em `gm-audit.log` no diretório do mapa. Os registros sobrevivem ao reinício do processo e documentam a operação no MapServer.
- **Permissões:** para liberar somente as ferramentas de evento, adicione uma seção `[event_operators]` a `local-server/family-gm.ini` e uma linha `conta=1` por operador. Para essa conta ficar restrita, retire-a da lista de acesso completo em `[local_admin]` e dos `MapServer.ini`. A autorização é conferida no servidor.

O início dos mapas usa `local-server/maps-enabled.json`. West Glade usa o diretório `local-server/map-w_glade` e a porta local 5770. O catálogo em `local-maps.json` identifica 97 mapas com arquivo `.map` e sete sem esse arquivo no pacote: BATTLE, KAELUNE, FIGHT2, HOUSE, HILL, mon_arena2 e ITEM_LOG.

As alterações são processadas pelo servidor para o personagem conectado e enviadas ao mecanismo normal de salvamento. Fora das operações com verificação de táticas, a mensagem de salvamento indica envio ao banco. A confirmação de um preset refere-se ao XP e às etapas das táticas. Saia normalmente do personagem antes de desligar o servidor.

O servidor autoriza a conta configurada em `[local_admin]` nos arquivos `local-server/map*/MapServer.ini`. Após ativar o modo família, também autoriza as três contas de `local-server/family-gm.ini`. O cliente habilita a janela por `[local] AdminPanel=1` em `local-client/dragon.ini`.

Backup anterior à instalação: `C:/ProgramData/DragonRajaLocal/backups/BeforeLocalAdmin_20261117_163651.bak`.
