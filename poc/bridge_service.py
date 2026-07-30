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
# que o callback NUNCA bloqueie: ele e chamado por um thread da teVirtualMIDI, e
# prende-lo prenderia o software de DJ.
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

# ---- teVirtualMIDI ----
# A falha de carga e TRATADA, e nao propagada, por duas razoes independentes:
#  (1) o executavel e empacotado com --noconsole. Uma excecao aqui mataria o processo
#      ANTES de qualquer linha de log, e o usuario nao teria como descobrir por que os
#      controles nao funcionam - a unica pista seria o processo nao existir. Com o
#      tratamento, run() escreve o motivo no bridge.log e devolve codigo de erro;
#  (2) e o que deixa o `--autoteste` rodar numa maquina SEM a biblioteca. As funcoes
#      que o autoteste exercita (CIN_LEN, midi_to_usb, injetar_pacote) nao precisam
#      dela: o injetor troca `tevm` por um gravador.
# CALLBACK fica FORA do try de proposito: C.CFUNCTYPE e do proprio ctypes e nao
# depende de DLL nenhuma, e _cb precisa existir sempre.
_tevm_erro = None
try:
    tevm = C.WinDLL(r"C:\Windows\System32\teVirtualMIDI64.dll")
    tevm.virtualMIDICreatePortEx2.restype = C.c_void_p
    tevm.virtualMIDICreatePortEx2.argtypes = [W.LPCWSTR, C.c_void_p, C.c_void_p, W.DWORD, W.DWORD]
    tevm.virtualMIDIClosePort.argtypes = [C.c_void_p]
    tevm.virtualMIDISendData.argtypes = [C.c_void_p, C.POINTER(C.c_ubyte), W.DWORD]
    tevm.virtualMIDISendData.restype = W.BOOL
except OSError as _e:
    tevm = None
    _tevm_erro = str(_e)
CALLBACK = C.CFUNCTYPE(None, C.c_void_p, C.POINTER(C.c_ubyte), W.DWORD, C.c_void_p)
FLAGS_BOTH_PARSE = 12 | 1

if3 = None  # handle da IF3 do aparelho (None quando desconectado)
dev = None  # pacote opaco de handles para close_dev (None quando desconectado)

# A porta virtual. Criada UMA VEZ em abrir_porta_uma_vez() e zerada so em
# encerrar_porta(), no fim do processo.
port_atual = None

# Serializa a escrita de LED contra o fechamento dos handles do aparelho. O
# callback da teVirtualMIDI roda em outro thread; sem esta trava, o fechamento
# poderia acontecer entre o `h = if3` do callback e o WritePipe dele, e o WritePipe
# usaria um valor de handle que o Windows ja pode ter reciclado.
_led_lock = threading.Lock()

# Serializa a INJECAO na porta virtual e protege a troca de port_atual. Duas
# razoes:
#  (1) sao duas fontes possiveis de injecao - o laco de leitura do aparelho e o
#      thread do canal - e ordenacao de mensagens MIDI por porta e desejavel;
#  (2) o fechamento da porta no fim do processo nao pode acontecer no meio de um
#      virtualMIDISendData de outro thread.
# NUNCA chamar virtualMIDIClosePort segurando esta trava junto de _led_lock: o
# close espera pelo callback de LED, e o callback espera por _led_lock.
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
    Antes devolvia True depois de CHAMAR virtualMIDISendData, descartando o retorno
    dela, e com isso o contador de injetados contava TENTATIVA e nao sucesso. Isso
    importa porque `injetados` e coluna de criterio de portao: uma porta que parasse
    de aceitar ficaria indistinguivel de uma sessao perfeita.
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
        enviado = bool(tevm.virtualMIDISendData(p, arr, len(m)))
    # O log fica FORA da trava: injetar_pacote roda no caminho dos controles, e
    # nenhuma escrita em arquivo precisa segurar a trava da porta.
    if not enviado:
        _cont["inj_erros"] += 1
        if _cont["inj_erros"] <= 5 or _cont["inj_erros"] % 500 == 0:
            log(f"injecao na porta virtual falhou #{_cont['inj_erros']} "
                f"(virtualMIDISendData devolveu falso; a porta ainda existe?)")
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

    O callback da teVirtualMIDI roda num thread da biblioteca; prende-lo prenderia
    o software de DJ. Fila cheia significa que o driver parou de drenar o canal por
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
    # propagar excecao para dentro da teVirtualMIDI.
    try:
        _fila_led.get_nowait()
        _fila_led.put_nowait(pkt)
    except (queue.Empty, queue.Full):
        pass

def rx_callback(port, data_ptr, length, inst):
    """LEDs vindos do software de DJ -> aparelho, direto ou pelo canal."""
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

_cb = CALLBACK(rx_callback)

def criar_porta():
    """Cria a porta virtual. None se o nome ja estiver em uso."""
    if tevm is None:
        return None
    p = tevm.virtualMIDICreatePortEx2(PORT_NAME, _cb, None, 65535, FLAGS_BOTH_PARSE)
    return p if p else None

