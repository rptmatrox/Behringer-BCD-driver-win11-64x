"""
BCD3000 Bridge - servico standalone (Fase 3a).

*** ESTE PROGRAMA E O DONO PERMANENTE DA PORTA MIDI VIRTUAL 'BCD3000'. ***

Ele a cria UMA VEZ ao iniciar e NUNCA a fecha enquanto viver. Nem quando o cabo
e arrancado, nem quando o driver ASIO toma o aparelho, nem quando o software de
DJ e fechado. O driver nao cria porta nenhuma.

POR QUE (medido tres vezes no hardware em 2026-07-29): o software de DJ NAO volta
a procurar o controlador MIDI depois de a porta virtual desaparecer com ele
aberto. A prova sao duas linhas consecutivas do log do driver:
    antes da queda:  midi: ponte parada (controles=734 leds=546 ledErrors=53)
    apos a queda:    midi: ponte parada (controles=604 leds=0   ledErrors=0)
Nos 13 s seguintes o driver LEU 604 mensagens de controle e injetou na porta; o
software de DJ mandou ZERO LEDs. O `leds=0` prova que ele nao estava escutando.
Reproduzido com cabo arrancado, com o botao de forca do aparelho E com troca de
placa de som - que e uma transicao unica e limpa. Logo o problema nao e a porta
"piscar": e o software de DJ desistir do controlador de uma vez.
O unico componente capaz de manter a porta viva o tempo todo e este, que e
processo de vida longa.

*** LIMITACAO CONHECIDA, QUE E CONSEQUENCIA DIRETA DISSO: matar este programa
destroi a porta e reintroduz o defeito. Reiniciar o bridge EXIGE reiniciar o
software de DJ. Um instalador nao deve oferecer "parar o servico" como operacao
de rotina. ***

O que ele faz:
- cria a porta virtual 'BCD3000' uma unica vez, no arranque;
- quando ELE tem o aparelho: le os controles do USB e injeta na porta; escreve
  os LEDs que vem da porta direto no USB;
- quando o DRIVER ASIO tem o aparelho (audio ligado): serve um canal local
  (named pipe) em que o driver escreve os pacotes de controle e le os de LED. Os
  controles entram na porta pelo MESMO caminho de injecao das duas fontes;
- passagem de bastao: quando o driver sinaliza que quer o aparelho, solta SO O
  APARELHO. A porta fica;
- se o aparelho cair (desplugar), reconecta sozinho e a porta continua existindo;
- registra tudo em %LOCALAPPDATA%\\BCD3000Bridge\\bridge.log

Empacotavel com PyInstaller (--onefile --noconsole).
"""
import os, sys, time, tempfile, threading, queue, ctypes as C, logging
from ctypes import wintypes as W
from winusb_bcd import open_dev_full, close_dev, read_ctrl, write_led

PORT_NAME = "BCD3000"

# TABELA DE CIN (code index number) da USB-MIDI 1.0: quantos bytes de MIDI de fio
# cada pacote de 4 bytes carrega. *** TODOS os controles do aparelho passam por
# ela *** - e o unico lugar do sistema que sabe o tamanho das mensagens, desde que
# o driver deixou de converter e passou a repassar pacotes crus.
#
# Congelada pelo autoteste deste arquivo (`--autoteste`), entrada por entrada. O
# teste que a protegia antes vivia no tests.cpp do lado C++ e foi apagado junto com
# as funcoes de conversao que viraram codigo morto na Tarefa 10 - e a tabela viva
# ficou sem teste em lugar nenhum. Editar um valor aqui nao quebra nada em tempo de
# execucao: faz ALGUMAS mensagens funcionarem e outras nao, que e exatamente o
# defeito que a arquitetura de caminho unico existe para evitar.
#
# 0x0 e 0x1 sao RESERVADOS na especificacao e de proposito NAO estao aqui: quem os
# receber cai no default de 3 bytes do `.get`, e o teste do bit de status um passo
# depois decide. Ver injetar_pacote().
CIN_LEN = {0x5:1, 0xF:1, 0x2:2, 0x6:2, 0xC:2, 0xD:2,
           0x3:3, 0x4:3, 0x7:3, 0x8:3, 0x9:3, 0xA:3, 0xB:3, 0xE:3}

# ---- log ----
# O `--autoteste` desvia o diretorio de log para um temporario ANTES de o logging
# abrir o arquivo. Sem isto, rodar o autoteste na maquina do usuario abriria (e, se
# algum caminho registrasse algo, escreveria em) o bridge.log de verdade - que e a
# EVIDENCIA do portao de hardware desta tarefa, contada por linha.
_autoteste = "--autoteste" in sys.argv
_logdir = (os.path.join(tempfile.gettempdir(), "BCD3000Bridge-autoteste") if _autoteste
           else os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")),
                             "BCD3000Bridge"))
os.makedirs(_logdir, exist_ok=True)
logging.basicConfig(filename=os.path.join(_logdir, "bridge.log"), level=logging.INFO,
                    format="%(asctime)s %(levelname)s %(message)s")
def log(m): logging.info(m)

# ---- passagem de bastao com o driver ASIO ----
# O aparelho aceita UM processo por vez (medido: com este programa rodando, o
# CreateFile do driver devolve erro 5). Quando o software de DJ liga o audio, o
# BcdAsio.dll precisa do aparelho INTEIRO - IF1, IF2 e IF3 - e sinaliza um evento
# nomeado do Windows.
#
# A PORTA VIRTUAL NAO ENTRA MAIS NESTA NEGOCIACAO. Ela e nossa do inicio ao fim
# da execucao; o bastao passa apenas o aparelho.
#
# Os dois nomes sao os mesmos do lado C++ (native/bcdasio/handoff.h, kEventName e
# kEventNameLocal, congelados por teste em tests.cpp). O driver cria e sinaliza os
# DOIS; este lado faz um OU sobre os dois. Como os dois lados cobrem os dois
# nomes, o encontro NAO depende de ordem nem de elevacao - e essa e a unica forma
# em que ele nao depende: medido nesta maquina, um software de DJ iniciado de um
# shell elevado cria no 'Global\', e um BCD3000Bridge.exe iniciado pelo Windows no
# login roda SEM elevacao e pode nao ter permissao de abrir esse objeto. O nome
# local nao exige privilegio nenhum e e o que sempre converge.
EVENT_NAME       = "Global\\BCD3000_DriverWantsDevice"
EVENT_NAME_LOCAL = "BCD3000_DriverWantsDevice"
SYNCHRONIZE   = 0x00100000
WAIT_OBJECT_0 = 0
ERROR_ACCESS_DENIED = 5

# Passo de reavaliacao do pedido, em segundos. Igual ao kHandoffRetryMs de 200 ms
# do lado C++ de proposito: e o passo em que o driver retenta tomar o aparelho, e
# nenhum sono deste programa pode ser mais grosso que ele enquanto o aparelho
# estiver na nossa mao (ver dormir_atento).
HANDOFF_STEP_S = 0.2

_k32 = C.WinDLL("kernel32", use_last_error=True)
_k32.OpenEventW.restype = W.HANDLE
_k32.OpenEventW.argtypes = [W.DWORD, W.BOOL, W.LPCWSTR]
_k32.WaitForSingleObject.restype = W.DWORD
_k32.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
_k32.CloseHandle.argtypes = [W.HANDLE]
_k32.CloseHandle.restype = W.BOOL

_acesso_negado_visto = set()   # nomes cujo erro 5 ja foi para o log (uma vez cada)

def _registrar_acesso_negado(nome):
    """Registra UMA VEZ por nome quando o OpenEventW falhou com erro 5.

    Sem isto, "o evento nao existe" (o caso normal, sem audio ligado) e "o evento
    existe e eu NAO consigo abri-lo" sao indistinguiveis deste lado - e o segundo
    e justamente o caso que precisa de diagnostico. Uma vez por nome, porque a
    consulta roda ~5x/s: repetir encheria o log sem informacao nova.

    O codigo de erro e lido ANTES de qualquer outra coisa: get_last_error() devolve
    o valor guardado pela ULTIMA chamada ctypes desta thread.
    """
    if C.get_last_error() != ERROR_ACCESS_DENIED:
        return
    if nome in _acesso_negado_visto:
        return
    _acesso_negado_visto.add(nome)
    log(f"evento do bastao '{nome}' EXISTE mas nao consigo abri-lo (erro 5, acesso "
        f"negado): o software de DJ provavelmente roda ELEVADO e este programa nao. "
        f"O nome local nao exige privilegio e deve resolver; se ele tambem falhar, "
        f"a passagem de bastao nao vai acontecer e os controles ficam mudos com o "
        f"audio ligado")

def driver_quer_aparelho():
    """True se o driver ASIO sinalizou que quer o aparelho inteiro.

    OU sobre os DOIS nomes: verdadeiro se QUALQUER um estiver sinalizado. Parar no
    primeiro nome que se consegue abrir e devolver o estado DELE estaria errado -
    se o 'Global\\' existir sem estar sinalizado (permissao restritiva, ou criado
    por outro processo) enquanto o pedido vivo esta no nome local, essa forma
    responderia False e nunca olharia o outro, e a passagem de bastao nunca
    aconteceria. O driver cria e sinaliza os dois (native/bcdasio/handoff.cpp),
    entao qualquer um sinalizado e pedido vivo.

    O handle e aberto e FECHADO em cada consulta, e isso e LOAD-BEARING - nao e
    desperdicio a ser otimizado. E o que faz a recuperacao automatica funcionar
    quando o driver morre sem devolver: o Windows destroi o evento quando o
    ULTIMO handle se fecha, e o unico handle permanente e o do proprio driver.
    Se guardassemos um handle aberto aqui, o objeto sobreviveria a morte do
    driver AINDA SINALIZADO, e este programa nunca mais retomaria o aparelho.
    Todo handle aberto aqui e fechado no `finally`, inclusive na saida por True.

    Evento inexistente = ninguem pediu nada (o caso normal, sem audio ligado).
    """
    for nome in (EVENT_NAME, EVENT_NAME_LOCAL):
        h = _k32.OpenEventW(SYNCHRONIZE, False, nome)
        if not h:
            _registrar_acesso_negado(nome)
            continue
        try:
            if _k32.WaitForSingleObject(h, 0) == WAIT_OBJECT_0:
                return True    # um escopo sinalizado basta
        finally:
            _k32.CloseHandle(h)
    return False

