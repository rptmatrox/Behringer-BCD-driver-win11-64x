"""
ARNES DO INVARIANTE DA ARQUITETURA - a porta virtual nasce UMA VEZ e nunca fecha.

    python arnes_invariante.py

O invariante que a Tarefa 10 existe para garantir e um so: *** a porta MIDI virtual
'BCD3000' e criada exatamente UMA VEZ por execucao do bridge e nunca e fechada, nem na
passagem de bastao, nem em queda de cabo, nem quando o software de DJ para o audio. ***
Ele nasceu de um defeito medido tres vezes no hardware: o software de DJ NAO volta a
procurar o controlador depois de a porta desaparecer com ele aberto.

O portao de hardware verifica isso contando UMA linha no bridge.log de uma sessao
inteira. Isto aqui e o que se pode verificar sem hardware, das duas formas possiveis:

  ESTATICA  - por contagem de pontos de chamada no fonte. E a unica metade que cobre
              caminhos que nao se consegue exercitar sem aparelho (queda de cabo, por
              exemplo): se `virtualMIDIClosePort` tem UM ponto de chamada e ele esta
              dentro de `encerrar_porta`, nenhum caminho de execucao pode fechar a
              porta por outra via, tenha ele sido exercitado ou nao.
  DINAMICA  - rodando o `run()` DE VERDADE, com o aparelho e a teVirtualMIDI
              substituidos, duas passagens de bastao simuladas e dois drivers em
              sequencia no canal.

A passagem de bastao e simulada trocando `driver_quer_aparelho` por uma bandeira, e
NAO sinalizando o evento nomeado de verdade. Isto e deliberado: o evento e visivel a
toda a sessao do Windows, e sinaliza-lo faria o BCD3000Bridge.exe que roda nesta
maquina soltar o aparelho de um usuario que nao pediu nada. O mesmo motivo pelo qual o
test_handoff do lado C++ PULA quando ha pedido vivo.

*** NAO RODAR COM O BCD3000Bridge.exe DESTA TAREFA NO AR: *** o `run()` daqui cria o
mesmo nome de canal. Ha guarda, e ela sai com codigo 3 dizendo que nada foi provado.

Saida: `ARNES_INVARIANTE: N verificacoes, M falhas`, codigo 0 se M == 0.
"""
import os, re, subprocess, sys, tempfile, threading, time
import ctypes as C
from ctypes import wintypes as W

AQUI = os.path.dirname(os.path.abspath(__file__))
TESTS_EXE = os.path.join(AQUI, "..", "native", "bcdasio", "tests.exe")
FONTE = os.path.join(AQUI, "bridge_service.py")

# Desvio do log ANTES do import, pelo mesmo motivo do arnes_canal: o bridge_service
# configura o logging no nivel do modulo, e aqui o run() de verdade REGISTRA bastante.
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

class TevmContador:
    """teVirtualMIDI substituta que CONTA criacoes e fechamentos."""
    def __init__(self):
        self.criadas = 0
        self.fechadas = 0
        self.injetados = []
    def virtualMIDICreatePortEx2(self, nome, cb, inst, tam, flags):
        self.criadas += 1
        return 0xBEEF
    def virtualMIDIClosePort(self, p):
        self.fechadas += 1
    def virtualMIDISendData(self, port, arr, tam):
        self.injetados.append(bytes(bytearray(arr[:tam])))
        return True

def nome_do_canal_existe():
    k32 = C.WinDLL("kernel32", use_last_error=True)
    k32.WaitNamedPipeW.argtypes = [W.LPCWSTR, W.DWORD]
    k32.WaitNamedPipeW.restype = W.BOOL
    if k32.WaitNamedPipeW(bs.RELAY_PIPE_NAME, 1):
        return True
    return C.get_last_error() != 2      # 2 = ERROR_FILE_NOT_FOUND

def entregar_led(msg):
    arr = (C.c_ubyte * len(msg)).from_buffer_copy(msg)
    bs.rx_callback(None, arr, len(msg), None)

