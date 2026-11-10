# SPDX-License-Identifier: GPL-3.0-or-later
"""Launcher texts. The key is the Portuguese text, as in the SotC-PC launcher; a text without a
translation shows in Portuguese."""

LANG = 'pt'
LANGUAGES = [('Português', 'pt'), ('English', 'en'), ('Español', 'es')]

T = {
    'Jogar': ('Play', 'Jugar'),
    'Jogo': ('Game', 'Juego'),
    'Imagem e FPS': ('Display & FPS', 'Imagen y FPS'),
    'Idioma': ('Language', 'Idioma'),
    'Saves': ('Saves', 'Partidas'),
    'Ajuda': ('Help', 'Ayuda'),
    'Registro': ('Log', 'Registro'),
    'port nativo · Windows': ('native port · Windows', 'port nativo · Windows'),
    'Registro do jogo': ('Game log', 'Registro del juego'),
    'Trocar jogo': ('Change game', 'Cambiar juego'),
    'Configurar de novo': ('Set up again', 'Configurar de nuevo'),
    'JOGAR': ('PLAY', 'JUGAR'),
    'Bem-vindo! Escolha o arquivo do seu jogo para começar.':
        ('Welcome! Choose your game file to begin.', '¡Bienvenido! Elige el archivo de tu juego para empezar.'),
    'Formatos aceitos: .nup, .nx, .nsp, .xci':
        ('Accepted formats: .nup, .nx, .nsp, .xci', 'Formatos aceptados: .nup, .nx, .nsp, .xci'),
    'Usar o selecionado': ('Use the selected one', 'Usar el seleccionado'),
    'Procurar arquivo...': ('Browse...', 'Buscar archivo...'),
    'Procurando jogos...': ('Looking for games...', 'Buscando juegos...'),
    '{n} arquivo(s) encontrado(s).': ('{n} file(s) found.', '{n} archivo(s) encontrado(s).'),
    'Nenhum jogo encontrado: use "Procurar arquivo...".':
        ('No game found: use "Browse...".', 'Ningún juego encontrado: usa "Buscar archivo...".'),
    'Arquivo do jogo': ('Game file', 'Archivo del juego'),
    'Jogos': ('Games', 'Juegos'),
    'Todos': ('All', 'Todos'),
    'Imagem': ('Picture', 'Imagen'),
    'Resolução': ('Resolution', 'Resolución'),
    'Filtro de escala': ('Scaling filter', 'Filtro de escalado'),
    'Anti-serrilhado': ('Anti-aliasing', 'Antialiasing'),
    'Proporção': ('Aspect ratio', 'Relación de aspecto'),
    'Tela': ('Screen', 'Pantalla'),
    'Tela cheia': ('Fullscreen', 'Pantalla completa'),
    'Modo do console': ('Console mode', 'Modo de la consola'),
    'Shaders': ('Shaders', 'Shaders'),
    'Shaders sem travadas (GPL)': ('Stutter-free shaders (GPL)', 'Shaders sin tirones (GPL)'),
    'Monta os shaders em partes e liga na hora: bem menos engasgo na 1ª vez.':
        ('Builds shaders in parts and links them on the spot: far less stutter the first time.',
         'Monta los shaders por partes y los enlaza al momento: muchos menos tirones la 1ª vez.'),
    'Pré-compilar antes de jogar': ('Precompile before playing', 'Precompilar antes de jugar'),
    'Na 1ª abertura (ou driver novo) prepara os shaders do jogo com tela de carregamento. Esc pula.':
        ('On the first start (or a new driver) the game shaders are prepared on a loading screen. '
         'Esc skips.',
         'En el 1er inicio (o driver nuevo) prepara los shaders con pantalla de carga. Esc salta.'),
    'Áudio': ('Audio', 'Audio'),
    'Volume': ('Volume', 'Volumen'),
    'Arquivo': ('File', 'Archivo'),
    'Idioma do jogo': ('Game language', 'Idioma del juego'),
    'Idioma do lançador': ('Launcher language', 'Idioma del lanzador'),
    'O jogo usa o idioma do console. A região é ajustada sozinha.':
        ('The game uses the console language. The region is set to match.',
         'El juego usa el idioma de la consola. La región se ajusta sola.'),
    'Se o jogo não tiver o idioma escolhido, ele usa o inglês.':
        ('If the game lacks the chosen language, it uses English.',
         'Si el juego no tiene el idioma elegido, usa el inglés.'),
    'Pastas de save': ('Save slots', 'Ranuras de guardado'),
    'Abrir pasta': ('Open folder', 'Abrir carpeta'),
    'Fazer backup': ('Back up', 'Hacer copia'),
    'Backup salvo em:': ('Backup saved to:', 'Copia guardada en:'),
    'Nenhum save ainda: jogue uma vez.': ('No saves yet: play once.', 'Aún no hay partidas: juega una vez.'),
    'Atualizar': ('Refresh', 'Actualizar'),
    'Abrir arquivo': ('Open file', 'Abrir archivo'),
    'Ainda não há registro: jogue uma vez.': ('No log yet: play once.', 'Aún no hay registro: juega una vez.'),
    'Shaders prontos.': ('Shaders ready.', 'Shaders listos.'),
    '1ª abertura: os shaders serão preparados (alguns minutos).':
        ('First start: the shaders will be prepared (a few minutes).',
         '1er inicio: los shaders se prepararán (unos minutos).'),
    'eden-cli.exe não encontrado em {path}': ('eden-cli.exe not found in {path}',
                                               'eden-cli.exe no encontrado en {path}'),
    'Jogo em execução...': ('Game running...', 'Juego en ejecución...'),
    'Não foi possível iniciar o jogo:': ('Could not start the game:', 'No se pudo iniciar el juego:'),
    'O jogo fechou com o código {code}.': ('The game closed with code {code}.',
                                            'El juego se cerró con el código {code}.'),
    'Últimas linhas do registro:': ('Last log lines:', 'Últimas líneas del registro:'),
    'Erro no lançador:': ('Launcher error:', 'Error del lanzador:'),
    'Detalhes em user\\zoot\\launcher.log': ('Details in user\\zoot\\launcher.log',
                                              'Detalles en user\\zoot\\launcher.log'),
    'Nenhum': ('None', 'Ninguno'),
    'Desligado': ('Off', 'Apagado'),
    'Ligado (FIFO)': ('On (FIFO)', 'Activado (FIFO)'),
    'FIFO relaxado': ('Relaxed FIFO', 'FIFO relajado'),
    'Esticar': ('Stretch', 'Estirar'),
    'Portátil': ('Handheld', 'Portátil'),
    'Bicúbico': ('Bicubic', 'Bicúbico'),
    'Área': ('Area', 'Área'),
    'Ajustes rápidos': ('Quick settings', 'Ajustes rápidos'),
    'Verificação': ('Ready check', 'Verificación'),
    'Jogo encontrado': ('Game found', 'Juego encontrado'),
    'Jogo não encontrado': ('Game not found', 'Juego no encontrado'),
    'Núcleo do jogo (eden-cli) presente': ('Game core (eden-cli) present',
                                           'Núcleo del juego (eden-cli) presente'),
    'eden-cli.exe ausente': ('eden-cli.exe missing', 'Falta eden-cli.exe'),
    'Shaders prontos': ('Shaders ready', 'Shaders listos'),
    'Shaders serão preparados na 1ª abertura': ('Shaders will be prepared on the first start',
                                                'Los shaders se prepararán en el 1er inicio'),
}