def abrir_porta_uma_vez():
    """Cria a porta virtual e a publica. Insiste ate conseguir.

    *** ESTA E A UNICA FUNCAO DO PROGRAMA QUE CRIA A PORTA, e ela e chamada de UM
    SO lugar, antes do laco principal. A linha 'porta virtual ... criada' aparece
    portanto EXATAMENTE UMA VEZ por execucao, e isso e verificavel no log - e o
    criterio objetivo do portao de hardware desta arquitetura. ***

    Insistir em vez de desistir importa numa situacao concreta: um BcdAsio.dll
    ANTIGO carregado num software de DJ ja aberto ainda cria a porta com este nome.
    Nesse caso esperamos ele soltar, em vez de morrer e deixar o usuario sem
    controles para sempre.
    """
    global port_atual
    falhas = 0
    while True:
        p = criar_porta()
        if p:
            with _port_lock:
                port_atual = p
            log(f"porta virtual '{PORT_NAME}' criada")
            return
        falhas += 1
        if falhas == 1:
            log(f"porta '{PORT_NAME}' em uso por outro programa (um BcdAsio.dll "
                f"antigo num software de DJ aberto?); continuo tentando a cada 1 s")
        time.sleep(1)

def encerrar_porta():
    """Fecha a porta virtual. SO no encerramento do processo.

    Copiar, ZERAR e so entao fechar. Assim um injetor concorrente ou ja terminou
    (a trava garante) ou ve None e desiste. E virtualMIDIClosePort e chamada FORA
    das travas: ela espera pelo callback de LED em voo, e o callback espera por
    _led_lock - chamar de dentro seria travamento mutuo.
    """
    global port_atual
    with _port_lock:
        p = port_atual
        port_atual = None
    if p:
        tevm.virtualMIDIClosePort(p)

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
    if tevm is None:
        # Sem a biblioteca nao ha porta virtual, e sem porta virtual este programa
        # nao tem nada a fazer: o canal com o driver so existe para levar controles
        # ATE a porta. Sair com o motivo escrito e melhor que insistir num laco que
        # nunca vai conseguir nada, e MUITO melhor que morrer sem log.
        log(f"FATAL: teVirtualMIDI64.dll nao carregou ({_tevm_erro}). Instale o "
            f"teVirtualMIDI (o mesmo pacote do loopMIDI serve) e inicie de novo - "
            f"sem ela nao existe porta MIDI virtual e os controles nao chegam ao "
            f"software de DJ")
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
# Roda SEM aparelho, SEM teVirtualMIDI e SEM canal, e nao escreve no bridge.log de
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

class _TevmGravador:
    """Substituto de `tevm` que GRAVA em vez de injetar.

    Sem ele nao se distingue "o filtro descartou" de "nao havia porta": as duas
    coisas fazem injetar_pacote() devolver False. Assinatura igual a da unica funcao
    que injetar_pacote usa.
    """
    def __init__(self):
        self.enviados = []
    def virtualMIDISendData(self, port, arr, tam):
        self.enviados.append(bytes(bytearray(arr[:tam])))
        return True

class _TevmRecusando:
    """Substituto de `tevm` cuja porta RECUSA tudo.

    Existe por um motivo especifico: a virtualMIDISendData de verdade devolve BOOL, e
    esse retorno era descartado. Sem este substituto, o unico caminho de falha de
    injecao que existe no programa nao seria exercitado por nada - e caminho de
    excecao nao exercitado e onde esta sessao ja achou cinco defeitos.
    """
    def virtualMIDISendData(self, port, arr, tam):
        return False

def autoteste():
    global tevm, port_atual
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
    grav = _TevmGravador()
    tevm_antes, porta_antes = tevm, port_atual
    tevm = grav
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

        # (5) FALHA DE INJECAO: a porta existe e virtualMIDISendData devolve FALSO.
        #     Antes desta rodada o retorno era DESCARTADO e o pacote contava como
        #     injetado. Como `injetados` e coluna de criterio de portao, uma porta que
        #     parasse de aceitar ficava indistinguivel de uma sessao perfeita.
        erros_antes = _cont["inj_erros"]
        tevm = _TevmRecusando()
        _check(not injetar_pacote(b"\x09\x90\x40\x7F"),
               "porta que recusa a injecao tem de fazer injetar_pacote devolver False")
        _check(_cont["inj_erros"] == erros_antes + 1,
               f"e tem de contar em inj_erros, veio "
               f"{_cont['inj_erros'] - erros_antes}")
        tevm = grav

        # (6) SEM PORTA nada e injetado, nem mensagem boa. E a guarda que protege a
        #     janela entre encerrar_porta() e o fim do processo.
        port_atual = None
        _check(not injetar_pacote(b"\x09\x90\x40\x7F"),
               "sem porta virtual, nada e injetado")
    finally:
        tevm, port_atual = tevm_antes, porta_antes

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

    print(f"== {_ok + _falhas} verificacoes, {_falhas} falhas ==")
    return 1 if _falhas else 0

if __name__ == "__main__":
    if "--autoteste" in sys.argv:
        sys.exit(autoteste())
    sys.exit(run())