def dormir_atento(total):
    """Dorme ate `total` segundos EM FATIAS, e volta na hora se o driver pedir o
    aparelho. Devolve True se saiu por causa do pedido.

    Um `time.sleep(total)` inteiro custa de um jeito que nao aparece em erro
    nenhum: o driver tem 15 tentativas de 200 ms para tomar o aparelho, e um unico
    sono de 2 s consome 2.000 dos 2.800 ms de orcamento antes de este laco sequer
    reavaliar o pedido. Desde que a porta virtual saiu da negociacao, o orcamento
    ficou mais folgado - o unico recurso disputado e o aparelho -, mas a razao
    continua valendo: enquanto seguramos os handles do aparelho, o driver esta
    esperando por nos.

    A checagem vem ANTES da primeira fatia, entao "pedido ja feito" tambem volta
    na hora - era o unico caso que a versao anterior cobria.
    """
    fim = time.monotonic() + total
    while True:
        if driver_quer_aparelho():
            return True
        restante = fim - time.monotonic()
        if restante <= 0:
            return False
        time.sleep(min(HANDOFF_STEP_S, restante))

# ---- canal local de MIDI com o driver ASIO ----
#
# ESTE PROGRAMA E O SERVIDOR E O DRIVER E O CLIENTE, e isso e necessidade MEDIDA,
# nao estilo: este programa roda SEM elevacao (atalho na pasta Inicializar do
# usuario) e o software de DJ roda COM elevacao - foi ele que conseguiu criar
# objeto no escopo 'Global\', o que exige privilegio. Um processo elevado abre sem
# problema um objeto criado por um nao elevado; o caminho inverso e o que costuma
# ser barrado. Invertidos os papeis, isto funcionaria nesta maquina e falharia na
# de outra pessoa.
#
# Os quatro valores abaixo sao contrato com native/bcdasio/midibridge.h, e mudar
# um deles sem mudar o outro lado nao daria erro em lugar nenhum - so faria os
# controles ficarem mudos. O nome esta congelado por teste nos dois lados.
RELAY_PIPE_NAME = r"\\.\pipe\BCD3000MidiRelay"
RELAY_PACKET_BYTES = 4
# Maior mensagem que o driver manda: uma transferencia USB inteira, e ele le o
# EP 0x81 com buffer de 64 bytes. 512 e folga de 8x.
RELAY_READ_BYTES = 512
# Buffers do pipe, um por sentido: 4096 bytes = 1024 pacotes.
RELAY_BUF_BYTES = 4096

# Passo do laco do canal quando nao ha nada a fazer. O laco e de POLLING de
# proposito, e o motivo e ciclo de vida e nao preguica: assim UM SO thread e dono
# do handle do canal, do inicio ao fim. A alternativa - um thread lendo e outro
# escrevendo - exigiria que um deles fechasse o handle enquanto o outro pode estar
# dentro de uma chamada bloqueante, que e exatamente a classe de defeito que esta
# sessao ja encontrou duas vezes no lado C++.
# Custo: no maximo 3 ms de atraso num pacote de controle ou de LED (imperceptivel
# num controlador de DJ) e ~0,3 a 1% de um nucleo, SO enquanto o driver esta
# conectado - passo de 3 ms da ~330 voltas/s, cada uma com um PeekNamedPipe e um
# get_nowait que levanta excecao, na ordem de 10 a 30 us por volta. (O numero
# anterior, ~0,2%, estava baixo; a conclusao - desprezivel, e so enquanto conectado -
# nao muda.) Sem driver conectado o thread fica parado no ConnectNamedPipe, com zero
# CPU.
RELAY_POLL_S = 0.003
# Fila de LEDs entre o callback da porta virtual e o thread do canal. Existe para
# que o callback NUNCA bloqueie: ele e chamado por um thread do proprio MIDI do
# Windows, e prende-lo prenderia o software de DJ.
RELAY_LED_FILA = 512
# Teto de LEDs por volta do laco, para que uma rajada de LED nao deixe os
# controles esperando.
RELAY_LED_POR_VOLTA = 64

PIPE_ACCESS_DUPLEX        = 0x00000003
PIPE_TYPE_MESSAGE         = 0x00000004
PIPE_READMODE_MESSAGE     = 0x00000002
PIPE_WAIT                 = 0x00000000
PIPE_REJECT_REMOTE_CLIENTS = 0x00000008
INVALID_HANDLE_VALUE = C.c_void_p(-1).value
ERROR_PIPE_CONNECTED = 535

_k32.CreateNamedPipeW.restype = W.HANDLE
_k32.CreateNamedPipeW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, W.DWORD,
                                  W.DWORD, W.DWORD, W.DWORD, C.c_void_p]
_k32.ConnectNamedPipe.restype = W.BOOL
_k32.ConnectNamedPipe.argtypes = [W.HANDLE, C.c_void_p]
_k32.DisconnectNamedPipe.restype = W.BOOL
_k32.DisconnectNamedPipe.argtypes = [W.HANDLE]
_k32.PeekNamedPipe.restype = W.BOOL
_k32.PeekNamedPipe.argtypes = [W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD),
                               C.POINTER(W.DWORD), C.POINTER(W.DWORD)]
_k32.ReadFile.restype = W.BOOL
_k32.ReadFile.argtypes = [W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.c_void_p]
_k32.WriteFile.restype = W.BOOL
_k32.WriteFile.argtypes = [W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.c_void_p]

_fila_led = queue.Queue(maxsize=RELAY_LED_FILA)

# Contadores de diagnostico. Sao tocados por dois threads sem trava de proposito:
# servem so para as linhas de log e o pior caso e uma contagem um pouco baixa.
#
# `inj_filtrados` e `inj_erros` sao os dois motivos pelos quais um pacote entra em
# injetar_pacote() e nao sai na porta, e existem separados de proposito: sem eles,
# "pacotes - injetados" some tudo numa conta so e nao responde a pergunta que
# interessa. Hoje ha uma contradicao publicada que ninguem consegue resolver - um
# comentario afirma que o enchimento 00 00 00 00 e o pacote MAIS COMUM de todos no
# EP 0x81, e a medicao do portao deu pacotes == injetados em 4.706 pacotes, ou seja
# ZERO descartes. Uma das duas afirmacoes esta errada, e o contador de filtrados diz
# qual: se o enchimento chegasse pelo canal, ele apareceria aqui.
_cont = {"canal_pkts": 0, "canal_inj": 0, "led_canal": 0, "led_usb": 0,
         "led_perdidos": 0, "led_erros": 0, "inj_filtrados": 0, "inj_erros": 0}

# ---- BcdMidi.dll (porta virtual pelo MIDI do Windows) ----
# A falha de carga e TRATADA, e nao propagada, por duas razoes independentes:
#  (1) o executavel e empacotado com --noconsole. Uma excecao aqui mataria o processo
#      ANTES de qualquer linha de log, e o usuario nao teria como descobrir por que os
#      controles nao funcionam - a unica pista seria o processo nao existir. Com o
#      tratamento, run() escreve o motivo no bridge.log e devolve codigo de erro;
#  (2) e o que deixa o `--autoteste` rodar numa maquina SEM a DLL. As funcoes
#      que o autoteste exercita (CIN_LEN, midi_to_usb, injetar_pacote) nao precisam
#      dela: o injetor troca `bcdmidi` por um gravador.
# CALLBACK fica FORA do try de proposito: C.CFUNCTYPE e do proprio ctypes e nao
# depende de DLL nenhuma, e _cb precisa existir sempre.
_bcdmidi_erro = None

# Onde o BcdMidi.dll (e a Windows.Devices.Midi2.dll da Microsoft, ao lado dele)
# moram de verdade. Empacotado (--onefile, runtime_tmpdir=None), e o diretorio do
# payload descompactado - sys._MEIPASS, que NAO e o diretorio do .exe -, arranjo
# medido e decidido na Tarefa 2 (ver poc/BCD3000Bridge.spec). Fora do pacote
# (desenvolvimento), e native/bcdmidi/ ao lado deste repositorio, que e onde
# native/bcdmidi/build.bat deixa a DLL.
if getattr(sys, "frozen", False):
    _BCDMIDI_DIR = sys._MEIPASS
    # ADITIVO: esta chamada usa AddDllDirectory, que NAO mexe no
    # SetDllDirectory. O bootloader do PyInstaller ja chamou
    # SetDllDirectoryW(_MEIPASS) antes de este modulo rodar - a Tarefa 2 mediu
    # que E ISSO que torna a Windows.Devices.Midi2.dll da Microsoft achavel de
    # dentro do pacote -, mas essa chamada do bootloader NAO E DOCUMENTADA e
    # guarda exatamente UM diretorio: qualquer biblioteca carregada depois que
    # chame SetDllDirectory para o proprio uso apaga _MEIPASS em silencio, e a
    # criacao da porta passa a falhar sem erro nenhum que aponte para a causa.
    # add_dll_directory nao tem esse modo de falha. Chamada ANTES da primeira
    # carga do BcdMidi.dll, como a Tarefa 2 recomenda. Guardada por
    # getattr(sys, "frozen") porque sys._MEIPASS so existe dentro do pacote.
    os.add_dll_directory(_BCDMIDI_DIR)
else:
    _BCDMIDI_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "native", "bcdmidi")

# use_last_error=True NAO e enfeite e nao pode ser removido: e o que faz
# C.get_last_error() devolver o erro DESTA biblioteca e nao o de uma chamada
# anterior de outra WinDLL qualquer. Medido em 2026-08-01, com duas WinDLL de
# kernel32 lado a lado: uma chamada por uma DLL carregada SEM a opcao deixou
# C.get_last_error() em 2 (o valor de uma chamada anterior, de outra DLL)
# enquanto o GetLastError do sistema dizia 3. NOTA para quem le isto depois da
# troca de biblioteca: o BcdMidi.dll NAO reporta os seus proprios erros pelo
# GetLastError do Windows - eles vem pelos dois ponteiros de saida de
# BcdMidiCreatePort (errOut, hrOut), lidos direto do retorno da chamada em
# criar_porta(). C.get_last_error() portanto NAO e a fonte do diagnostico da
# porta neste arquivo; a opcao fica mesmo assim porque as outras WinDLL deste
# processo (o _k32 acima, e as tres do winusb_bcd) dependem dela para os
# proprios erros delas.
try:
    bcdmidi = C.WinDLL(os.path.join(_BCDMIDI_DIR, "BcdMidi.dll"), use_last_error=True)
    bcdmidi.BcdMidiCreatePort.restype = C.c_void_p
    bcdmidi.BcdMidiCreatePort.argtypes = [W.LPCWSTR, C.c_void_p, C.c_void_p,
                                          C.POINTER(C.c_uint), C.POINTER(C.c_long)]
    bcdmidi.BcdMidiClosePort.argtypes = [C.c_void_p]
    bcdmidi.BcdMidiSend.argtypes = [C.c_void_p, C.POINTER(C.c_ubyte), W.DWORD]
    bcdmidi.BcdMidiSend.restype = C.c_int
    bcdmidi.BcdMidiErrorText.argtypes = [C.c_uint]
    bcdmidi.BcdMidiErrorText.restype = C.c_char_p
