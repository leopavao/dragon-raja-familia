# Cliente familia: estabilidade, launcher e conveniencias GM

## Pedido e entrega

Corrigir a perda de comunicacao apos comandos do Ctrl+F12; entregar um launcher Windows que atualize e abra o cliente; exibir Reviver instantaneamente na tela enquanto fantasma; tornar a previa de itens opcional. O servidor esta no PC do Leo. Nenhum push ou release antes da confirmacao do teste em jogo pelo usuario. Screenshots, dados locais e credenciais nao entram no Git.

## Abordagem

Trabalhar em clone proprio, branch codex/launcher-painel-gm, preservando o cliente funcional e criando backup antes de instala-lo. Corrigir o transporte TCP existente, sem trocar protocolo ou implementar reconexao que possa duplicar comandos. Envio parcial ou WSAEWOULDBLOCK deve conservar os bytes ja criptografados e sua ordem. Validar com simulacao deterministica do envio real.

Cliente e servidor usam LocalAdminProtocol v3. Manter os tamanhos existentes e adicionar uma operacao de reviver; servidor antigo deve continuar respondendo aos comandos existentes. O servidor valida permissao GM e estado morto antes de reviver, chamando SkillMgr.CharacterToAlive(ch, 1), a rotina usada na ressurreicao por item. O botao aparece na tela do jogo somente no modo familia/admin e enquanto fantasma. Nao consome item.

Criacao direta envia a configuracao inteira com previewToken=0. A previa continua disponivel; token informado continua exigindo uma previa valida, vinculada ao personagem. Ambas as rotas usam PrepareItem e a verificacao de espaco antes de inserir. Nada de previa automatica escondida.

Launcher em C#/.NET Framework com WinForms, usando o runtime do Windows e sem precisar do SDK no PC de quem joga. Consultar releases jogaveis do repo, autenticar com credencial Git local ou token individual salvo com DPAPI, baixar apenas pacote/manifesto publicado, verificar SHA256, preparar antes de aplicar, fazer backup e rollback em caso de falha. Nunca executar scripts vindos do repo. Preservar configuracoes, screenshots e dados pessoais, nunca apagar extras. Nao atualizar enquanto o jogo esta aberto. Se a verificacao remota falhar, mostrar o motivo e permitir abrir a instalacao existente por decisao do usuario. Nao rebaixar automaticamente um build local em teste.

## Alternativas consideradas

- Compilar o repo ao abrir: exige Visual Studio/SDK e dependencias legadas em cada PC, lento e nao garante versao validada.
- Sincronizar arquivos indiscriminadamente: sobrescreve conexao/configuracao e nao produz executavel a partir de C++.
- Releases com manifesto de arquivos: escolhida pelo implementador porque distribui builds testados e permite preservar dados locais. Commits de codigo precisam de build e publicacao para chegar aos jogadores.

## Validacao e limites

Testar envios parciais, bloqueio temporario, fechamento, ordem e retomada. Testar launcher com pacotes sinteticos: hash incorreto, travessia de caminhos, configuracoes preservadas, jogo aberto, falha durante aplicacao e rollback. Compilar cliente/MapServer e launcher. Instalar cliente candidato local com backup e preparar pacote de servidor para o Leo. O teste em jogo com o servidor atualizado e a confirmacao do usuario sao a condicao para publicar. Nao alterar SQL/banco ou parar servidor remoto.

## Integracao autorizada em 19/11/2026

O usuario autorizou o push do codigo antes do teste em jogo. Integrar sobre a main 5579c5e, preservando protocolo v5, layouts e IDs existentes; LA_REVIVE passa a 30. Esta atualizacao substitui as referencias ao protocolo v3 e a espera de autorizacao para push no plano original. Versao de pacote 1.1.0, com MapServer e RajaDB em conjunto. Gameplay permanece pendente e nenhuma mudanca de hospedagem foi feita.