HELP = {
    'pt': [
        ('Como jogar', 'Na primeira vez escolha o arquivo do seu jogo (.nup, .nx, .nsp ou .xci) e aperte '
                       'JOGAR. Os shaders são preparados numa tela de carregamento (alguns minutos, uma vez só; '
                       'Esc pula).'),
        ('Engasgos', 'Deixe ligados "Shaders sem travadas (GPL)" e "Pré-compilar antes de jogar". Cada efeito '
                     'novo é preparado uma vez e fica guardado em user\\cache.'),
        ('Capa e ícone', 'Coloque sua imagem em user\\zoot\\capa.png (ou .jpg) para o cartão do jogo, e '
                         'user\\zoot\\icon.png para o ícone da janela. Ficam só no seu PC.'),
        ('O que não vem no pacote', 'O ZooT-PC não traz o jogo, chaves, firmware nem arte da Nintendo: '
                                    'use a sua própria cópia.'),
    ],
    'en': [
        ('How to play', 'The first time, choose your game file (.nup, .nx, .nsp or .xci) and press PLAY. '
                        'The shaders are prepared on a loading screen (a few minutes, once; Esc skips).'),
        ('Stutter', 'Keep "Stutter-free shaders (GPL)" and "Precompile before playing" on. Each new effect is '
                    'prepared once and kept in user\\cache.'),
        ('Cover and icon', 'Put your picture in user\\zoot\\capa.png (or .jpg) for the game card, and '
                           'user\\zoot\\icon.png for the window icon. They stay on your PC.'),
        ('Not in the package', 'ZooT-PC ships no game, keys, firmware or Nintendo art: use your own copy.'),
    ],
    'es': [
        ('Cómo jugar', 'La primera vez elige el archivo de tu juego (.nup, .nx, .nsp o .xci) y pulsa JUGAR. '
                       'Los shaders se preparan en una pantalla de carga (unos minutos, una sola vez; Esc salta).'),
        ('Tirones', 'Deja activados "Shaders sin tirones (GPL)" y "Precompilar antes de jugar". Cada efecto '
                    'nuevo se prepara una vez y queda en user\\cache.'),
        ('Portada e icono', 'Pon tu imagen en user\\zoot\\capa.png (o .jpg) para la tarjeta del juego, y '
                            'user\\zoot\\icon.png para el icono de la ventana. Se quedan en tu PC.'),
        ('Lo que no incluye', 'ZooT-PC no trae el juego, claves, firmware ni arte de Nintendo: usa tu copia.'),
    ],
}


def _(text, **kw):
    if LANG != 'pt':
        pair = T.get(text)
        if pair:
            i = 0 if LANG == 'en' else 1
            if pair[i]:
                text = pair[i]
    return text.format(**kw) if kw else text