# ---------------------------------------------------------------------------
def metade_estatica():
    linhas = open(FONTE, "r", encoding="utf-8", errors="replace").read().splitlines()

    def linhas_com(padrao):
        rx = re.compile(padrao)
        return [i for i, ln in enumerate(linhas)
                if rx.search(ln) and not ln.lstrip().startswith("#")]

    # Uma DEFINICAO e uma CHAMADA para cada uma das tres funcoes da porta. Duas
    # chamadas de criar_porta seriam duas portas; duas de encerrar_porta seriam um
    # fechamento fora do fim do processo.
    for nome in ("criar_porta", "abrir_porta_uma_vez", "encerrar_porta"):
        defs = linhas_com(r"^def " + nome + r"\(")
        chamadas = [i for i in linhas_com(r"(?<![\w.])" + nome + r"\(")
                    if i not in defs]
        check(len(defs) == 1, f"{nome}: 1 definicao, achei {len(defs)}")
        check(len(chamadas) == 1, f"{nome}: 1 chamada, achei {len(chamadas)}")

    # O que REALMENTE fecha a porta. As linhas de `.argtypes`/`.restype` nao contam
    # porque nao tem parentese logo depois do nome - e por isso o padrao exige um.
    fecha = linhas_com(r"virtualMIDIClosePort\(")
    cria  = linhas_com(r"virtualMIDICreatePortEx2\(")
    check(len(fecha) == 1, f"virtualMIDIClosePort: 1 chamada, achei {len(fecha)}")
    check(len(cria) == 1, f"virtualMIDICreatePortEx2: 1 chamada, achei {len(cria)}")

    # E a UNICA chamada de fechamento tem de estar DENTRO de encerrar_porta. Sem
    # esta linha, "uma chamada" nao diria de onde.
    def_enc = linhas_com(r"^def encerrar_porta\(")[0]
    proxima_def = min([i for i in linhas_com(r"^def ") if i > def_enc] + [len(linhas)])
    check(def_enc < fecha[0] < proxima_def,
          "a unica chamada de virtualMIDIClosePort esta dentro de encerrar_porta")

    # fechar_porta era a funcao que a Tarefa 9 chamava na passagem de bastao. Ela ter
    # DESAPARECIDO e o que garante que nenhum caminho novo a use por engano.
    check(not linhas_com(r"(?<![\w.])fechar_porta\b"),
          "fechar_porta nao existe mais em lugar nenhum")

    # A porta e criada ANTES do laco principal, e nao dentro dele: dentro, cada volta
    # de reconexao criaria uma porta nova - que e literalmente o defeito antigo.
    chamada_abrir = [i for i in linhas_com(r"(?<![\w.])abrir_porta_uma_vez\(")
                     if i not in linhas_com(r"^def abrir_porta_uma_vez\(")][0]
    def_run = linhas_com(r"^def run\(")[0]
    laco = min(i for i in linhas_com(r"^\s*while True:") if i > def_run)
    finally_run = min(i for i in linhas_com(r"^\s*finally:") if i > def_run)
    check(def_run < chamada_abrir < laco,
          "abrir_porta_uma_vez() e chamada dentro de run() e ANTES do laco principal")
    chamada_enc = [i for i in linhas_com(r"(?<![\w.])encerrar_porta\(") if i != def_enc][0]
    check(chamada_enc > finally_run,
          "encerrar_porta() so aparece DEPOIS do finally do run()")

    # O laco de reconexao do aparelho nao pode mencionar a porta. Se mencionar, a
    # queda de cabo volta a tocar nela.
    fim_laco = finally_run
    trecho = "\n".join(linhas[laco:fim_laco])
    check("criar_porta" not in trecho and "encerrar_porta" not in trecho and
          "virtualMIDIClosePort" not in trecho,
          "o laco principal (bastao + reconexao de aparelho) nao toca na porta")