except OSError as _e:
    bcdmidi = None
    _bcdmidi_erro = str(_e)

# A CALLBACK de recepcao tem 3 argumentos - contrato de
# native/bcdmidi/bcdmidi.h (BcdMidiRecvCb): (user, bytes, count). O ponteiro
# `bytes` so vale durante a chamada, por isso rx_callback copia os bytes antes
# de devolver o controle.
CALLBACK = C.CFUNCTYPE(None, C.c_void_p, C.POINTER(C.c_ubyte), C.c_uint)

if3 = None  # handle da IF3 do aparelho (None quando desconectado)
dev = None  # pacote opaco de handles para close_dev (None quando desconectado)

# A porta virtual. Criada UMA VEZ em abrir_porta_uma_vez() e zerada so em
# encerrar_porta(), no fim do processo.
port_atual = None

# Serializa a escrita de LED contra o fechamento dos handles do aparelho. O
# callback do BcdMidi.dll roda numa thread do proprio servico do MIDI do
# Windows; sem esta trava, o fechamento poderia acontecer entre o `h = if3` do
# callback e o WritePipe dele, e o WritePipe usaria um valor de handle que o
# Windows ja pode ter reciclado.
_led_lock = threading.Lock()

# Serializa a INJECAO na porta virtual e protege a troca de port_atual. Tres
# razoes, a terceira nova nesta troca de biblioteca:
#  (1) sao duas fontes possiveis de injecao - o laco de leitura do aparelho e o
#      thread do canal - e ordenacao de mensagens MIDI por porta e desejavel;
#  (2) o fechamento da porta no fim do processo nao pode acontecer no meio de um
#      BcdMidiSend de outro thread - o proprio DLL NAO serializa isso: ver
#      bcdmidi.h, "It is NOT safe to call [BcdMidiSend] while another thread is
#      inside BcdMidiClosePort on the same handle: close frees the port". Essa
#      garantia e responsabilidade NOSSA, e e o que esta trava entrega, presa
#      durante toda a chamada de BcdMidiSend e nao so na leitura de port_atual;
#  (3) BcdMidiSend BLOQUEIA - entrega a mensagem para a thread da porta e
#      espera o servico aceitar, ao contrario do envio da biblioteca anterior,
#      que so copiava. Ver a nota de latencia em injetar_pacote.
# NUNCA chamar BcdMidiClosePort segurando esta trava junto de _led_lock: o
# close espera pelo callback de LED em voo, e o callback espera por _led_lock.
_port_lock = threading.Lock()

def midi_to_usb(msg):
    if not msg: return b""
    st = msg[0]
    if 0x80 <= st < 0xF0:
        d1 = msg[1] if len(msg) > 1 else 0
        d2 = msg[2] if len(msg) > 2 else 0
        return bytes([st >> 4, st, d1, d2])
    if st >= 0xF8:
        return bytes([0x0F, st, 0, 0])
    return b""

def injetar_pacote(pkt):
    """CAMINHO UNICO de injecao de um pacote USB-MIDI de 4 bytes na porta virtual.

    Os DOIS leitores passam por aqui: o laco que le o aparelho quando este
    programa o tem, e o thread do canal quando o driver ASIO o tem. Duas copias
    desta funcao divergiriam com o tempo, e a divergencia apareceria como
    "algumas mensagens funcionam num modo e nao no outro" - o pior tipo de
    defeito para diagnosticar.

    Aqui e onde o enchimento do aparelho morre: o nibble baixo do primeiro byte e
    o code index number, que diz o tamanho da mensagem, e toda mensagem MIDI de
    verdade comeca com o bit de status ligado. Enchimento 00 00 00 00 falha no
    segundo teste.

    Devolve True se injetou DE FATO - ou seja, se a porta virtual aceitou os bytes.
    Antes devolvia True depois de CHAMAR o envio da biblioteca anterior, descartando
    o retorno dela, e com isso o contador de injetados contava TENTATIVA e nao
    sucesso. Isso importa porque `injetados` e coluna de criterio de portao: uma
    porta que parasse de aceitar ficaria indistinguivel de uma sessao perfeita.
    BcdMidiSend preserva essa mesma propriedade: nao-zero significa que os bytes
    SAIRAM, nao que foram so enfileirados - ver bcdmidi.h.

    NOTA DE LATENCIA (Tarefa 4): BcdMidiSend BLOQUEIA - entrega a mensagem a thread
    da porta e espera o servico aceitar, ao contrario do envio da biblioteca
    anterior, que so copiava e devolvia na hora. Isso significa que _port_lock fica preso pela
    duracao de uma volta real entre threads, e nao mais de uma copia. Medido na
    Tarefa 3 (via WinMM, contaminado pela granularidade do polling do proprio
    self-test): 31 ms e 16 ms para a mensagem chegar - a duracao do PROPRIO
    BcdMidiSend nunca foi isolada e pode ser bem menor. Mantido dentro da trava de
    proposito: e a unica coisa que torna o fechamento da porta seguro contra este
    envio (ver o comentario de _port_lock). Se a fila de LEDs do aparelho um dia
    virar fonte de rajadas SOBRE este mesmo caminho, o custo cumulativo dessas
    esperas passa a valer medir - hoje o caminho de LED nao chama injetar_pacote.
    """
    if len(pkt) < RELAY_PACKET_BYTES:
        _cont["inj_filtrados"] += 1
        return False
    cin = pkt[0] & 0x0F
    ln = CIN_LEN.get(cin, 3)
    m = bytes(pkt[1:1 + ln])
    if not m or (m[0] & 0x80) == 0:
        # AQUI morre o enchimento 00 00 00 00 do aparelho, e e este contador que
        # torna esse descarte visivel em vez de deduzido.
        _cont["inj_filtrados"] += 1
        return False
    arr = (C.c_ubyte * len(m)).from_buffer_copy(m)
    with _port_lock:
        p = port_atual
        if p is None:
            return False
        enviado = bool(bcdmidi.BcdMidiSend(p, arr, len(m)))
    # O log fica FORA da trava: injetar_pacote roda no caminho dos controles, e
    # nenhuma escrita em arquivo precisa segurar a trava da porta.
    if not enviado:
        _cont["inj_erros"] += 1
        if _cont["inj_erros"] <= 5 or _cont["inj_erros"] % 500 == 0:
            log(f"injecao na porta virtual falhou #{_cont['inj_erros']} "
                f"(BcdMidiSend devolveu falso; a porta ainda existe?)")
    return enviado

def _escrever_led_no_aparelho(pkt):
    """Escreve o LED direto no USB. Devolve True se O APARELHO E NOSSO.

    Devolver True numa FALHA de escrita e deliberado: se o aparelho e nosso, o
    driver ASIO nao o tem, e mandar o pacote pelo canal seria mandar para um
    processo que nao consegue escrever nele. Falha de escrita e falha, nao motivo
    para desviar o trafego.
    """
    if if3 is None:
        return False
    with _led_lock:
        # Reler if3 sob a trava: quem solta o aparelho zera if3 ANTES de tomar a
        # trava e fechar, entao aqui ou o handle ainda e valido ou if3 ja e None e
        # nao se escreve nada.
        h = if3
        if h is None:
            return False
        try:
            write_led(h, pkt)
        except Exception as e:
            _cont["led_erros"] += 1
            if _cont["led_erros"] <= 5 or _cont["led_erros"] % 500 == 0:
                log(f"escrita de LED no aparelho falhou #{_cont['led_erros']} "
                    f"(aparelho caiu?): {e}")
            return True
        _cont["led_usb"] += 1
        return True

def _enfileirar_led_para_o_canal(pkt):
    """Poe o LED na fila do canal SEM NUNCA BLOQUEAR.

    O callback do BcdMidi.dll roda numa thread do servico do MIDI do Windows;
    prende-lo prenderia o software de DJ. Fila cheia significa que o driver parou de drenar o canal por
    segundos - descartar e a saida certa, e o contador diz que aconteceu.

    QUAL pacote se descarta importa, e a versao anterior descartava o ERRADO. Com a
    fila cheia ela jogava fora o pacote NOVO e entregava depois 512 estados de VU
    vencidos; para VU o inutil e o MAIS VELHO, porque o estado seguinte o
    sobrescreve. Agora a fila anda: tira o mais velho e poe o novo. So importa no
    caso ja degradado (o driver parou de drenar por segundos), mas estava do lado
    errado, e o custo de acertar e uma linha.
    """
    try:
        _fila_led.put_nowait(pkt)
        return
    except queue.Full:
        pass
    _cont["led_perdidos"] += 1
    if _cont["led_perdidos"] == 1 or _cont["led_perdidos"] % 1000 == 0:
        log(f"fila de LED do canal cheia; {_cont['led_perdidos']} pacotes mais "
            f"VELHOS descartados para dar lugar aos novos (o driver ASIO parou de "
            f"ler o canal?)")
    # Abrir espaco jogando fora o mais velho. Os dois `except` cobrem a corrida com
    # o thread do canal, que drena em paralelo: ele pode ter esvaziado a fila entre o
    # put e o get (Empty), ou tornado a enche-la antes do segundo put (Full). Nos dois
    # casos a saida certa e desistir DESTE pacote sem levantar - o callback nunca pode
    # propagar excecao para dentro do BcdMidi.dll.
    try:
        _fila_led.get_nowait()
        _fila_led.put_nowait(pkt)
    except (queue.Empty, queue.Full):
        pass

