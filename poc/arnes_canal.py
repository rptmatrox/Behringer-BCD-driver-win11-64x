"""
ARNES DO CONTRATO ENTRE AS DUAS LINGUAGENS - o canal local de MIDI.

    python arnes_canal.py

O que ele prova, e que NENHUM teste de unidade prova: que o servidor em Python
entende os bytes que o cliente em C++ manda, e vice-versa. Os testes do tests.exe
exercitam o RelayLink contra um servidor FALSO escrito em C++; o autoteste do
bridge_service exercita o filtro em Python contra pacotes escritos em Python. Nos
dois casos o contrato entre as linguagens fica sem testemunha.

Aqui o servidor e o `servidor_do_canal()` DE VERDADE, importado do bridge_service, e o
cliente e o `RelayLink` DE VERDADE, rodando dentro do `tests.exe` no modo arnes
(`tests.exe rele-cliente <pacotes> <ms>`). O unico substituto e o BcdMidi.dll, que
e trocado por um GRAVADOR - sem isso nao ha como afirmar O QUE foi injetado, so que
algo foi.

Roda SEM aparelho e SEM a porta virtual de verdade. Precisa do `tests.exe` compilado.

*** NAO RODAR COM O BCD3000Bridge.exe DESTA TAREFA NO AR: *** ele e o dono do nome
`\\.\pipe\BCD3000MidiRelay`, e este arnes precisa criar esse mesmo nome. Ha guarda:
se o nome existir, o arnes sai com codigo 3 e diz que NAO provou nada - um "0 falhas"
que nao rodou seria pior que uma falha.

Saida: `ARNES_CANAL: N verificacoes, M falhas`, codigo 0 se M == 0.
"""
import os, re, subprocess, sys, tempfile, threading, time
import ctypes as C
from ctypes import wintypes as W

AQUI = os.path.dirname(os.path.abspath(__file__))
TESTS_EXE = os.path.join(AQUI, "..", "native", "bcdasio", "tests.exe")
MIDIBRIDGE_H = os.path.join(AQUI, "..", "native", "bcdasio", "midibridge.h")

# O DESVIO DO LOG VEM ANTES DO IMPORT, e nao e detalhe: o bridge_service configura o
# logging no nivel do modulo, a partir de LOCALAPPDATA. Sem isto o arnes escreveria no
# bridge.log do BCD3000Bridge.exe que roda nesta maquina - o mesmo arquivo que e a
# EVIDENCIA do portao de hardware, contada por linha.
_tmp = os.path.join(tempfile.gettempdir(), "BCD3000Bridge-arnes")
os.makedirs(_tmp, exist_ok=True)
os.environ["LOCALAPPDATA"] = _tmp

sys.path.insert(0, AQUI)
import bridge_service as bs        # noqa: E402  (o desvio acima tem de vir primeiro)

_ok = 0
_falhas = 0

def check(cond, o_que):
    global _ok, _falhas
    if cond:
        _ok += 1
    else:
        _falhas += 1
        print(f"  FALHA: {o_que}")

class BcdMidiGravador:
    """Grava o que seria injetado na porta virtual, em vez de injetar.

    Este arnes nunca chama criar_porta()/abrir_porta_uma_vez() - so
    injetar_pacote() (que chama bcdmidi.BcdMidiSend diretamente) - entao,
    diferente do arnes_invariante, nao ha necessidade de simular
    BcdMidiCreatePort aqui: a classe antiga (TevmGravador) tinha um metodo e
    um contador de criacoes que main() nunca lia, codigo morto que so
    confundiria sobre o que este arnes realmente exercita.
    """
    def __init__(self):
        self.injetados = []
        self.fechadas = 0
    def BcdMidiClosePort(self, p):
        self.fechadas += 1
    def BcdMidiSend(self, port, arr, tam):
        self.injetados.append(bytes(bytearray(arr[:tam])))
        return 1     # BcdMidiSend devolve int (nao-zero em sucesso), nao BOOL

def nome_do_canal_existe():
    """True se ja ha um servidor com o nome real do canal.

    WaitNamedPipeW nao conecta nem consome instancia: so pergunta se existe. Mesma
    guarda do test_relay_link no lado C++, pelo mesmo motivo.
    """
    k32 = C.WinDLL("kernel32", use_last_error=True)
    k32.WaitNamedPipeW.argtypes = [W.LPCWSTR, W.DWORD]
    k32.WaitNamedPipeW.restype = W.BOOL
    if k32.WaitNamedPipeW(bs.RELAY_PIPE_NAME, 1):
        return True
    return C.get_last_error() != 2      # 2 = ERROR_FILE_NOT_FOUND

