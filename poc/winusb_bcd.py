"""
Acesso direto ao WinUSB da BCD3000 (contorna a limitacao da libusb com
aparelhos multi-interface numa funcao unica).

Usa a API oficial WinUSB do Windows:
  CreateFile -> WinUsb_Initialize (IF0) -> WinUsb_GetAssociatedInterface(idx=2 -> IF3)
  -> WinUsb_ReadPipe(0x81)  (controles)  /  WinUsb_WritePipe(0x01)  (LEDs)

Modos:
  python winusb_bcd.py validate        # abre e testa leitura por ~3s (sem precisar mexer)
  python winusb_bcd.py capture [seg]   # captura eventos dos controles (mexa nos controles)
"""
import sys, time, ctypes as C
from ctypes import wintypes as W
import winreg

DEV_KEY = r"SYSTEM\CurrentControlSet\Enum\USB\VID_1397&PID_00BF&MI_00"
EP_CTRL_IN  = 0x81   # controles  (dev -> PC)
EP_LED_OUT  = 0x01   # LEDs/VU    (PC -> dev)
IF3_ASSOC_INDEX = 2  # 0->IF1, 1->IF2, 2->IF3

# Prazo de pipe da escrita de LED, em ms. MESMO valor do kLedWriteTimeoutMs do lado
# C++ (native/bcdasio/midibridge.h), e a igualdade e proposital - ver o docstring de
# open_dev_full(), onde este numero entra no orcamento da passagem de bastao.
LED_WRITE_TIMEOUT_MS = 100

setupapi = C.WinDLL("setupapi", use_last_error=True); winusb = C.WinDLL("winusb", use_last_error=True)
kernel32 = C.WinDLL("kernel32", use_last_error=True); ole32 = C.WinDLL("ole32")

class GUID(C.Structure):
    _fields_ = [("Data1", W.DWORD), ("Data2", W.WORD), ("Data3", W.WORD), ("Data4", C.c_ubyte*8)]
class SP_DEVICE_INTERFACE_DATA(C.Structure):
    _fields_ = [("cbSize", W.DWORD), ("InterfaceClassGuid", GUID), ("Flags", W.DWORD), ("Reserved", C.POINTER(C.c_ulonglong))]
class SP_DEV_DETAIL(C.Structure):
    _fields_ = [("cbSize", W.DWORD), ("DevicePath", W.WCHAR*512)]

ole32.CLSIDFromString.argtypes = [W.LPCWSTR, C.POINTER(GUID)]; ole32.CLSIDFromString.restype = C.c_long
setupapi.SetupDiGetClassDevsW.argtypes = [C.POINTER(GUID), W.LPCWSTR, C.c_void_p, W.DWORD]; setupapi.SetupDiGetClassDevsW.restype = W.HANDLE
setupapi.SetupDiEnumDeviceInterfaces.argtypes = [W.HANDLE, C.c_void_p, C.POINTER(GUID), W.DWORD, C.POINTER(SP_DEVICE_INTERFACE_DATA)]; setupapi.SetupDiEnumDeviceInterfaces.restype = W.BOOL
setupapi.SetupDiGetDeviceInterfaceDetailW.argtypes = [W.HANDLE, C.POINTER(SP_DEVICE_INTERFACE_DATA), C.POINTER(SP_DEV_DETAIL), W.DWORD, C.POINTER(W.DWORD), C.c_void_p]; setupapi.SetupDiGetDeviceInterfaceDetailW.restype = W.BOOL
kernel32.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p, W.DWORD, W.DWORD, W.HANDLE]; kernel32.CreateFileW.restype = W.HANDLE
winusb.WinUsb_Initialize.argtypes = [W.HANDLE, C.POINTER(C.c_void_p)]; winusb.WinUsb_Initialize.restype = W.BOOL
winusb.WinUsb_GetAssociatedInterface.argtypes = [C.c_void_p, C.c_ubyte, C.POINTER(C.c_void_p)]; winusb.WinUsb_GetAssociatedInterface.restype = W.BOOL
winusb.WinUsb_SetPipePolicy.argtypes = [C.c_void_p, C.c_ubyte, W.ULONG, W.ULONG, C.c_void_p]; winusb.WinUsb_SetPipePolicy.restype = W.BOOL
winusb.WinUsb_ReadPipe.argtypes = [C.c_void_p, C.c_ubyte, C.POINTER(C.c_ubyte), W.ULONG, C.POINTER(W.ULONG), C.c_void_p]; winusb.WinUsb_ReadPipe.restype = W.BOOL
winusb.WinUsb_WritePipe.argtypes = [C.c_void_p, C.c_ubyte, C.POINTER(C.c_ubyte), W.ULONG, C.POINTER(W.ULONG), C.c_void_p]; winusb.WinUsb_WritePipe.restype = W.BOOL
winusb.WinUsb_Free.argtypes = [C.c_void_p]; winusb.WinUsb_Free.restype = W.BOOL
kernel32.CloseHandle.argtypes = [W.HANDLE]; kernel32.CloseHandle.restype = W.BOOL