def rx_callback(user, data_ptr, length):
    """LEDs vindos do software de DJ -> aparelho, direto ou pelo canal.

    TRES argumentos, nao quatro: o contrato de BcdMidiRecvCb (bcdmidi.h) e
    (user, bytes, count) - o `port` e o `inst` da biblioteca anterior nao tem
    equivalente aqui. `user` e o mesmo ponteiro passado em BcdMidiCreatePort
    (None, aqui - nao precisamos dele) e NUNCA usado para achar a porta: o
    codigo abaixo so fala com o aparelho e com a fila do canal.

    RODA NUMA THREAD DO SERVICO DO MIDI DO WINDOWS, nao numa thread deste
    programa (bcdmidi.h e explicito quanto a isso). Por isso o corpo inteiro
    esta dentro do try: uma excecao Python nao pode atravessar a fronteira de
    volta para C - ela seria, na melhor das hipoteses, impressa no lugar errado
    e, na pior, derrubaria o processo do servico. Ela e engolida e vira uma
    linha de log, nunca escapa daqui.
    """
    try:
        pkt = midi_to_usb(bytes(bytearray(data_ptr[:length])))
        if not pkt:
            return
        if _escrever_led_no_aparelho(pkt):
            return
        # O aparelho nao e nosso: quem o tem e o driver ASIO. Vai pelo canal.
        _enfileirar_led_para_o_canal(pkt)
    except Exception as e:
        log(f"callback de LED falhou: {e}")

# Guardada em globals do modulo, que vivem tanto quanto o processo: o objeto
# CFUNCTYPE tem de sobreviver por TODO o tempo em que a porta existir. Se a
# unica referencia fosse local a alguma funcao, o coletor de lixo do Python
# poderia recolher o trampolim enquanto o servico do MIDI do Windows ainda
# tem o ponteiro - e a proxima mensagem recebida chamaria memoria ja liberada.
_cb = CALLBACK(rx_callback)

def criar_porta():
    """Cria a porta virtual. Devolve (porta, categoria de erro, HRESULT).

    A porta e None quando a criacao falha, e ai o segundo e o terceiro item sao
    os dois ponteiros de saida de BcdMidiCreatePort (bcdmidi.h): a CATEGORIA e o
    HRESULT completo. Sem eles esta funcao devolveria so `None` e o motivo seria
    jogado fora; o custo de jogar fora o motivo, com a biblioteca anterior, foi
    uma hora de diagnostico em 2026-08-01, procurando um conflito de nome que nao
    existia.

    NAO E C.get_last_error(): o BcdMidi.dll nao reporta os proprios erros pelo
    GetLastError do Windows. Os dois valores saem DIRETO do retorno desta
    chamada, nos dois ponteiros passados por referencia - e por isso nao ha
    "linha imediatamente seguinte" a se preocupar aqui: os valores ja estao nas
    variaveis Python antes de qualquer outra chamada ctypes ter chance de
    escrever por cima de nada.

    Em sucesso os dois sao 0, exatamente como a DLL promete (bcdmidi.h: "On
    success both are set to 0 ... Neither is ever left holding a stale value").
    """
    if bcdmidi is None:
        return None, 0, 0
    err = C.c_uint(0)
    hr = C.c_long(0)
    p = bcdmidi.BcdMidiCreatePort(PORT_NAME, _cb, None, C.byref(err), C.byref(hr))
    return p, err.value, hr.value

# Categorias com causa NOMEADA por ESTE contrato (native/bcdmidi/bcdmidi.h) -
# a nossa propria enumeracao, e nao mais codigos do GetLastError de uma DLL de
# terceiro. Diferente da tabela antiga (so tres codigos tinham causa medida, o
# resto era desconhecido), aqui as CINCO categorias de falha tem redacao propria:
# o cabecalho da DLL documenta o que cada uma significa, entao nao ha "chute"
# em nomea-las. So a excecao (6) e o "fora do enum" continuam sem causa unica.
NOME_DA_CATEGORIA = {
    1: "kBcdMidiServiceMissing",
    2: "kBcdMidiTransportMissing",
    3: "kBcdMidiCreateFailed",
    4: "kBcdMidiOpenFailed",
    5: "kBcdMidiBadArgument",
    6: "kBcdMidiException",
}

def _fmt_hr(hr):
    """O HRESULT como 8 digitos hexadecimais, sem depender do sinal do long.

    ctypes entrega um c_long com sinal; um HRESULT de falha tem o bit 31 ligado
    e chega como um Python int NEGATIVO (ex.: -2147023436). O `& 0xFFFFFFFF`
    devolve a mesma representacao de 32 bits sem sinal - Python faz aritmetica
    de precisao arbitraria, entao o AND bit a bit com uma mascara de 32 bits
    reproduz o complemento de dois corretamente, para positivo ou negativo.
    """
    return f"0x{hr & 0xFFFFFFFF:08X}"

def _diagnostico_falha_da_porta(erro, hr):
    """A linha de log de uma falha de criacao da porta, escolhida pela CATEGORIA e
    pelo HRESULT reais.

    ESTA FUNCAO EXISTE POR UM DEFEITO MEDIDO no hardware em 2026-08-01, com a
    biblioteca anterior: a mensagem unica que existia afirmava conflito de nome
    sem nunca ter olhado o codigo de erro, e a causa medida era outra
    inteiramente. Uma mensagem que CHUTA a causa e pior que uma que diz "nao
    sei", porque uma hora de busca foi gasta acreditando nela - e essa e a razao
    de o ramo final aqui embaixo NAO inventar nada.

    TRES RAMOS, o mesmo formato de antes:
      - causa NOMEADA (categorias 1 a 5 do nosso proprio enum - bcdmidi.h);
      - EXCECAO (categoria 6): a causa e o HRESULT completo, que e tudo o que
        BcdMidi.dll sabe dizer sobre uma excecao do WinRT - ver bcdmidi.h;
      - DESCONHECIDO: qualquer categoria fora do enum. Nao inventa nada.

    O NUMERO DA CATEGORIA E O HRESULT SAEM EM TODOS OS RAMOS, sem excecao -
    "erro {erro}" primeiro (o formato que o autoteste confere), e o HRESULT
    sempre impresso por extenso, mesmo quando e 0 (bcdmidi.h: hrOut e sempre
    escrito, mesmo que seja 0 quando a falha nao tem HRESULT). Foi o numero nao
    sair em ramo nenhum que custou o que custou da vez passada.
    """
    nome = NOME_DA_CATEGORIA.get(erro)
    hexr = _fmt_hr(hr)
    if erro == 1:      # kBcdMidiServiceMissing
        return (f"nao consegui criar a porta '{PORT_NAME}' (erro {erro}, {nome}): o "
                f"servico MIDI do Windows nao esta disponivel nesta maquina. Confira "
                f"se o servico 'Windows MIDI Services' (MidiSrv) esta instalado e "
                f"rodando (services.msc). HRESULT {hexr}. Continuo tentando a cada 1 s")
    if erro == 2:      # kBcdMidiTransportMissing
        return (f"nao consegui criar a porta '{PORT_NAME}' (erro {erro}, {nome}): o "
                f"transporte de dispositivo virtual do MIDI do Windows nao esta "
                f"disponivel nesta maquina. HRESULT {hexr}. Continuo tentando a cada 1 s")
    if erro == 3:      # kBcdMidiCreateFailed
        return (f"nao consegui criar a porta '{PORT_NAME}' (erro {erro}, {nome}): o "
                f"servico MIDI do Windows nao devolveu um dispositivo virtual. Pode ser "
                f"o defeito conhecido do Windows (microsoft/MIDI, issue #1047), que so "
                f"um reinicio da maquina resolve, ou o servico ainda subindo. HRESULT "
                f"{hexr}. Continuo tentando a cada 1 s")
    if erro == 4:      # kBcdMidiOpenFailed
        return (f"nao consegui criar a porta '{PORT_NAME}' (erro {erro}, {nome}): a "
                f"conexao com o dispositivo virtual nao abriu. HRESULT {hexr}. "
                f"Continuo tentando a cada 1 s")
    if erro == 5:      # kBcdMidiBadArgument
        return (f"nao consegui criar a porta '{PORT_NAME}' (erro {erro}, {nome}): "
                f"argumento invalido para BcdMidiCreatePort - isto e defeito deste "
                f"programa, nao do ambiente do usuario. HRESULT {hexr}. Continuo "
                f"tentando a cada 1 s")
    if erro == 6:      # kBcdMidiException
        return (f"nao consegui criar a porta '{PORT_NAME}' (erro {erro}, {nome}): uma "
                f"excecao do WinRT aconteceu dentro do BcdMidi.dll. O HRESULT {hexr} e "
                f"a causa completa - e o unico dado que a DLL tem para uma excecao, "
                f"procure esse valor. Continuo tentando a cada 1 s")
    return (f"nao consegui criar a porta '{PORT_NAME}' (erro {erro}): motivo "
            f"DESCONHECIDO. Categoria fora do contrato de BcdMidi.dll (bcdmidi.h so "
            f"define 0 a 6); nao tenho causa medida para ela e nao vou adivinhar uma. "
            f"HRESULT {hexr}. Continuo tentando a cada 1 s")

def abrir_porta_uma_vez():
    """Cria a porta virtual e a publica. Insiste ate conseguir.

    *** ESTA E A UNICA FUNCAO DO PROGRAMA QUE CRIA A PORTA, e ela e chamada de UM
    SO lugar, antes do laco principal. A linha 'porta virtual ... criada' aparece
    portanto EXATAMENTE UMA VEZ por execucao, e isso e verificavel no log - e o
    criterio objetivo do portao de hardware desta arquitetura. ***

    Insistir em vez de desistir importa numa situacao concreta: um BcdAsio.dll
    ANTIGO carregado num software de DJ ja aberto ainda cria a porta com este nome.
    Nesse caso esperamos ele soltar, em vez de morrer e deixar o usuario sem
    controles para sempre. Insistir tambem cobre o caso de o servico MIDI do
    Windows ainda estar subindo, ou preso pelo defeito conhecido
    microsoft/MIDI #1047: o laco nao desiste, entao um reinicio da maquina ou do
    servico COM este programa no ar faz a porta nascer sozinha na volta seguinte.

    A falha vai ao log na PRIMEIRA vez e sempre que a CATEGORIA OU O HRESULT
    MUDAM, e nao uma vez para sempre. Repetir o mesmo par ~1x/s encheria o log
    sem informacao nova, mas uma MUDANCA e informacao: por exemplo, quando o
    servico MIDI do Windows termina de subir com o programa no ar. So a
    primeira linha faria o log continuar acusando uma causa que ja mudou.
    """
    global port_atual
    ultimo_par = None      # None e sentinela: nenhum par (erro, hr) e None, entao
                           # a primeira falha SEMPRE registra
    while True:
        p, erro, hr = criar_porta()
        if p:
            with _port_lock:
                port_atual = p
            log(f"porta virtual '{PORT_NAME}' criada")
            return
        par = (erro, hr)
        if par != ultimo_par:
            ultimo_par = par
            log(_diagnostico_falha_da_porta(erro, hr))
        time.sleep(1)

