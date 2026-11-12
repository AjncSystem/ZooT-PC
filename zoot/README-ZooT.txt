ZooT-PC v0.13.1
===============

Port para PC de The Legend of Zelda: Ocarina of Time (Nintendo Switch 2), feito sobre o
Lichii (github.com/zortos293/Lichii, GPL-3.0).

O pacote NAO traz o jogo, chaves, firmware nem nenhuma arte da Nintendo. Voce precisa da sua
propria copia do jogo (.nup, .nx, .nsp ou .xci).

Como usar
---------
1. Extraia esta pasta onde quiser (ela e portatil: tudo fica dentro dela, em user\).
2. Abra ZooT.exe. Na primeira vez ele procura o jogo nos seus discos; escolha o arquivo ou
   use "Procurar arquivo...".
3. Aperte JOGAR. Na primeira abertura (e depois de trocar o driver de video) os shaders do
   jogo sao preparados numa tela de carregamento: alguns minutos, uma vez so. Esc pula.

Aviso "O Windows protegeu o computador" (tela vermelha)
-------------------------------------------------------
E o Microsoft Defender SmartScreen: ZooT.exe e eden-cli.exe sao novos e nao tem assinatura
digital, entao o Windows ainda nao os conhece. Clique em "Mais informacoes" e depois em
"Executar assim mesmo" (so na primeira vez). O codigo e publico em
github.com/AjncSystem/ZooT-PC e o pacote e gerado a partir dele.

Opcoes
------
- Shaders sem travadas (GPL): monta os shaders em partes e so "liga" na hora. Bem menos
  engasgo na primeira vez que cada efeito aparece.
- Pre-compilar antes de jogar: extrai os shaders do seu jogo e prepara tudo antes de comecar.
- Resolucao, filtro de escala (FSR 1), anti-serrilhado, proporcao, tela cheia, VSync,
  modo TV/portatil e volume.
- Upscaler: o do jogo, AMD FSR 3.1 (qualquer placa), AMD FSR 4 (placas AMD) ou NVIDIA DLSS
  (placas RTX; as DLLs oficiais da NVIDIA ficam em streamline\, licencas em licenses\).
- Geracao de quadros 2x: um quadro gerado entre dois do jogo.

Atalhos no jogo
---------------
F1 menu ZooT (abas com Q/E, funciona com o mouse)  -  F7 modo de FPS (60/30/ilimitado)  -
F8 geracao de quadros  -  F10 painel de desempenho (FPS, tempo de quadro, VRAM e RAM)  -
F11 tela cheia

Controle, teclado e mouse
-------------------------
Funcionam juntos, sempre. WASD anda (C devagar), o mouse move a camera (clique na janela do
jogo para prender o mouse; Esc ou F1 soltam), clique esquerdo = espada (B), clique direito =
mirar (ZL), Espaco = acao (A). Tudo pode ser trocado em Controles no lancador.

Personalizar (so no seu PC)
---------------------------
- user\zoot\capa.png (ou .jpg): imagem do cartao do jogo no lancador.
- user\zoot\icon.png: icone da janela do jogo.

Pastas
------
- user\nand\user\save   saves (botao "Saves" no lancador)
- user\log\eden_log.txt log do jogo (botao "Log")
- user\zoot\launcher.log erros do lancador

Licencas em licenses\.