def constantes_do_header():
    """Extrai do midibridge.h os valores que sao contrato, sem compilar nada."""
    txt = open(MIDIBRIDGE_H, "r", encoding="utf-8", errors="replace").read()
    def inteiro(nome):
        m = re.search(r"\b" + nome + r"\s*=\s*(\d+)\s*;", txt)
        return int(m.group(1)) if m else None
    m = re.search(r'kRelayPipeName\s*=\s*L"([^"]+)"', txt)
    nome = m.group(1).replace("\\\\", "\\") if m else None
    return {"nome": nome,
            "pacote": inteiro("kRelayPacketBytes"),
            "buf_leitura": inteiro("kRelayReadBufBytes")}

def entregar_led(msg):
    """Chama o callback REAL da porta virtual, como o BcdMidi.dll o chamaria."""
    arr = (C.c_ubyte * len(msg)).from_buffer_copy(msg)
    bs.rx_callback(None, arr, len(msg))    # TRES argumentos - BcdMidiRecvCb, nao mais quatro

def main():
    print("== arnes do canal (C++ <-> Python) ==")

    if not os.path.exists(TESTS_EXE):
        print(f"ARNES_PULADO: {TESTS_EXE} nao existe. Rode build.bat tests primeiro.")
        return 3
    if nome_do_canal_existe():
        print(f"ARNES_PULADO: ja existe um servidor em '{bs.RELAY_PIPE_NAME}' "
              f"(BCD3000Bridge.exe desta tarefa rodando?). NADA foi provado - pare o "
              f"bridge e rode de novo.")
        return 3

    # ---- 1. as constantes de contrato, lado a lado ----
    # Mudar uma delas em UM dos lados nao daria erro de compilacao em lugar nenhum -
    # so faria os controles ficarem mudos. E aqui que se quebra.
    h = constantes_do_header()
    check(h["nome"] == bs.RELAY_PIPE_NAME,
          f"nome do canal: C++ {h['nome']!r} vs Python {bs.RELAY_PIPE_NAME!r}")
    check(h["pacote"] == bs.RELAY_PACKET_BYTES,
          f"tamanho do pacote: C++ {h['pacote']} vs Python {bs.RELAY_PACKET_BYTES}")
    check(h["pacote"] == 4, "o pacote e o da USB-MIDI 1.0: 4 bytes")
    # O buffer de leitura de cada lado tem de caber a MAIOR mensagem que o outro
    # manda. O driver manda uma transferencia USB inteira (le o EP 0x81 com 64
    # bytes); o bridge manda um pacote de 4.
    check(bs.RELAY_READ_BYTES >= 64,
          f"o bridge le {bs.RELAY_READ_BYTES}, e o driver pode mandar 64 de uma vez")
    check(h["buf_leitura"] >= h["pacote"],
          "o driver le pelo menos um pacote de cada vez")
    check(h["buf_leitura"] % h["pacote"] == 0,
          "buffer de leitura do driver multiplo do pacote")
    check(bs.RELAY_READ_BYTES % bs.RELAY_PACKET_BYTES == 0,
          "buffer de leitura do bridge multiplo do pacote")
    check(bs.RELAY_BUF_BYTES % bs.RELAY_PACKET_BYTES == 0,
          "buffer do pipe multiplo do pacote")

    # ---- 2. o filtro do caminho unico, direto ----
    # GUARDA antes da substituicao: se bridge_service.py renomear 'bcdmidi' de
    # novo sem que este arnes acompanhe, `bs.bcdmidi = grav` abaixo viraria um
    # NO-OP SILENCIOSO - criaria um atributo novo no modulo em vez de
    # substituir o que injetar_pacote() de fato chama - e o `port_atual`
    # FABRICADO duas linhas abaixo seria entregue a DLL REAL. Foi exatamente
    # isso que aconteceu quando a Tarefa 4 trocou `tevm` por `bcdmidi`: a
    # linha antiga `bs.tevm = grav` continuava rodando sem erro nenhum, so que
    # sem efeito, e bs.injetar_pacote() teria chamado bcdmidi.BcdMidiSend com
    # um ponteiro de porta que nao existe.
    assert hasattr(bs, "bcdmidi"), (
        "bridge_service.py nao tem mais o atributo 'bcdmidi' - este arnes "
        "esta desatualizado e a substituicao seria um no-op silencioso")
    grav = BcdMidiGravador()
    bs.bcdmidi = grav
    bs.port_atual = C.c_void_p(0xBEEF)
    n0 = len(grav.injetados)
    check(bs.injetar_pacote(b"\x09\x90\x40\x7F") and
          grav.injetados[-1] == b"\x90\x40\x7F", "CIN 0x9 -> 3 bytes")
    check(bs.injetar_pacote(b"\x0C\xC0\x07\xAA") and
          grav.injetados[-1] == b"\xC0\x07", "CIN 0xC -> 2 bytes")
    check(bs.injetar_pacote(b"\x0F\xF8\xAA\xBB") and
          grav.injetados[-1] == b"\xF8", "CIN 0xF -> 1 byte")
    check(not bs.injetar_pacote(b"\x00\x00\x00\x00"), "enchimento descartado")
    check(not bs.injetar_pacote(b"\x09\x40\x40\x00"), "byte sem status descartado")
    check(not bs.injetar_pacote(b"\x09\x90\x40"), "pacote curto descartado")
    check(len(grav.injetados) == n0 + 3, "3 injecoes e 3 descartes")

    # ---- 3. o servidor DE VERDADE contra o cliente DE VERDADE ----
    threading.Thread(target=bs.servidor_do_canal, name="canal-arnes",
                     daemon=True).start()
    time.sleep(0.3)      # deixa o ConnectNamedPipe ficar de pe

    base = dict(bs._cont)
    marca = len(grav.injetados)

    # Os LEDs so podem ser postos DEPOIS de o driver conectar: _atender_um_driver()
    # esvazia a fila na conexao, de proposito (estado de VU de 30 s atras nao serve).
    # Sincronizar por CONTADOR e nao pela saida do filho e deliberado: o stdout do
    # filho vai para um pipe, entao o CRT dele usa buffer cheio e as linhas podem so
    # aparecer no fim - esperar por elas para agir seria uma armadilha.
    leds = [b"\x90\x20\x7F", b"\xB0\x0A\x40", b"\xF8"]
    esperado_no_driver = [b"\x09\x90\x20\x7F", b"\x0B\xB0\x0A\x40", b"\x0F\xF8\x00\x00"]

    def por_os_leds():
        limite = time.monotonic() + 5
        while time.monotonic() < limite:
            if bs._cont["canal_pkts"] - base["canal_pkts"] >= 10:
                break
            time.sleep(0.01)
        for m in leds:
            entregar_led(m)

    t = threading.Thread(target=por_os_leds, daemon=True)
    t.start()
    saida = subprocess.run([TESTS_EXE, "rele-cliente", "5", "2000"],
                           capture_output=True, text=True, timeout=60).stdout
    t.join(timeout=10)

    pkts = bs._cont["canal_pkts"] - base["canal_pkts"]
    injs = bs._cont["canal_inj"] - base["canal_inj"]
    # Cada mensagem do arnes do C++ leva DOIS pacotes: um util e o enchimento
    # 00 00 00 00 do aparelho. O driver repassa os dois crus, de proposito - o filtro
    # mora aqui, num caminho unico. Logo 10 pacotes e 5 injecoes.
    check(pkts == 10, f"10 pacotes atravessaram o canal, veio {pkts}")
    check(injs == 5, f"5 injecoes (o enchimento morre no filtro), veio {injs}")
    recebidos = grav.injetados[marca:marca + 5]
    esperado = [bytes([0xB0, 0x05, i + 1]) for i in range(5)]
    check(recebidos == esperado,
          f"conteudo injetado: esperado {esperado!r}, veio {recebidos!r}")

    # ---- 4. o sentido dos LEDs, byte a byte, do outro lado ----
    vistos = [bytes(int(x, 16) for x in ln.split()[1:5])
              for ln in saida.splitlines() if ln.startswith("ARNES_LED ")]
    check(vistos == esperado_no_driver,
          f"LEDs no driver: esperado {esperado_no_driver!r}, veio {vistos!r}")
    fim = [ln for ln in saida.splitlines() if ln.startswith("ARNES_FIM")]
    check(len(fim) == 1, f"uma linha ARNES_FIM, veio {fim!r}")
    if fim:
        # close=1 quer dizer que o close() do canal resolveu a I/O pendente e NAO
        # precisou vazar o estado do rele. E o unico jeito de observar isso de fora.
        check("close=1" in fim[0], f"close limpo no cliente: {fim[0]!r}")
        check("enviados=10" in fim[0], f"o cliente enviou 10 pacotes: {fim[0]!r}")
        check("recebidos=3" in fim[0], f"o cliente recebeu 3 LEDs: {fim[0]!r}")
    check("ARNES_FALHA" not in saida, "nenhuma ARNES_FALHA na saida do cliente")

    print(f"ARNES_CANAL: {_ok + _falhas} verificacoes, {_falhas} falhas")
    return 1 if _falhas else 0

if __name__ == "__main__":
    sys.exit(main())