def encerrar_porta():
    """Fecha a porta virtual. SO no encerramento do processo.

    Copiar, ZERAR e so entao fechar. Assim um injetor concorrente ou ja terminou
    (a trava garante) ou ve None e desiste. E BcdMidiClosePort e chamada FORA das
    travas, por DUAS razoes agora:
      (1) ela espera pelo callback de LED em voo, e o callback espera por
          _led_lock - chamar de dentro de _led_lock seria travamento mutuo;
      (2) ela espera pelo callback de RECEPCAO em voo tambem (bcdmidi.h:
          "Always returns, even if the service stops answering"), e esse
          callback (rx_callback) toma _led_lock por dentro. Chamar o close
          segurando _led_lock faria o close esperar por um callback que esta
          esperando por quem o chamou - a DLL limita essa espera a cerca de 1 s,
          entao o pior caso hoje e um atraso e nao uma trava permanente, mas a
          regra continua sendo: NUNCA close com _led_lock (nem com _port_lock)
          seguro.
    """
    global port_atual
    with _port_lock:
        p = port_atual
        port_atual = None
    if p:
        bcdmidi.BcdMidiClosePort(p)

def soltar_aparelho():
    """Zera if3 e dev e fecha os handles do aparelho.

    A funcao e DONA das duas globais, como sempre foi de if3 - quem chama nao
    atribui nada de volta. Antes era `dev = soltar_aparelho(dev)`, e o par
    fechar-e-depois-atribuir tinha uma janela em que uma excecao assincrona entre as
    duas metades deixaria `dev` apontando para handles JA fechados, e o `finally`
    os fecharia de novo. Fechar handle duas vezes e pior que vazar: o valor pode ja
    ter sido reciclado pelo Windows para outro objeto.
    """
    global if3, dev
    # if3 e dev saem de circulacao ANTES do close: o callback de LED tira uma copia
    # de if3 e desiste se for None, e um segundo soltar_aparelho() nao acha mais
    # nada para fechar. A trava cobre o instante entre a copia e o WritePipe.
    if3 = None
    d   = dev
    dev = None
    with _led_lock:
        close_dev(d)

def _esvaziar_fila_led():
    """Joga fora os LEDs que sobraram de uma conexao anterior.

    Mandar estado de VU de 30 s atras para um driver que acabou de conectar nao
    ajuda ninguem, e a rajada atrasaria os LEDs de verdade.
    """
    while True:
        try:
            _fila_led.get_nowait()
        except queue.Empty:
            return

def _atender_um_driver(h):
    """Serve UM driver ASIO conectado, ate o canal quebrar.

    UM SO thread - este - e dono do handle `h`, e por isso nao existe aqui a
    pergunta "quem fecha o handle enquanto o outro esta dentro de uma chamada".
    Os dois sentidos sao multiplexados por consulta e nao por bloqueio.
    """
    ok = _k32.ConnectNamedPipe(h, None)
    if not ok and C.get_last_error() != ERROR_PIPE_CONNECTED:
        raise RuntimeError(f"ConnectNamedPipe erro {C.get_last_error()}")

    _esvaziar_fila_led()
    base = dict(_cont)
    log("canal: driver ASIO conectado; os controles agora vem dele")

    buf = (C.c_ubyte * RELAY_READ_BYTES)()
    disp = W.DWORD(0)
    n = W.DWORD(0)
    motivo = "cliente desconectou"
    try:
        while True:
            # ---- controles: driver -> nos -> porta virtual ----
            if not _k32.PeekNamedPipe(h, None, 0, None, C.byref(disp), None):
                motivo = f"PeekNamedPipe erro {C.get_last_error()}"
                return
            leu = False
            # `if disp.value:` e nao `>= RELAY_PACKET_BYTES`. A forma antiga
            # ENCALHARIA para sempre uma mensagem de 1 a 3 bytes: em modo mensagem o
            # PeekNamedPipe devolveria 1, 2 ou 3 disponiveis e a condicao nunca
            # abriria, entao o laco consultaria eternamente sem nunca LER, e a
            # mensagem seguinte ficaria presa atras dela. E inalcancavel pelo nosso
            # driver (RelayLink::send recusa tamanho que nao seja multiplo de 4), mas
            # depender de um invariante do outro processo para nao travar e barato de
            # evitar: qualquer coisa disponivel e lida, e o laco de troceamento
            # abaixo ja descarta um resto curto sozinho.
            if disp.value:
                if not _k32.ReadFile(h, buf, RELAY_READ_BYTES, C.byref(n), None):
                    motivo = f"ReadFile erro {C.get_last_error()}"
                    return
                got = n.value
                for i in range(0, got - (RELAY_PACKET_BYTES - 1), RELAY_PACKET_BYTES):
                    _cont["canal_pkts"] += 1
                    if injetar_pacote(bytes(buf[i:i + RELAY_PACKET_BYTES])):
                        _cont["canal_inj"] += 1
                leu = True

            # ---- LEDs: porta virtual -> nos -> driver ----
            enviados = 0
            while enviados < RELAY_LED_POR_VOLTA:
                try:
                    pkt = _fila_led.get_nowait()
                except queue.Empty:
                    break
                arr = (C.c_ubyte * RELAY_PACKET_BYTES).from_buffer_copy(pkt)
                if not _k32.WriteFile(h, arr, RELAY_PACKET_BYTES, C.byref(n), None):
                    motivo = f"WriteFile erro {C.get_last_error()}"
                    return
                _cont["led_canal"] += 1
                enviados += 1

            if not leu and enviados == 0:
                time.sleep(RELAY_POLL_S)
    finally:
        # `filtrados` e `injErros` saem como DELTA desta sessao de canal, igual as
        # outras colunas, e e o delta que os torna atribuiveis ao canal: enquanto o
        # driver esta conectado, o laco de leitura do aparelho nao roda (o aparelho e
        # dele), entao todo pacote que passou por injetar_pacote() nesta janela veio
        # daqui. `pacotes - injetados` nao substitui as duas colunas: ele soma
        # descarte pelo filtro com falha de injecao, e sao coisas diferentes - a
        # primeira e o funcionamento normal, a segunda e defeito.
        log(f"canal: driver ASIO desconectado ({motivo}); "
            f"pacotes={_cont['canal_pkts'] - base['canal_pkts']} "
            f"injetados={_cont['canal_inj'] - base['canal_inj']} "
            f"filtrados={_cont['inj_filtrados'] - base['inj_filtrados']} "
            f"injErros={_cont['inj_erros'] - base['inj_erros']} "
            f"leds={_cont['led_canal'] - base['led_canal']} "
            f"ledsPerdidos={_cont['led_perdidos'] - base['led_perdidos']}")

def servidor_do_canal():
    """Laco do servidor do canal. Uma instancia por vez, para sempre.

    nMaxInstances = 1 de proposito: so um processo por vez pode segurar o
    aparelho, entao so um driver por vez tem o que dizer. Um segundo cliente
    receberia ERRO de ocupado e retentaria, que e o comportamento certo - dois
    leitores no mesmo canal receberiam os pacotes de LED alternados.

    Seguranca: lpSecurityAttributes = NULL, ou seja o descritor PADRAO - so a
    propria conta e os administradores. Nao se usa DACL nula aqui. E
    PIPE_REJECT_REMOTE_CLIENTS porque o canal e estritamente local; um cliente de
    rede nao tem nada a fazer no MIDI de um controlador ligado por USB.

    RISCO ACEITO, escrito aqui para nao ser redescoberto como novo: NINGUEM VERIFICA
    QUEM E O CLIENTE. Qualquer processo da mesma conta pode conectar neste canal e
    mandar 4 bytes que serao INJETADOS na porta virtual como se viessem do aparelho.
    Por que se aceita: o dano e uma mensagem MIDI falsa no software de DJ da propria
    conta do usuario - quem consegue rodar codigo nessa conta tem caminhos bem mais
    diretos (a porta virtual e aberta por qualquer programa MIDI, inclusive) -, o
    tamanho e limitado a RELAY_READ_BYTES, e o troceamento em 4 bytes nao tem como
    desalinhar. nMaxInstances=1 tambem quer dizer que um invasor conectado NEGA o
    canal ao driver de verdade; o desfecho e "controles mudos", que e falha segura e
    identica ao caso de este programa estar parado.
    ENDURECIMENTO POSSIVEL, se algum dia valer o custo: GetNamedPipeClientProcessId
    aqui, e GetNamedPipeServerProcessId no lado do driver, comparando com o
    executavel esperado. NAO implementado - amarraria os dois lados a caminhos de
    executavel e nao fecharia nada que hoje cause dano real. O lado C++ ja fecha o
    unico item com dano serio (impersonacao) com SECURITY_IDENTIFICATION; ver
    native/bcdasio/midibridge.h.
    """
    falha_registrada = False
    while True:
        h = _k32.CreateNamedPipeW(
            RELAY_PIPE_NAME, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
            1, RELAY_BUF_BYTES, RELAY_BUF_BYTES, 0, None)
        if not h or h == INVALID_HANDLE_VALUE:
            if not falha_registrada:
                falha_registrada = True
                log(f"canal: nao consegui criar '{RELAY_PIPE_NAME}' (erro "
                    f"{C.get_last_error()}) - outro programa e o dono desse nome? "
                    f"Continuo tentando; o MIDI pela porta virtual segue "
                    f"funcionando enquanto o aparelho for meu")
            time.sleep(1)
            continue
        falha_registrada = False
        try:
            _atender_um_driver(h)
        except Exception as e:
            log(f"canal: {e}")
        finally:
            _k32.DisconnectNamedPipe(h)
            _k32.CloseHandle(h)

