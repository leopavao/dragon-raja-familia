# Dragon Raja Família

Código do cliente e dos servidores Windows, scripts de configuração e guia do painel GM para partidas em uma rede privada. Consulte [Jogar em família](JOGAR-EM-FAMILIA.md) para a configuração e [Painel GM](PAINEL-GM.md) para os comandos.

O cliente compilado é distribuído na área **Releases**. Ao extrair o cliente, edite `dragon.ini` e coloque o IPv4 do Tailscale do anfitrião em `host1`, `host2`, `name1_host1` e `name1_host2`. Use o executável `DragonRajaFamilia.exe`.

Os projetos usam Visual Studio Build Tools para Win32. A hospedagem requer os dados do servidor e um SQL Server local configurado. Os scripts de restauração esperam backups locais com os esquemas do jogo.
