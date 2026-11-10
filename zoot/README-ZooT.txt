ZooT-PC v0.11
=============

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

Opcoes
------
- Shaders sem travadas (GPL): monta os shaders em partes e so "liga" na hora. Bem menos
  engasgo na primeira vez que cada efeito aparece.
- Pre-compilar antes de jogar: extrai os shaders do seu jogo e prepara tudo antes de comecar.
- Resolucao, filtro de escala (FSR 1/3), anti-serrilhado, proporcao, tela cheia, VSync,
  modo TV/portatil e volume.

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