def run():
    global if3, dev
    log("=== BCD3000 Bridge iniciando ===")
    if bcdmidi is None:
        # Sem a DLL nao ha porta virtual, e sem porta virtual este programa
        # nao tem nada a fazer: o canal com o driver so existe para levar controles
        # ATE a porta. Sair com o motivo escrito e melhor que insistir num laco que
        # nunca vai conseguir nada, e MUITO melhor que morrer sem log.
        log(f"FATAL: BcdMidi.dll nao carregou de '{_BCDMIDI_DIR}' ({_bcdmidi_erro}). "
            f"Reinstale o programa - sem essa DLL nao existe porta MIDI virtual e os "
            f"controles nao chegam ao software de DJ")
        return 1
    dev = None
    try:
        # A PORTA PRIMEIRO, e uma vez so. Nada abaixo daqui a fecha.
        abrir_porta_uma_vez()

        # O servidor do canal e daemon: quando run() devolve, o processo termina e
        # o Windows fecha o handle do pipe. Nao ha desmontagem a fazer - e a porta,
        # que e o unico recurso com ordem de fechamento importante, e fechada no
        # `finally` abaixo, com trava.
        threading.Thread(target=servidor_do_canal, name="canal-midi",
                         daemon=True).start()

        while True:
            # ---- passagem de bastao: o driver quer o aparelho ----
            # A PORTA VIRTUAL NAO SAI DE CENA. Nem aqui, nem no laco de leitura
            # abaixo, nem em evento de cabo. So o APARELHO e devolvido. Foi
            # exatamente a devolucao da porta que produziu o defeito que esta
            # arquitetura existe para consertar - ver o topo do arquivo.
            if driver_quer_aparelho():
                # Nada a soltar aqui: se tinhamos o aparelho, quem o soltou foi o
                # laco de leitura abaixo. Este ramo cobre o caso de o pedido
                # aparecer enquanto estamos sem aparelho.
                time.sleep(HANDOFF_STEP_S)
                continue

            # ---- (re)conectar ao aparelho ----
            try:
                path, if3, dev = open_dev_full()
                log(f"aparelho conectado: {path}")
            except Exception as e:
                if3 = None
                dev = None
                # Aparelho ausente -> aguardar e tentar de novo. O sono e fatiado e
                # reavalia o pedido a cada 200 ms: o open_dev_full() falha de forma
                # transitoria logo depois de o driver devolver o aparelho
                # (reenumeracao do WinUSB), quando o pedido ja sumiu, e o software
                # de DJ pode religar o audio exatamente nesse intervalo.
                dormir_atento(2)
                continue

            # ---- loop de leitura dos controles ----
            try:
                while True:
                    if driver_quer_aparelho():
                        log("driver ASIO pediu o aparelho; soltando SO o aparelho "
                            f"(a porta virtual '{PORT_NAME}' fica comigo)")
                        raise RuntimeError("bastao passado ao driver ASIO")
                    data = read_ctrl(if3)
                    if not data:
                        continue
                    for i in range(0, len(data) - (RELAY_PACKET_BYTES - 1),
                                   RELAY_PACKET_BYTES):
                        injetar_pacote(data[i:i + RELAY_PACKET_BYTES])
            except Exception as e:
                log(f"aparelho solto ({e}); reconectando...")
                soltar_aparelho()
                # Fatiado pelo mesmo motivo do outro sono: um pedido que chegue
                # durante este segundo tem de ser visto na hora. Na saida por
                # passagem de bastao o pedido ja esta setado, entao isto volta
                # imediatamente em vez de dormir 1 s inteiro.
                dormir_atento(1)
    except KeyboardInterrupt:
        pass
    finally:
        # A porta e fechada SO aqui, no fim do processo.
        encerrar_porta()
        soltar_aparelho()
        log(f"=== BCD3000 Bridge encerrado (injetados do aparelho e do canal: "
            f"canal_pkts={_cont['canal_pkts']} canal_inj={_cont['canal_inj']} "
            f"inj_filtrados={_cont['inj_filtrados']} inj_erros={_cont['inj_erros']} "
            f"led_usb={_cont['led_usb']} led_canal={_cont['led_canal']} "
            f"led_perdidos={_cont['led_perdidos']} led_erros={_cont['led_erros']}) ===")
    return 0

# ---------------------------------------------------------------------------
# AUTOTESTE
#
#   python bridge_service.py --autoteste
#
# Roda SEM aparelho, SEM o BcdMidi.dll e SEM canal, e nao escreve no bridge.log de
# verdade (o diretorio de log e desviado no topo do arquivo quando este argumento
# esta presente).
#
# POR QUE ELE EXISTE: desde a Tarefa 10 a conversao de MIDI e o filtro de enchimento
# vivem SO deste lado - o driver repassa pacotes crus. Os testes que protegiam essa
# logica eram do lado C++ (test_tabela_de_cin e companhia) e foram apagados junto com
# as funcoes que viraram codigo morto, deixando CIN_LEN, midi_to_usb e o filtro de
# injetar_pacote *** sem teste em lugar nenhum ***. Um valor errado em CIN_LEN nao
# quebra nada visivelmente: faz algumas mensagens funcionarem e outras nao.
# ---------------------------------------------------------------------------
_ok = 0
_falhas = 0

def _check(cond, o_que):
    global _ok, _falhas
    if cond:
        _ok += 1
    else:
        _falhas += 1
        print(f"  FALHA: {o_que}")

class _BcdMidiGravador:
    """Substituto de `bcdmidi` que GRAVA em vez de injetar.

    Sem ele nao se distingue "o filtro descartou" de "nao havia porta": as duas
    coisas fazem injetar_pacote() devolver False. Assinatura igual a da unica funcao
    que injetar_pacote usa.
    """
    def __init__(self):
        self.enviados = []
    def BcdMidiSend(self, port, arr, tam):
        self.enviados.append(bytes(bytearray(arr[:tam])))
        return True

class _BcdMidiRecusando:
    """Substituto de `bcdmidi` cuja porta RECUSA tudo.

    Existe por um motivo especifico: a BcdMidiSend de verdade devolve int (nao-zero
    em sucesso), e esse retorno era descartado antes da Tarefa 9. Sem este
    substituto, o unico caminho de falha de injecao que existe no programa nao
    seria exercitado por nada - e caminho de excecao nao exercitado e onde esta
    sessao ja achou cinco defeitos.
    """
    def BcdMidiSend(self, port, arr, tam):
        return 0