INVALID = C.c_void_p(-1).value
PIPE_TRANSFER_TIMEOUT = 0x03

def _guid():
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, DEV_KEY) as root:
        for i in range(64):
            try:
                sub = winreg.EnumKey(root, i)
            except OSError:
                break
            try:
                with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, DEV_KEY + "\\" + sub + "\\Device Parameters") as dp:
                    for name in ("DeviceInterfaceGUIDs", "DeviceInterfaceGUID"):
                        try:
                            v, _ = winreg.QueryValueEx(dp, name)
                            return v[0] if isinstance(v, list) else v
                        except FileNotFoundError:
                            pass
            except FileNotFoundError:
                pass
    return None

def open_base():
    """Abre o device WinUSB e retorna (path, file_handle, usb_handle base=IF0)."""
    gs = _guid()
    if not gs: raise RuntimeError("DeviceInterfaceGUID nao encontrado (Zadig aplicou WinUSB?)")
    guid = GUID()
    if ole32.CLSIDFromString(gs, C.byref(guid)) != 0: raise RuntimeError("GUID invalido: "+gs)
    hdev = setupapi.SetupDiGetClassDevsW(C.byref(guid), None, None, 0x10 | 0x02)  # DEVICEINTERFACE|PRESENT
    did = SP_DEVICE_INTERFACE_DATA(); did.cbSize = C.sizeof(did)
    if not setupapi.SetupDiEnumDeviceInterfaces(hdev, None, C.byref(guid), 0, C.byref(did)):
        raise RuntimeError("nenhuma interface WinUSB encontrada")
    detail = SP_DEV_DETAIL(); detail.cbSize = 8 if C.sizeof(C.c_void_p) == 8 else 6
    if not setupapi.SetupDiGetDeviceInterfaceDetailW(hdev, C.byref(did), C.byref(detail), C.sizeof(detail), None, None):
        raise RuntimeError("GetDeviceInterfaceDetail falhou err=%d" % C.get_last_error())
    path = detail.DevicePath
    h = kernel32.CreateFileW(path, 0xC0000000, 0x1 | 0x2, None, 3, 0x40000000 | 0x80, None)  # GENERIC RW, SHARE RW, OPEN_EXISTING, OVERLAPPED|NORMAL
    if h == INVALID or h is None: raise RuntimeError("CreateFile falhou err=%d" % C.get_last_error())
    usbh = C.c_void_p()
    if not winusb.WinUsb_Initialize(h, C.byref(usbh)):
        err = C.get_last_error()
        # Fechar o handle do CreateFile antes de propagar: no WinUSB o aparelho
        # aceita UM processo por vez, e um handle vazado aqui o prende a este
        # processo ate ele morrer - inclusive contra o driver ASIO.
        kernel32.CloseHandle(h)
        raise RuntimeError("WinUsb_Initialize err=%d" % err)
    return path, h, usbh

def assoc(usbh, idx):
    """Retorna o handle da interface associada (0->IF1, 1->IF2, 2->IF3)."""
    hnd = C.c_void_p()
    if not winusb.WinUsb_GetAssociatedInterface(usbh, idx, C.byref(hnd)):
        raise RuntimeError("GetAssociatedInterface(%d) err=%d" % (idx, C.get_last_error()))
    return hnd

