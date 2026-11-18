# Dragon Raja pela VPN

O seu PC hospeda o jogo. Cada primo instala o Tailscale, aceita o compartilhamento do seu PC e cria a própria conta dentro do jogo. Cada um dos três IPs autorizados pode registrar uma conta. O painel GM é liberado no cadastro.

## 1. Conectar os quatro computadores

1. Instale e conecte o Tailscale no PC servidor. Cada primo instala o Tailscale no próprio PC e entra com a própria conta. [Instalação para Windows](https://tailscale.com/docs/install/windows)
2. Na página **Machines** do Tailscale, abra o menu do seu PC e use **Share**. Gere um convite para cada primo. Peça que aceitem e enviem o IPv4 do Tailscale dos PCs deles. Anote também o IPv4 do seu PC. [Compartilhar um computador](https://tailscale.com/docs/features/sharing)
3. No seu PC, salve e saia do personagem normalmente e feche o jogo. Abra o **Windows PowerShell como administrador**, entre na pasta do projeto e execute os comandos abaixo. Troque os quatro IPs de exemplo pelos endereços do servidor e dos primos.

```powershell
Set-Location 'C:\Jogos\DragonRaja'
.\stop-local.ps1
.\configurar-vpn-familia.ps1 -Servidor 100.100.0.10 -Primos 100.100.0.11,100.100.0.12,100.100.0.13
.\start-local.ps1 -LaunchClient
.\preparar-cliente-familia.ps1
```

O script configura as portas TCP 9001, 9004 e 18765 na interface Tailscale para os três IPs informados e atualiza os executáveis de mapa. O banco e as conexões internas do jogo continuam locais. O roteador pode ficar como está. Se você já gerou a pasta `cliente-familia` com a versão anterior, renomeie essa pasta antes de gerar a nova cópia.

## 2. Criar a conta e usar o GM

1. Compacte somente a pasta `cliente-familia` gerada e mande uma cópia para cada primo. Cada um extrai a pasta, conecta o Tailscale e abre **DragonRajaFamilia.exe**.
2. Na tela de login, digite um usuário de 3 a 15 letras ou números e uma senha exclusiva para o jogo de 8 a 15 letras ou números. Clique em **Criar conta**. O cliente tenta entrar assim que o servidor confirma o cadastro. Nas próximas vezes, use o botão normal de login.
3. Crie um personagem e, já no mapa, pressione **Ctrl+F12** para abrir o painel completo. O servidor grava a autorização GM automaticamente em `family-gm.ini`. Sua conta `leo` já tem sua própria autorização.
4. Se um primo receber erro de conexão, execute os testes abaixo no PC dele usando o IPv4 Tailscale do servidor. Cada porta deve retornar `TcpTestSucceeded : True`. A porta 18765 é usada somente para criar a conta.

```powershell
Test-NetConnection 100.100.0.10 -Port 9004
Test-NetConnection 100.100.0.10 -Port 9001
Test-NetConnection 100.100.0.10 -Port 18765
```

Mantenha seu PC e o Tailscale ligados enquanto estiverem jogando. Nas próximas sessões, execute `.\start-local.ps1 -LaunchClient` no PowerShell como administrador. O script também inicia o serviço de cadastro.
