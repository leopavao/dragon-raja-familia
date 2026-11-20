# Dragon Raja Família

Código do cliente e dos servidores Windows, scripts de configuração e guia do painel GM para partidas em uma rede privada. Consulte [Jogar em família](JOGAR-EM-FAMILIA.md) para a configuração e [Painel GM](PAINEL-GM.md) para os comandos.

O cliente compilado é distribuído na área **Releases**. Ao extrair o cliente, edite `dragon.ini` e coloque o IPv4 do Tailscale do anfitrião em `host1`, `host2`, `name1_host1` e `name1_host2`. Use o executável `DragonRajaFamilia.exe`.

Os projetos usam Visual Studio Build Tools para Win32. A hospedagem requer os dados do servidor e um SQL Server local configurado. Os scripts de restauração esperam backups locais com os esquemas do jogo.

## Launcher e atualizacoes

Coloque `DragonRajaLauncher.exe` na mesma pasta de `DragonRajaFamilia.exe` e abra o launcher. Ele confere a ultima release, verifica SHA-256, faz backup e abre o jogo. O jogo precisa estar fechado para atualizar. Configuracoes, screenshots e arquivos pessoais sao preservados.

O repo e privado: o launcher usa o login do GitHub ja configurado no Git deste Windows. Em **Acesso ao GitHub**, cada pessoa tambem pode informar seu proprio token com leitura deste repo; a opcao de lembrar usa a protecao DPAPI do usuario do Windows. Nenhuma credencial acompanha o executavel ou os pacotes.

As releases novas devem incluir `family-update.json` e o ZIP indicado no manifesto. Alteracoes apenas no codigo precisam ser compiladas e publicadas para chegar aos jogadores. O launcher nao executa scripts baixados e nao rebaixa um build local de teste para uma release anterior.

Build e testes: [tools/build-family.ps1](tools/build-family.ps1), [tools/package-family-update.ps1](tools/package-family-update.ps1) e [guia de teste/publicacao](docs/TESTAR-ATUALIZACAO.md). O launcher usa .NET Framework 4.8; o cliente, o MapServer e o RajaDB usam MSVC v143/Win32. As dependencias COM legadas de compilacao sao copiadas de um cliente ja instalado e ficam ignoradas pelo Git.