def open_dev_full():
    """Abre e retorna (path, if3, dev), pronto para MIDI.

    `dev` e um pacote opaco de handles que serve para close_dev(). Existe porque
    open_dev() DESCARTA o handle do CreateFile: quem usa open_dev() nao consegue
    soltar o aparelho depois - o handle fica aberto ate o processo morrer, e no
    WinUSB o aparelho aceita UM processo por vez. Quem precisa devolver o
    aparelho (a passagem de bastao com o driver ASIO) tem de usar esta funcao.

    OS DOIS ENDPOINTS GANHAM PRAZO DE PIPE, e o do LED nao e enfeite: quem solta o
    aparelho (soltar_aparelho no bridge_service.py) ESPERA pela trava _led_lock, e o
    callback de LED segura essa trava atravessando a escrita no EP 0x01. Sem prazo
    nesse endpoint, o orcamento da passagem de bastao com o driver ASIO nao tem
    limite superior nenhum - o driver desiste depois de 15 x 200 ms e o audio nao
    sobe. Os 100 ms sao os MESMOS do lado C++ (kLedWriteTimeoutMs em
    native/bcdasio/midibridge.h), que poe prazo no mesmo endpoint pelo mesmo motivo:
    a escrita de LED nao pode segurar quem tambem cuida dos controles. Assimetria
    entre os dois lados neste numero seria defeito, nao estilo.
    """
    path, h, usbh = open_base()
    try:
        if3 = assoc(usbh, IF3_ASSOC_INDEX)
        to = W.ULONG(400)
        winusb.WinUsb_SetPipePolicy(if3, EP_CTRL_IN, PIPE_TRANSFER_TIMEOUT, C.sizeof(to), C.byref(to))
        to_led = W.ULONG(LED_WRITE_TIMEOUT_MS)
        winusb.WinUsb_SetPipePolicy(if3, EP_LED_OUT, PIPE_TRANSFER_TIMEOUT, C.sizeof(to_led), C.byref(to_led))
    except Exception:
        # Sem isto, faltar a IF3 deixaria o aparelho preso a este processo para
        # sempre - o handle do CreateFile ja esta aberto neste ponto.
        close_dev((None, usbh, h))
        raise
    return path, if3, (if3, usbh, h)

def close_dev(dev):
    """Solta o aparelho DE VERDADE e devolve None (para o chamador atribuir).

    Libera as interfaces WinUSB e FECHA o handle do CreateFile, que e o que da a
    posse exclusiva do aparelho. Ordem inversa da aquisicao - a mesma de
    UsbDevice::close() no lado C++. Inofensivo com None.
    """
    if not dev:
        return None
    if3, usbh, h = dev
    if if3:  winusb.WinUsb_Free(if3)
    if usbh: winusb.WinUsb_Free(usbh)
    if h:    kernel32.CloseHandle(h)
    return None

def open_dev():
    """Abre e retorna (path, if3) pronto para MIDI (compat com o serviço).

    ATENCAO: descarta os handles de base, entao NAO da para soltar o aparelho
    depois. Serve para programas de uma tacada, hoje so o main deste arquivo.
    Quem precisa devolver o aparelho usa open_dev_full()/close_dev(), e o
    bridge_service.py usa esse par -- descartar o handle aqui foi um vazamento
    real que travava o aparelho no processo do servico.
    """
    path, if3, _dev = open_dev_full()
    return path, if3

def read_ctrl(if3):
    buf = (C.c_ubyte*64)(); n = W.ULONG(0)
    ok = winusb.WinUsb_ReadPipe(if3, EP_CTRL_IN, buf, 64, C.byref(n), None)
    if ok: return bytes(buf[:n.value])
    err = C.get_last_error()
    if err in (121, 995): return b""   # SEM_TIMEOUT / operacao abortada por timeout
    raise RuntimeError("ReadPipe err=%d" % err)

def write_led(if3, data):
    """Escreve um pacote USB-MIDI de 4 bytes no EP 0x01 (LEDs/VU)."""
    b = bytes(data)
    buf = (C.c_ubyte * len(b)).from_buffer_copy(b); n = W.ULONG(0)
    ok = winusb.WinUsb_WritePipe(if3, EP_LED_OUT, buf, len(b), C.byref(n), None)
    if not ok:
        raise RuntimeError("WritePipe err=%d" % C.get_last_error())
    return n.value

CIN = {0x8:"NoteOff",0x9:"NoteOn",0xB:"CtrlChange",0xE:"PitchBend",0xF:"1Byte"}

def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "validate"
    path, if3 = open_dev()
    print("[OK] aberto:", path)
    if mode == "validate":
        t0 = time.time(); got = 0; reads = 0
        while time.time() - t0 < 3:
            d = read_ctrl(if3); reads += 1
            if d: got += 1; print("  dado:", d.hex(' '))
        print(f"[OK] acesso funcionando. {reads} leituras, {got} com dados (0 e normal sem mexer).")
    elif mode == "capture":
        dur = int(sys.argv[2]) if len(sys.argv) > 2 else 45
        print("="*56); print(f"MEXA NOS CONTROLES por {dur}s"); print("="*56)
        t0 = time.time(); n = 0
        while time.time() - t0 < dur:
            d = read_ctrl(if3)
            for i in range(0, len(d)-3, 4):
                p = d[i:i+4]
                if p == b"\0\0\0\0": continue
                n += 1
                print(f"#{n:04d} raw={p.hex(' ')} CIN=0x{p[0]&0xF:X}({CIN.get(p[0]&0xF,'?')}) midi={p[1]:02X} {p[2]:02X} {p[3]:02X}")
        print(f"== fim: {n} eventos ==")

if __name__ == "__main__":
    C.windll.kernel32.SetLastError(0)
    main()
