# Registro de execucao — 2026-11-18

- Base: main 877530eb95e08593d27a9c8bc096e8441e2b1136; branch local codex/launcher-painel-gm. Sem push/release. Servidor no PC do Leo.
- Transporte: testes reproduziram envio parcial seguido de WSAEWOULDBLOCK. Corrigido buffer persistente dos bytes criptografados, retomada nos dois caminhos de escrita e select com writefds. Quatro cenarios passaram.
- GM: criacao direta com token zero usa PrepareItem; token nao zero mantem vinculacao personagem/previa. LA_REVIVE foi acrescentado ao final do enum sem mudar v3/layout, com verificacao de morto/fantasma e permissao existente. Oito cenarios do codigo de servidor passaram.
- UI: botao proprio na tela para fantasma; inicializa painel oculto para receber resposta/timeout sem depender de Ctrl+F12 aberto. Teste em jogo pendente.
- Build: cliente, MapServer e launcher compilados. Ruling: remover dependencia de MFC usada apenas por CTime/assert e pelo marcador de link do HSEL — o PC nao tem MFC e possui pouco espaco no C:. Datas passaram para time_t/mktime com teste de fuso local e intervalo; sistema Windows incluido com packing 8 e packing legado restaurado. Imports/lib originais preservados. Avisos legados de conversao, macros e PDB continuam.
- Launcher: 16 testes do atualizador + 4 de HTTP passaram. API privada real confirmou v1.0.0. Helper real instalou cliente e launcher em fixture, verificou hashes, preservou dragon.ini e escreveu recibo. Revisao independente encontrou falta de carregar credencial no helper; corrigido no Shown comum. Captura WGC indisponivel; acessibilidade confirmou controles e status de conclusao, sem screenshot salvo no repo.
- Servidor: dois testes executam instalador real no Windows PowerShell 5.1, cobrindo dois mapas, backups/configuracoes e rollback quando o segundo mapa esta bloqueado. Ruling: usar NullString.Value em File.Replace para evitar conversao de null para string vazia no PS5.
- Total automatizado: 15 testes Python/nativos/instalador + 16 do motor de atualizacao + 4 HTTP = 35 verificacoes aprovadas. Gameplay e atualizacao real via uma release nova aguardam o teste conjunto/publicacao autorizada.
- Candidato 1.0.1 instalado em cliente-familia: executavel e launcher atualizados pelo motor real, limites 710 conferidos, dragon.ini/ddraw.ini e tres screenshots preservados. Backup do executavel anterior em cliente-familia/.launcher/backups/2026-11-18_021815_793d07a0. Recibo local marcado localBuild=true para impedir rebaixamento. Pacote final em ../atualizacao-teste-1.0.1-r2.
- Proxima etapa: Leo instala o pacote de servidor e os jogadores testam os quatro pontos em docs/TESTAR-ATUALIZACAO.md. Somente depois da confirmacao, atualizar branch remota e release; conferir novamente a main antes de integrar alteracoes de outras pessoas.

## 19/11/2026 — integracao para push autorizado

- Trabalho salvo em cde9501 foi integrado sobre 5579c5e em worktree separada, preservando novas funcoes GM, persistencia de taticas e arte HD da main.
- Protocolo atual v5 preservado, LA_REVIVE=30; controles, roster, eventos e timers de ondas mantidos. Criacao direta preserva configuracao Legend adicionada na main.
- Build e pacote passam a incluir RajaDB, necessario para a confirmacao de taticas da main. Instalador aceita manifesto legado so de MapServer e novo manifesto de componentes; testes cobrem mapas+DB e rollback conjunto.
- Autorizacao explicita do usuario: pode dar push ja. Validacao de login/gameplay continua pendente devido a conectividade com o host original; source push nao equivale a instalacao no servidor.
- Revisao independente sem bloqueadores apos integrar os conflitos. Limite herdado do instalador: uma falha durante o rollback pode exigir restauracao manual pelo backup.

- Verificacao final: 17 testes Python/nativos/instalador, 16 do atualizador e 4 HTTP aprovados (37). Build completo de cliente, MapServer, RajaDB e launcher aprovado; ZIPs 1.1.0 gerados localmente, conteudo permitido e todos os hashes conferidos. Avisos legados de compilacao permanecem.
- RajaDB: includes MFC sem uso substituidos por Win32/Winsock e assert padrao, mantendo packing dos headers de sistema e suporte de link legado. Sem mudancas de banco/configuracoes.
- Publicacao: envio do codigo integrado para origin/main, sem promover os binarios locais a Release automatica antes do teste coordenado com o servidor.