def autoteste():
    global bcdmidi, port_atual
    print("== autoteste do bridge_service ==")

    # ---- 1. CIN_LEN: os 14 CINs definidos, UM POR UM ----
    # Escritos como literais e nao derivados da propria tabela: um teste que leia a
    # tabela para conferir a tabela nao prova nada.
    esperado = {0x2:2, 0x3:3, 0x4:3, 0x5:1, 0x6:2, 0x7:3, 0x8:3,
                0x9:3, 0xA:3, 0xB:3, 0xC:2, 0xD:2, 0xE:3, 0xF:1}
    _check(len(CIN_LEN) == 14, f"CIN_LEN tem de ter 14 entradas, tem {len(CIN_LEN)}")
    for cin, ln in sorted(esperado.items()):
        _check(CIN_LEN.get(cin) == ln,
               f"CIN 0x{cin:X} deveria carregar {ln} byte(s), veio {CIN_LEN.get(cin)}")
    # 0x0 e 0x1 sao RESERVADOS na USB-MIDI 1.0 e nao devem estar na tabela.
    _check(0x0 not in CIN_LEN, "CIN 0x0 e reservado e nao deve estar na tabela")
    _check(0x1 not in CIN_LEN, "CIN 0x1 e reservado e nao deve estar na tabela")

    # ---- 2. midi_to_usb: MIDI de fio -> pacote USB-MIDI de 4 bytes ----
    # Este e o sentido dos LEDs: o que o software de DJ manda pela porta virtual.
    _check(midi_to_usb(b"\x90\x40\x7F") == b"\x09\x90\x40\x7F",
           "Note On de 3 bytes -> CIN 0x9")
    _check(midi_to_usb(b"\xB0\x05\x40") == b"\x0B\xB0\x05\x40",
           "Control Change -> CIN 0xB")
    _check(midi_to_usb(b"\xC0\x07") == b"\x0C\xC0\x07\x00",
           "Program Change de 2 bytes -> CIN 0xC, com zero de enchimento")
    _check(midi_to_usb(b"\xEF\x00\x40") == b"\x0E\xEF\x00\x40",
           "Pitch Bend no canal 16 -> CIN 0xE")
    # Realtime (0xF8..0xFF): sempre um byte, sempre CIN 0xF. A referencia zera os
    # dois bytes de dados, e a igualdade com ela e proposital.
    _check(midi_to_usb(b"\xF8") == b"\x0F\xF8\x00\x00", "Clock 0xF8 -> CIN 0xF")
    _check(midi_to_usb(b"\xFE") == b"\x0F\xFE\x00\x00", "Active Sensing -> CIN 0xF")
    _check(midi_to_usb(b"\xFF\x01\x02") == b"\x0F\xFF\x00\x00",
           "Reset com bytes a mais: os dados sao ZERADOS, nao repassados")
    # SYSTEM COMMON (0xF0..0xF7) e RECUSADO: nao ha CIN unico para ele nesta
    # conversao, e o aparelho nao gera nada disso. Devolver b"" faz o chamador
    # ignorar sem injetar meia mensagem.
    for st in (0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7):
        _check(midi_to_usb(bytes([st, 0x01, 0x02])) == b"",
               f"system common 0x{st:02X} tem de ser recusado")
    _check(midi_to_usb(b"") == b"", "mensagem vazia -> b''")
    _check(midi_to_usb(b"\x40\x40\x40") == b"",
           "byte sem bit de status nao e mensagem MIDI")

    # ---- 3. injetar_pacote: o caminho UNICO de injecao, e os seus descartes ----
    grav = _BcdMidiGravador()
    bcdmidi_antes, porta_antes = bcdmidi, port_atual
    bcdmidi = grav
    port_atual = C.c_void_p(0x1234)     # sentinela: so precisa ser diferente de None
    try:
        # (a) caminho positivo, para os descartes significarem algo. CIN 9 = 3 bytes.
        _check(injetar_pacote(b"\x09\x90\x40\x7F"), "Note On tem de ser injetado")
        _check(grav.enviados[-1] == b"\x90\x40\x7F",
               f"injetou 3 bytes certos, veio {grav.enviados[-1:]!r}")
        # CIN C = 2 bytes: o quarto byte do pacote NAO entra.
        _check(injetar_pacote(b"\x0C\xC0\x07\xAA"), "Program Change tem de ser injetado")
        _check(grav.enviados[-1] == b"\xC0\x07",
               f"CIN 0xC injeta 2 bytes, veio {grav.enviados[-1:]!r}")
        # CIN F = 1 byte.
        _check(injetar_pacote(b"\x0F\xF8\xAA\xBB"), "Clock tem de ser injetado")
        _check(grav.enviados[-1] == b"\xF8",
               f"CIN 0xF injeta 1 byte, veio {grav.enviados[-1:]!r}")
        # Pacote MAIOR que 4 bytes: so os 4 primeiros contam (o chamador troceia,
        # mas a funcao nao pode se perder se receber mais).
        n = len(grav.enviados)
        _check(injetar_pacote(b"\x09\x90\x41\x7F\x00\x00\x00\x00"),
               "pacote maior que 4 bytes usa os 4 primeiros")
        _check(grav.enviados[-1] == b"\x90\x41\x7F", "e injeta so a mensagem deles")
        _check(len(grav.enviados) == n + 1, "uma injecao, nao duas")

        # ---- os DESCARTES ----
        n = len(grav.enviados)
        filtrados_antes = _cont["inj_filtrados"]
        # (1) ENCHIMENTO do aparelho. E o pacote mais comum de todos no EP 0x81, e o
        #     unico motivo pelo qual o driver pode repassar tudo cru: ele morre aqui.
        _check(not injetar_pacote(b"\x00\x00\x00\x00"), "enchimento 00 00 00 00 descartado")
        # (2) PRIMEIRO BYTE SEM BIT DE STATUS, com CIN valido. E o teste que apanha
        #     lixo/dessincronizacao: MIDI de verdade sempre comeca com o bit 7 ligado.
        _check(not injetar_pacote(b"\x09\x40\x40\x00"),
               "CIN valido mas primeiro byte sem bit de status: descartado")
        # (3) PACOTE CURTO. Nao ha 4 bytes, entao nao ha pacote.
        _check(not injetar_pacote(b"\x09\x90\x40"), "pacote de 3 bytes descartado")
        _check(not injetar_pacote(b""), "pacote vazio descartado")
        # (4) CIN DESCONHECIDO (reservado 0x0/0x1). *** ATENCAO: aqui NAO ha descarte
        #     por causa do CIN. *** O `.get(cin, 3)` cai no default de 3 bytes e a
        #     mensagem PASSA se carregar bit de status - e essa e a divergencia
        #     deliberada com o C++ removido, que descartava. Registrado como o
        #     comportamento REAL, e nao como se fosse um quarto descarte: o que
        #     realmente descarta enchimento com CIN 0 e o teste do bit de status,
        #     verificado em (1). Se alguem trocar o default por 0 ou por descarte, e
        #     esta linha que apanha a mudanca.
        _check(injetar_pacote(b"\x00\x90\x40\x7F"),
               "CIN reservado com status: default de 3 bytes, PASSA")
        _check(grav.enviados[-1] == b"\x90\x40\x7F", "e injeta os 3 bytes do default")
        _check(injetar_pacote(b"\x01\xB0\x05\x40"),
               "CIN 0x1 com status: mesmo default de 3 bytes")
        _check(len(grav.enviados) == n + 2,
               f"4 descartes e 2 passagens, veio {len(grav.enviados) - n} injecoes")
        # Os 4 descartes tem de aparecer no CONTADOR, e nao apenas no retorno. E ele
        # que vai ao log e que responde a contradicao publicada entre "o enchimento e
        # o pacote mais comum do EP 0x81" e "pacotes == injetados em 4.706 pacotes".
        _check(_cont["inj_filtrados"] == filtrados_antes + 4,
               f"os 4 descartes tem de contar em inj_filtrados, veio "
               f"{_cont['inj_filtrados'] - filtrados_antes}")

        # (5) FALHA DE INJECAO: a porta existe e BcdMidiSend devolve ZERO.
        #     Antes desta rodada o retorno era DESCARTADO e o pacote contava como
        #     injetado. Como `injetados` e coluna de criterio de portao, uma porta que
        #     parasse de aceitar ficava indistinguivel de uma sessao perfeita.
        erros_antes = _cont["inj_erros"]
        bcdmidi = _BcdMidiRecusando()
        _check(not injetar_pacote(b"\x09\x90\x40\x7F"),
               "porta que recusa a injecao tem de fazer injetar_pacote devolver False")
        _check(_cont["inj_erros"] == erros_antes + 1,
               f"e tem de contar em inj_erros, veio "
               f"{_cont['inj_erros'] - erros_antes}")
        bcdmidi = grav

        # (6) SEM PORTA nada e injetado, nem mensagem boa. E a guarda que protege a
        #     janela entre encerrar_porta() e o fim do processo.
        port_atual = None
        _check(not injetar_pacote(b"\x09\x90\x40\x7F"),
               "sem porta virtual, nada e injetado")
    finally:
        bcdmidi, port_atual = bcdmidi_antes, porta_antes

    # ---- 4. as constantes de contrato com o lado C++ ----
    # Nao provam o outro lado (isso e o arnes_canal.py), mas apanham um dedo errado.
    _check(RELAY_PIPE_NAME == r"\\.\pipe\BCD3000MidiRelay", "nome do canal")
    _check(RELAY_PACKET_BYTES == 4, "pacote de 4 bytes")
    _check(RELAY_READ_BYTES % RELAY_PACKET_BYTES == 0,
           "buffer de leitura multiplo do pacote")
    _check(RELAY_BUF_BYTES % RELAY_PACKET_BYTES == 0,
           "buffer do pipe multiplo do pacote")
    _check(EVENT_NAME == "Global\\" + EVENT_NAME_LOCAL,
           "o nome global e o local com o prefixo de escopo")

    # ---- 5. o diagnostico de falha da criacao da porta ----
    # A biblioteca mudou (Tarefa 4): as categorias agora sao a NOSSA propria
    # enumeracao (native/bcdmidi/bcdmidi.h), nao mais codigos do GetLastError de
    # uma DLL de terceiro, e toda falha chega acompanhada de um HRESULT (0
    # quando nao ha um).
    #
    # Defeito medido no hardware em 2026-08-01, com a biblioteca anterior: a
    # UNICA mensagem que existia afirmava conflito de nome, o erro real nao
    # tinha nada a ver com isso, e o NUMERO nao aparecia em lugar nenhum. Custou
    # uma hora procurando um programa aberto que nao existia.
    #
    # O numero e conferido como "erro {n}" e nao como "{n}" solto, e a diferenca
    # importa: '3' e '0' aparecem em "BCD3000", entao um `str(erro) in msg` para
    # o erro 3 seria VERDADEIRO mesmo com a mensagem sem numero nenhum - uma
    # verificacao incapaz de falhar. O lado esquerdo de cada comparacao e
    # literal deste teste, o direito sai da funcao.

    # ---- 5.0 PINAGEM de NOME_DA_CATEGORIA contra BcdMidiErrorText ----
    # Revisao da Tarefa 4, achado 1: NOME_DA_CATEGORIA e um dicionario escrito
    # a mao, e bcdmidi.h:135 ja tem BcdMidiErrorText fazendo exatamente esse
    # mapeamento (categoria -> texto) dentro da DLL. Duas fontes independentes
    # descrevendo o mesmo enum de seis valores e a MESMA forma da familia de
    # defeitos "instance 14" que a Decisao D1 existe para evitar: uma sentenca
    # corrigida num lugar e esquecida no outro.
    #
    # A FONTE DA VERDADE sobre QUAIS categorias sao conhecidas e a propria DLL,
    # nao este dicionario - por isso o dicionario NAO foi apagado (ele so da um
    # ROTULO/simbolo em portugues-de-programador para cada numero, o que
    # BcdMidiErrorText nao fornece: ela devolve prosa em ingles, nunca o nome
    # do simbolo C), mas ele e VERIFICADO contra a DLL sempre que ela estiver
    # carregada. Se um dia bcdmidi.h ganhar uma setima categoria sem que
    # NOME_DA_CATEGORIA seja atualizado (ou o inverso: o dicionario ganhar uma
    # entrada que a DLL nao conhece), estas verificacoes acusam a divergencia
    # em vez de deixar as duas fontes derivarem em silencio.
    #
    # So roda com bcdmidi carregado de verdade - e nao um requisito novo: e a
    # MESMA regra que ja vale para o resto do arquivo ("as funcoes que o
    # autoteste exercita nao precisam da DLL"). Sem a DLL, este bloco e pulado
    # e o restante do bloco 5 (que so chama _diagnostico_falha_da_porta, e
    # NUNCA bcdmidi.BcdMidiErrorText) continua rodando normalmente.
    if bcdmidi is not None:
        textos_conhecidos = {}
        for cat in sorted(NOME_DA_CATEGORIA):
            texto = bcdmidi.BcdMidiErrorText(cat).decode("ascii", "replace")
            textos_conhecidos[cat] = texto
            # Toda categoria que O DICIONARIO diz conhecer tem de vir da DLL
            # com texto REAL, nao com a frase generica de fallback. Se
            # NOME_DA_CATEGORIA um dia tiver uma entrada que a DLL nao
            # reconhece (erro de digitacao no numero, por exemplo), a DLL
            # devolve "unknown error" e este check acusa a divergencia.
            _check("unknown error" not in texto,
                   f"BcdMidiErrorText({cat}) devolveu texto de fallback "
                   f"'unknown error', mas NOME_DA_CATEGORIA diz que {cat} e "
                   f"{NOME_DA_CATEGORIA[cat]!r} - dicionario e DLL "
                   f"divergiram: {texto!r}")
        # Nao-vazio e DISTINTO para cada categoria conhecida - um copy-paste
        # dentro do switch de bcdmidi.cpp (duas categorias com o mesmo texto)
        # nao quebraria nenhum outro teste deste arquivo, porque nenhum outro
        # teste olha para o texto da DLL isoladamente.
        _check(all(textos_conhecidos.values()),
               "BcdMidiErrorText nao pode devolver texto vazio para nenhuma "
               "categoria conhecida")
        _check(len(set(textos_conhecidos.values())) == len(textos_conhecidos),
               f"BcdMidiErrorText tem de devolver texto DISTINTO para cada "
               f"categoria conhecida, veio {textos_conhecidos!r}")
        # E o INVERSO: um numero que o dicionario NAO conhece (uma a mais que
        # a maior chave) tem de vir com o texto de fallback da DLL. Se a DLL
        # um dia ganhar uma setima categoria REAL neste numero exato sem que
        # o dicionario seja atualizado, e aqui que a divergencia aparece.
        _proximo_desconhecido = max(NOME_DA_CATEGORIA) + 1
        texto_desconhecido = bcdmidi.BcdMidiErrorText(_proximo_desconhecido).decode(
            "ascii", "replace")
        _check("unknown error" in texto_desconhecido,
               f"categoria {_proximo_desconhecido} (uma a mais que o "
               f"dicionario conhece, {sorted(NOME_DA_CATEGORIA)}) tem de vir "
               f"como 'unknown error' da DLL - se nao vier, a DLL ganhou uma "
               f"categoria que o dicionario nao tem: {texto_desconhecido!r}")

    for codigo in (1, 2, 3, 4, 5, 6, 0, 7, 87, 1306):
        msg = _diagnostico_falha_da_porta(codigo, 0)
        _check(f"erro {codigo}" in msg,
               f"o codigo {codigo} tem de sair na mensagem como 'erro {codigo}': {msg!r}")

    # (a) 1, kBcdMidiServiceMissing: causa NOMEADA, o servico MIDI do Windows -
    #     e NAO a biblioteca substituida. Esta e a verificacao que a Tarefa 4
    #     pede por nome.
    m1 = _diagnostico_falha_da_porta(1, 0)
    _check("kBcdMidiServiceMissing" in m1, f"1 tem de sair nomeado: {m1!r}")
    _check("MIDI do Windows" in m1, f"1 tem de nomear o servico MIDI do Windows: {m1!r}")
    _check("loopMIDI" not in m1, f"1 NAO pode mencionar loopMIDI: {m1!r}")
    _check("Tobias" not in m1, f"1 NAO pode mencionar Tobias: {m1!r}")

    # (b) 6, kBcdMidiException: a causa e o HRESULT, e SO ele - bcdmidi.h diz
    #     que e tudo o que a DLL sabe sobre uma excecao do WinRT. O HRESULT tem
    #     de aparecer por extenso (8 digitos hex), nao so a categoria - a outra
    #     verificacao que a Tarefa 4 pede por nome.
    m6 = _diagnostico_falha_da_porta(6, 0x80040154)   # REGDB_E_CLASSNOTREG
    _check("kBcdMidiException" in m6, f"6 tem de sair nomeado: {m6!r}")
    _check("0x80040154" in m6,
           f"6 tem de imprimir o HRESULT completo, nao so a categoria: {m6!r}")
    # E o HRESULT sai mesmo quando e zero (bcdmidi.h: hrOut e sempre escrito,
    # inclusive com 0 quando a falha nao tem um) - nao vira ausencia silenciosa.
    m6b = _diagnostico_falha_da_porta(6, 0)
    _check("0x00000000" in m6b,
           f"6 com HRESULT zero ainda tem de imprimir o HRESULT por extenso: {m6b!r}")

    # _fmt_hr tem de recuperar os 32 bits sem sinal de um c_long NEGATIVO - o
    # jeito que ctypes realmente entrega um HRESULT de falha (bit 31 ligado).
    # Construido com C.c_long() e nao a mao: deixar o proprio ctypes fazer a
    # conversao de complemento de dois evita erro de aritmetica neste teste.
    _hr_timeout = C.c_long(0x800705B4).value   # HRESULT_FROM_WIN32(ERROR_TIMEOUT)
    _check(_hr_timeout < 0, f"pre-condicao do teste: o c_long tem de vir negativo, veio {_hr_timeout}")
    _check(_fmt_hr(_hr_timeout) == "0x800705B4",
           f"_fmt_hr tem de recuperar 0x800705B4 de um c_long negativo, veio "
           f"{_fmt_hr(_hr_timeout)!r} a partir de {_hr_timeout}")

    # (c) NENHUM codigo, em NENHUMA categoria, pode mencionar a biblioteca
    # substituida - o proprio criterio de "Produces" da Tarefa 4. Cobre as seis
    # categorias do contrato mais tres valores fora dele.
    for codigo in (0, 1, 2, 3, 4, 5, 6, 7, 87, 1306):
        msg = _diagnostico_falha_da_porta(codigo, 0)
        _check("loopMIDI" not in msg,
               f"{codigo} NAO pode mencionar loopMIDI - biblioteca substituida: {msg!r}")
        _check("Tobias" not in msg, f"{codigo} NAO pode mencionar Tobias: {msg!r}")
        _check("teVirtualMIDI" not in msg,
               f"{codigo} NAO pode mencionar teVirtualMIDI: {msg!r}")

    # (d) as outras tres causas nomeadas (2, 3, 4): redacao PROPRIA cada uma, e
    #     o nome da categoria sai na mensagem.
    m2 = _diagnostico_falha_da_porta(2, 0)
    _check("kBcdMidiTransportMissing" in m2, f"2 tem de sair nomeado: {m2!r}")
    _check("transporte" in m2, f"2 tem de falar do transporte: {m2!r}")
    m3 = _diagnostico_falha_da_porta(3, 0)
    _check("kBcdMidiCreateFailed" in m3, f"3 tem de sair nomeado: {m3!r}")
    _check("1047" in m3, f"3 tem de citar o defeito conhecido microsoft/MIDI #1047: {m3!r}")
    m4 = _diagnostico_falha_da_porta(4, 0)
    _check("kBcdMidiOpenFailed" in m4, f"4 tem de sair nomeado: {m4!r}")
    m5 = _diagnostico_falha_da_porta(5, 0)
    _check("kBcdMidiBadArgument" in m5, f"5 tem de sair nomeado: {m5!r}")
    _check("defeito deste programa" in m5,
           f"5 e bug deste programa, nao do ambiente do usuario: {m5!r}")

    # (e) qualquer categoria FORA do contrato (0 e "sem erro", nunca deveria
    #     chegar aqui de verdade; 7+ nao existe no enum): NAO INVENTAR CAUSA.
    #     Uma mensagem que chuta e o defeito que este bloco existe para
    #     consertar, entao chutar num codigo novo seria reintroduzi-lo por
    #     outra porta.
    for codigo in (0, 7, 87, 1306):
        msg = _diagnostico_falha_da_porta(codigo, 0)
        _check("DESCONHECIDO" in msg, f"{codigo} tem de se declarar desconhecido: {msg!r}")

    # (f) o HRESULT sai SEMPRE, em toda categoria conhecida, mesmo quando e 0.
    for codigo in (1, 2, 3, 4, 5, 6):
        msg = _diagnostico_falha_da_porta(codigo, 0)
        _check("HRESULT" in msg and "0x00000000" in msg,
               f"{codigo} tem de imprimir o HRESULT mesmo quando e 0: {msg!r}")

    # ---- 6. duas propriedades do FONTE, que nenhum teste dinamico alcanca ----
    # As duas abaixo so se verificam lendo o arquivo. A primeira porque o defeito que
    # ela apanha depende de uma DLL de verdade responder; a segunda porque e sobre
    # existir SITIO DE CHAMADA, e um teste que chama a funcao ele mesmo nao prova que
    # o programa a chama - foi assim que uma tarefa desta pasta embarcou nove
    # verificacoes com zero sitios de chamada lendo verde.
    try:
        fonte = open(os.path.abspath(__file__), "r", encoding="utf-8").read()
    except Exception as e:
        fonte = ""
        _check(False, f"nao consegui ler o proprio fonte para as duas verificacoes "
                      f"estaticas ({e}); rode como `python bridge_service.py --autoteste`")
    if fonte:
        linhas_do_fonte = fonte.splitlines()
        # As agulhas sao MONTADAS por concatenacao de proposito: este arquivo esta
        # lendo a si mesmo, e uma agulha escrita inteira encontraria ESTA linha alem
        # da linha de verdade, e a contagem daria 2.
        agulha_dll = "bcdmidi = C.Win" + "DLL("
        linhas_dll = [ln for ln in linhas_do_fonte if agulha_dll in ln]
        _check(len(linhas_dll) == 1,
               f"1 linha carrega o BcdMidi.dll, achei {len(linhas_dll)}")
        # SEM use_last_error=True, um C.get_last_error() de qualquer chamada
        # ctypes deste modulo devolveria o erro de OUTRA WinDLL qualquer.
        # Medido em 2026-08-01 (biblioteca anterior) com duas WinDLL de
        # kernel32 lado a lado: a de fora da opcao deixou get_last_error() em 2
        # enquanto o GetLastError do sistema dizia 3. O BcdMidi.dll de hoje
        # reporta os PROPRIOS erros por errOut/hrOut e nao por GetLastError -
        # ver criar_porta() -, mas a opcao continua obrigatoria nesta chamada:
        # e a mesma regra que protege as outras WinDLL deste processo, e
        # remove-la deixaria QUALQUER get_last_error() chamado depois desta
        # linha, nesta thread, tao pouco confiavel quanto era o da biblioteca
        # antiga.
        _check(bool(linhas_dll) and "use_last_error=True" in linhas_dll[0],
               f"o BcdMidi.dll TEM de ser carregado com use_last_error=True: "
               f"{linhas_dll!r}")

        # O SITIO DE CHAMADA. Sem esta verificacao, todo o bloco 5 poderia estar
        # testando uma funcao que o programa nunca usa. O token nao aparece na
        # documentacao de abrir_porta_uma_vez de proposito: apareceria aqui dentro e
        # a verificacao passaria sem chamada nenhuma.
        # Agulha montada pelo mesmo motivo da de cima, e aqui ela tem um segundo:
        # escrita inteira, ela seria uma SEGUNDA "chamada" de abrir_porta_uma_vez aos
        # olhos do arnes_invariante, que conta sitios de chamada por texto e exige
        # exatamente um. Escrita assim, nao existe.
        agulha_def = "def abrir_porta_uma_vez" + "("
        i_def = [i for i, ln in enumerate(linhas_do_fonte) if ln.startswith(agulha_def)]
        _check(len(i_def) == 1, f"1 def de abrir_porta_uma_vez, achei {len(i_def)}")
        if i_def:
            fim = min([i for i, ln in enumerate(linhas_do_fonte)
                       if ln.startswith("def ") and i > i_def[0]] + [len(linhas_do_fonte)])
            corpo = "\n".join(linhas_do_fonte[i_def[0]:fim])
            _check("_diagnostico_falha_da_porta(" in corpo,
                   "abrir_porta_uma_vez tem de MANDAR a falha para o diagnostico; sem "
                   "este sitio de chamada o bloco 5 testa codigo que ninguem executa")
            _check("em uso por outro programa" not in corpo,
                   "abrir_porta_uma_vez nao pode ter mensagem propria de falha: era "
                   "dela a frase que acusava conflito de nome sem olhar o erro")

    print(f"== {_ok + _falhas} verificacoes, {_falhas} falhas ==")
    return 1 if _falhas else 0

if __name__ == "__main__":
    if "--autoteste" in sys.argv:
        sys.exit(autoteste())
    sys.exit(run())