# ---------------------------------------------------------------------------
def metade_dinamica():
    grav = TevmContador()
    bs.tevm = grav

    # O aparelho e removido do desenho de proposito: sem isto, um aparelho LIGADO
    # seria ABERTO por este arnes e roubado do BCD3000Bridge.exe do usuario.
    def sem_aparelho():
        raise RuntimeError("arnes: sem aparelho de proposito")
    bs.open_dev_full = sem_aparelho

    pedido = {"vivo": False}
    bs.driver_quer_aparelho = lambda: pedido["vivo"]

    threading.Thread(target=bs.run, name="run-arnes", daemon=True).start()
    limite = time.monotonic() + 5
    while grav.criadas == 0 and time.monotonic() < limite:
        time.sleep(0.02)
    check(grav.criadas == 1, f"a porta nasceu UMA vez, criadas={grav.criadas}")
    check(bs.port_atual is not None, "port_atual publicada")
    time.sleep(0.3)     # o servidor do canal sobe num thread daemon do proprio run()

    def rodar_driver(mensagens, leds_durante):
        """Roda um cliente REAL e devolve (linha ARNES_FIM, pacotes, injecoes)."""
        base = dict(bs._cont)
        def por_os_leds():
            fim = time.monotonic() + 5
            while time.monotonic() < fim:
                if bs._cont["canal_pkts"] - base["canal_pkts"] >= 2 * mensagens:
                    break
                time.sleep(0.01)
            for m in leds_durante:
                entregar_led(m)
        t = threading.Thread(target=por_os_leds, daemon=True)
        t.start()
        saida = subprocess.run([TESTS_EXE, "rele-cliente", str(mensagens), "700"],
                               capture_output=True, text=True, timeout=60).stdout
        t.join(timeout=10)
        fim = [ln for ln in saida.splitlines() if ln.startswith("ARNES_FIM")]
        return (fim[0] if fim else ""), \
               bs._cont["canal_pkts"] - base["canal_pkts"], \
               bs._cont["canal_inj"] - base["canal_inj"]

    # ---- LEDs de uma conexao ANTERIOR sao jogados fora, nao entregues ----
    # Estado de VU de segundos atras nao ajuda ninguem, e a rajada atrasaria os LEDs
    # de verdade. Estes quatro entram na fila SEM driver conectado.
    for m in (b"\x90\x01\x7F", b"\x90\x02\x7F", b"\x90\x03\x7F", b"\x90\x04\x7F"):
        entregar_led(m)
    check(bs._fila_led.qsize() == 4, f"4 LEDs velhos na fila, {bs._fila_led.qsize()}")

    # ---- PASSAGEM DE BASTAO #1 e o primeiro driver ----
    pedido["vivo"] = True
    time.sleep(0.3)
    fim1, pkts1, inj1 = rodar_driver(3, [])
    check(pkts1 == 6, f"driver 1: 6 pacotes (3 uteis + 3 de enchimento), veio {pkts1}")
    check(inj1 == 3, f"driver 1: 3 injecoes, veio {inj1}")
    check("recebidos=0" in fim1,
          f"driver 1 NAO recebeu os LEDs velhos (a fila e esvaziada na conexao): {fim1!r}")
    check("close=1" in fim1, f"driver 1 fechou o canal limpo: {fim1!r}")
    check(bs._fila_led.qsize() == 0, "a fila ficou vazia")

    # ---- bastao devolvido, e PASSAGEM #2 com um SEGUNDO driver ----
    # O segundo driver e o que prova os modos de falha 2 e 4 do plano: o servidor
    # volta a escutar sozinho depois de o primeiro cliente ir embora, sem ninguem
    # reiniciar nada.
    pedido["vivo"] = False
    time.sleep(0.4)
    pedido["vivo"] = True
    time.sleep(0.3)
    fim2, pkts2, inj2 = rodar_driver(2, [b"\x90\x20\x7F", b"\xB0\x0A\x40"])
    check(pkts2 == 4, f"driver 2: 4 pacotes, veio {pkts2}")
    check(inj2 == 2, f"driver 2: 2 injecoes, veio {inj2}")
    check("recebidos=2" in fim2, f"driver 2 recebeu os 2 LEDs de agora: {fim2!r}")
    check("close=1" in fim2, f"driver 2 fechou o canal limpo: {fim2!r}")

    # ---- O INVARIANTE, depois de tudo ----
    pedido["vivo"] = False
    time.sleep(0.3)
    check(grav.criadas == 1,
          f"*** a porta foi criada UMA vez apesar das 2 passagens de bastao e dos 2 "
          f"drivers: criadas={grav.criadas}")
    check(grav.fechadas == 0,
          f"*** a porta NUNCA foi fechada: fechadas={grav.fechadas}")

    # ---- e SO encerrar_porta() fecha ----
    bs.encerrar_porta()
    check(grav.fechadas == 1, f"encerrar_porta() fecha, fechadas={grav.fechadas}")
    check(bs.port_atual is None, "port_atual zerada antes do fechamento")

# ---------------------------------------------------------------------------
def main():
    print("== arnes do invariante da porta virtual ==")
    if not os.path.exists(TESTS_EXE):
        print(f"ARNES_PULADO: {TESTS_EXE} nao existe. Rode build.bat tests primeiro.")
        return 3
    if nome_do_canal_existe():
        print(f"ARNES_PULADO: ja existe um servidor em '{bs.RELAY_PIPE_NAME}' "
              f"(BCD3000Bridge.exe desta tarefa rodando?). NADA foi provado.")
        return 3
    metade_estatica()
    metade_dinamica()
    print(f"ARNES_INVARIANTE: {_ok + _falhas} verificacoes, {_falhas} falhas")
    return 1 if _falhas else 0

if __name__ == "__main__":
    sys.exit(main())
