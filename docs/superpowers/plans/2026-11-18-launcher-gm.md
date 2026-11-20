# Launcher e painel GM Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans para implementar nesta sessao, tarefa por tarefa.

**Goal:** Entregar os quatro pedidos em builds locais testaveis, sem publicar antes da confirmacao em jogo.

**Architecture:** Conservar o transporte/protocolo legado, corrigir envios TCP parciais, adicionar criacao direta e ressurreicao autorizadas no MapServer. Launcher separado instala releases compiladas e preserva arquivos locais.

**Tech Stack:** C++ Win32/MSVC v143; C#/.NET Framework/WinForms; Python para harness de testes; PowerShell para build/pacote.

**Spec:** ../specs/2026-11-18-launcher-gm-design.md

## Global Constraints

- Servidor no PC do Leo. Sem push/release ate confirmacao do usuario.
- Manter protocolo v3 e tamanhos dos pacotes existentes.
- Nao versionar screenshots, binarios locais, dados de jogo ou credenciais.
- Preservar configuracoes e dados locais; backups antes de substituir.
- Nao executar scripts de atualizacao baixados.

## Review Focus

- Socket aceita prefixo do pacote e bloqueia: retomar exatamente o sufixo sem nova criptografia.
- Cliente candidato mais novo que release: nunca rebaixar silenciosamente durante o teste.
- Pacote malformado, caminho absoluto/traversal ou hash incorreto: nenhuma alteracao da instalacao.
- Falha no meio de varios arquivos: restaurar todos os arquivos tocados.
- Servidor antigo e permissao ausente: negar nova operacao sem derrubar os comandos existentes.

## Task 1: Corrigir transporte do cliente

**Files:** dragon2022/network.cpp, dragon2022/network.h, tests/test_network_stream.py.

- [x] Criar harness que compila o corpo real de HandleWriting e simula send parcial/bloqueio/fechamento; comprovar falha atual.
- [x] Conservar lote criptografado na conexao ate envio completo e incluir socket no select de escrita quando houver bytes pendentes.
- [x] Executar os testes e compilar cliente baseline/candidato, registrando dependencias ausentes sem alterar logins.

## Task 2: Previa opcional e reviver

**Files:** Shared/LocalAdminProtocol.h, dragon2022/LocalAdminPanel.cpp/.h, dragon2022/gameproc.cpp, Mapserver/HigherLayers/LocalAdminServer.cpp.

- [x] Criar teste executavel de criacao sem token, previa valida/invalida e autorizacao de reviver, usando o codigo de decisao compartilhado quando necessario.
- [x] Compartilhar leitura dos campos de item entre previa e criacao. Token zero prepara item diretamente no servidor; token diferente de zero mantem validacao.
- [x] Adicionar LA_REVIVE sem mudar layouts; validar morto e permissao, reutilizar CharacterToAlive(ch,1).
- [x] Exibir botao na tela apenas quando fantasma, usando envio normal e sem duplicar pedido enquanto pendente.
- [x] Compilar cliente e MapServer e preparar instrucoes para teste no PC do Leo.

## Task 3: Launcher e distribuicao

**Files:** launcher/*.cs, launcher/*.csproj, tools/build-family.ps1, tools/package-family-update.ps1, tests/launcher/*, .gitignore, README.md.

- [x] Criar testes de integridade, caminhos, preservacao, rollback e jogo aberto usando diretorios temporarios e o atualizador real.
- [x] Implementar cliente GitHub autenticado sem segredo embutido, manifesto versionado, downloads com digest, backup/rollback e UI responsiva de progresso/erros.
- [x] Implementar build e geracao do manifesto/pacote a partir de arquivos explicitamente selecionados, excluindo screenshots/configuracoes pessoais.
- [x] Compilar exe e executar testes incluindo GitHub somente leitura e simulacao local ponta a ponta.

## Task 4: Entrega local e teste conjunto

- [x] Revisar diff, hashes, segredos e exclusoes; rodar verificacoes pertinentes.
- [x] Fazer backup e instalar cliente/launcher candidato se o jogo estiver fechado, conservando origem do build local para nao rebaixar.
- [x] Entregar pacote de servidor e checklist concreto (rede/GM, criar sem/com previa, morrer/reviver, launcher).
- [x] Registrar no daily_log. Aguardar confirmacao em jogo; so entao publicar repo/release e conferir o launcher real.

## Integracao autorizada em 19/11/2026

O usuario autorizou o push do codigo antes do teste em jogo. Integrar sobre a main 5579c5e, preservando protocolo v5, layouts e IDs existentes; LA_REVIVE passa a 30. Esta atualizacao substitui as referencias ao protocolo v3 e a espera de autorizacao para push no plano original. Versao de pacote 1.1.0, com MapServer e RajaDB em conjunto. Gameplay permanece pendente e nenhuma mudanca de hospedagem foi feita.
