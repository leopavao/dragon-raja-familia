# Atualizacao 1.1.0 — teste conjunto antes de publicar

O cliente e o launcher sao instalados no PC de quem joga. A integracao atual usa o protocolo GM v5 da main e precisa de cliente, MapServer e RajaDB correspondentes. O pacote do servidor inclui os dois executaveis para preservar a verificacao de taticas no banco adicionada na main. O ajuste de comunicacao e do cliente. Em 19/11/2026 o usuario autorizou expressamente o push do codigo antes da validacao em jogo; essa validacao continua pendente. Publicar codigo nao instala uma nova versao na maquina que hospeda o jogo.

## Na maquina que hospeda o servidor

1. Todos salvam e saem normalmente dos personagens.
2. Na pasta do projeto do servidor, rode `./stop-local.ps1`.
3. Extraia o ZIP `server-update-1.1.0.zip` em uma pasta separada. Abra o PowerShell nessa pasta e execute, ajustando o caminho para a instalacao do servidor:

   ```powershell
   .\instalar-servidor.ps1 -Projeto 'C:\Jogos\DragonRaja'
   ```

4. O instalador confere o hash, recusa servidores em execucao, faz backup de cada executavel de mapa e atualiza os mapas existentes e o RajaDB da instalacao. Se houver falha na troca, restaura os arquivos ja substituidos. O backup fica em `local-server/_backup_atualizacoes/`.
5. Volte a pasta do projeto e rode `./start-local.ps1`.

Configuracoes, contas e banco permanecem na instalacao atual. O script nao encerra processos nem faz alteracoes SQL.

## No cliente

Abra **DragonRajaLauncher.exe**, na pasta do cliente. O candidato anterior 1.0.1 usa protocolo v3; a integracao 1.1.0 usa v5. Instale os componentes correspondentes antes do teste do painel. O launcher preserva um build local quando a release disponivel tem versao anterior.

Confira em jogo:

- Use as acoes do Ctrl+F12 que antes causavam o travamento. Entre elas, confira chat, ataque, inventario e coleta de itens para confirmar que a comunicacao continua funcionando.
- Escolha um item e clique em **Adicionar ao inventario** sem gerar previa. Confira quantidade e atributos. Repita usando uma previa para verificar as duas formas.
- Com o personagem morto e fantasma, confira **Reviver instantaneamente** na tela, sem precisar abrir o painel. Confira a volta a vida, recursos e outras acoes do jogo. Vivo, o botao deve desaparecer.
- Feche o cliente e abra pelo launcher. Confira a verificacao da versao e a abertura do jogo. Abrir o launcher com o jogo em execucao deve pedir para fecha-lo antes de atualizar.

Avise o resultado de cada ponto. O push do codigo esta autorizado; uma Release estavel para atualizacao automatica precisa ser coordenada com a versao do servidor. Nenhum screenshot entra no commit nem nos pacotes.

## Compilar e preparar futuras releases

```powershell
.\tools\build-family.ps1 -ClientDirectory 'C:\Jogos\DragonRaja\cliente-familia'
python -m unittest discover -s tests -p 'test_*.py' -v
.\tools\test-launcher.ps1
.\tools\package-family-update.ps1 -Version '1.1.0' -ClientDirectory 'C:\Jogos\DragonRaja\cliente-familia'
```

O codigo pode ser publicado conforme autorizado em 19/11/2026. Depois do teste em jogo e da coordenacao com o servidor, criar a release `v1.1.0` e anexar `family-update.json`, `client-update-1.1.0.zip` e `server-update-1.1.0.zip`. Disponibilizar `DragonRajaLauncher.exe` separadamente para a primeira instalacao em outros PCs. Os pacotes saem em `artifacts/`, ignorados pelo Git; screenshots e credenciais tambem ficam fora.

O launcher verifica a versao numerica da release e so aplica versoes posteriores a instalada. Ao aprovar/publicar o candidato, atualizar o recibo local `.launcher/installed.json` para `localBuild: false`. Cada atualizacao preserva backups em `.launcher/backups/`. Use uma versao nova nas proximas publicacoes.
