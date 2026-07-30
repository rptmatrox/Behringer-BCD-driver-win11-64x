#pragma once

#include <windows.h>
#include <winusb.h>

// So por kChannels, que e o tamanho dos vetores de nome de canal do perfil. O
// formato de audio (4 canais, 16 bits, 44100 Hz) e IGUAL nos dois modelos - ver a
// tabela em usbdev.cpp -, entao ele continua sendo constante do projeto e nao
// campo de perfil.
#include "format.h"

namespace bcd {

// ===========================================================================
// PERFIL DE APARELHO
//
// POR QUE ISTO EXISTE: ate esta tarefa o aparelho estava gravado em tres lugares
// diferentes - a chave do registro aqui no usbdev.cpp, as constantes de endpoint e
// de indice de interface no alto deste header, e os nomes de canal dentro do
// getChannelInfo() do bcdasio.cpp. Acrescentar um modelo era garimpar constantes em
// tres arquivos. Agora e acrescentar UMA ENTRADA na tabela de usbdev.cpp.
//
// O criterio da forma: o perfil e DADO PURO (sem funcao virtual, sem construtor,
// sem alocacao) e as funcoes que o consultam sao PURAS (nao tocam em registro nem
// em USB). E o que faz a tabela e a escolha de perfil serem testaveis por unidade
// sem hardware nenhum - e este projeto nao tem BCD2000 para testar com.
// ===========================================================================

// Protocolo do endpoint de controles/LEDs.
enum ControlProtocol {
    // USB-MIDI 1.0: pacotes de 4 bytes com CIN. E o que o EP 0x81 da BCD3000
    // entrega, o que o EP 0x01 dela consome, e o unico enquadramento que a
    // MidiBridge e o BCD3000Bridge.exe sabem repassar (o canal entre os dois e de
    // mensagens de kRelayPacketBytes = 4 bytes).
    kCtrlUsbMidi10 = 0,

    // Enquadramento PROPRIETARIO da BCD2000. Fonte: driver Linux anyc/snd-bcd2000,
    // arquivo midi.c - na saida, 3 bytes de prefixo mais 1 de tamanho antes dos
    // dados; na entrada, 1 byte de tamanho antes dos dados. E o aparelho ainda
    // exige uma sequencia de inicializacao de 52 bytes antes de falar.
    //
    // NADA DISSO ESTA IMPLEMENTADO, e por isso este valor existe: ele e o que faz
    // MidiBridge::start() RECUSAR o modelo com uma mensagem clara, em vez de subir
    // um thread que leria bytes e os repassaria como se fossem pacotes de 4 bytes -
    // o que produziria LEDs e controles aleatorios em vez de uma falha legivel.
    kCtrlBcd2000Proprietary = 1
};

// Nome do protocolo, para o log. Nunca devolve nulo.
const char* controlProtocolName(int protocol);

// UM aparelho, inteiro. Dado puro: pode viver em memoria estatica de leitura.
struct DeviceProfile {
    // Nome do modelo. Entra no log, no nome da fonte de relogio do ASIO e nas
    // mensagens de diagnostico. Curto de proposito: ele e montado dentro de
    // buffers de 32 bytes do ASIO.
    const char*    model;

    // Identidade USB. A chave do registro e MONTADA a partir destes tres campos
    // por profileEnumKey() - nao existe a mesma informacao escrita duas vezes,
    // que era metade do problema que esta tabela resolve.
    unsigned short vid;
    unsigned short pid;
    // Sufixo da FUNCAO dentro do aparelho composto, como o enumerador USB do
    // Windows o escreve. Na BCD3000 e a funcao 0 ("MI_00"), que e onde o Zadig
    // aplica o WinUSB e onde as quatro interfaces ficam agrupadas.
    const char*    functionId;

    // Endpoints.
    unsigned char  epPlayback;      // ISO OUT, na interface de playback
    unsigned char  epCapture;       // ISO IN,  na interface de captura
    unsigned char  epControls;      // BULK IN,  botoes/faders/jogs
    unsigned char  epLeds;          // BULK OUT, LEDs e VU

    // Alternate settings das duas interfaces de audio.
    unsigned char  altStreaming;    // liga o streaming
    unsigned char  altIdle;         // desliga

    // Indices de WinUsb_GetAssociatedInterface, a partir da interface base.
    unsigned char  assocPlayback;
    unsigned char  assocCapture;
    unsigned char  assocMidi;

    // Um dos ControlProtocol.
    int            controlProtocol;

    // Se ESTE PERFIL INTEIRO foi provado no hardware por alguem deste projeto.
    //
    // Nao e enfeite e nao e otimismo: com false, o usbdev registra no log que o
    // caminho e experimental no momento em que o perfil casa, e os testes exigem
    // que os nomes de canal sejam genericos. Um perfil que nunca rodou e uma
    // hipotese, e o codigo tem de dizer isso em voz alta em vez de prometer.
    bool           provenOnHardware;

    // Nomes de canal, na ordem dos canais do aparelho. Cada um tem de caber nos
    // 32 bytes do ASIOChannelInfo::name - conferido por teste.
    const char*    outNames[kChannels];
    const char*    inNames[kChannels];
};

// A TABELA, por indice. A ORDEM E A ORDEM DE BUSCA, e ela e load-bearing: o perfil
// 0 e o unico validado no hardware e tem de ser procurado primeiro. Devolve 0 fora
// da tabela.
int                  profileCount();
const DeviceProfile* profileAt(int index);

// O perfil de um PID, ou 0 se o PID e desconhecido.
const DeviceProfile* profileForPid(unsigned short pid);

// O perfil usado quando nenhuma busca casou ainda: o perfil 0. Existe para que
// getChannelInfo() e companhia NUNCA fiquem sem nomes de canal - o host pode
// perguntar antes de o aparelho ser encontrado, e antes desta tarefa a resposta
// nesse caso era (corretamente) a da BCD3000.
const DeviceProfile& defaultProfile();

// Tamanho maximo de uma chave montada por profileEnumKey(), com folga.
const int kEnumKeyMax = 256;

// Monta a chave do registro do perfil em HKEY_LOCAL_MACHINE. false quando nao cabe
// (e nesse caso `out` fica vazio, nunca com meia chave).
//
// Funcao PURA de proposito: e ela que o teste compara, caractere por caractere, com
// a constante literal que existia antes desta tarefa.
bool profileEnumKey(const DeviceProfile& profile, char* out, int size);

// ===========================================================================
// MENSAGENS DE DIAGNOSTICO, E O LIMITE DE QUEM AS CONSOME
//
// As mensagens de falha deste arquivo nao param aqui: elas viram
// UsbDevice::lastError(), e a casca ASIO as entrega ao software de DJ pelo
// BcdAsioDriver::getErrorMessage(). O contrato desse metodo esta escrito no metodo
// 4 de asioapi.h: "no maximo 124 bytes com o terminador", e o buffer e DO HOST.
//
// POR QUE O NUMERO MORA AQUI, e nao so no bcdasio.h: e aqui que as mensagens
// NASCEM, e o alvo `tests` do build.bat linka este arquivo e NAO linka o
// bcdasio.cpp. Sem a constante deste lado, a assercao que impede a reincidencia
// nao teria onde existir.
//
// E ela existe porque o defeito ja aconteceu, em campo, em 2026-07-29: a mensagem
// de interface inativa ficou com 217 caracteres, o `strcpy` do getErrorMessage
// escrevia 128 bytes num buffer de 124 - quatro bytes alem do fim, dentro do
// processo do software de DJ, quatro vezes no mesmo dia - e o corte caia no meio da
// SEGUNDA causa: o dono leu a causa errada e a certa (cabo mal encaixado)
// desapareceu da tela E do log. Limitar a copia sozinho trocaria estouro por
// mentira silenciosa; o que fecha o caso e a mensagem CABER.
// ===========================================================================
const int kAsioErrorMax = 124;

// A mensagem cabe no contrato do getErrorMessage do ASIO?
//
// Pura, e publica por um motivo: e o que permite ao teste unitario renderizar todas
// as mensagens deste arquivo e afirmar que cada uma cabe. `msg` nulo conta como
// cabendo - nao ha nada a copiar.
bool diagnosticFitsAsio(const char* msg);

// ===========================================================================
// E O LOG NAO TEM ESSE LIMITE, NEM DEVE TER
//
// O contrato de 124 bytes e do HOST. O LOG deste projeto e a ferramenta de
// diagnostico dele, e nao tem contrato nenhum - a rodada que encurtou a mensagem de
// interface inativa de 217 para 117 caracteres para caber no host encurtou tambem o
// que ia para o log, e o log passou a dizer MENOS do que dizia sobre o cenario que
// custou uma hora de investigacao em 2026-07-29. Perderam-se, palavra por palavra,
// "(maquina virtual?)" e "o vinculo do Zadig se perdeu".
//
// Por que UMA cadeia deixou de ser a escolha certa: a objecao contra duas formas era
// que a invariante "quem escreve a longa lembra da curta" ficaria espalhada por 8
// pontos de escrita. Isso era verdade ANTES do refatoramento que extraiu
// findStageMessage(). Depois dele o mapa estagio->texto existe em UM lugar puro e
// quem registra no log existe em UM lugar (findPath), entao a forma longa tem
// exatamente um ponto de escrita e um de log, e o laco por FAIXA da enum que o teste
// percorre cobre as duas de graca. A invariante deixou de ser humana e passou a ser
// estrutural.
//
// Este e o tamanho do buffer em que findPath() renderiza a forma longa. Ele NAO e
// contrato de ninguem, e por isso e generoso; o que ele evita e truncamento
// SILENCIOSO no log, que seria repetir o defeito de 2026-07-29 com outro numero. A
// maior forma longa de hoje tem 553 caracteres (medida, nao estimada), e o teste
// renderiza cada uma num buffer MAIOR que este para medir o tamanho VERDADEIRO antes
// de comparar - medir dentro de um buffer do tamanho do limite faria a assercao passar
// sempre.
const int kDiagnosticLogMax = 768;
// ===========================================================================

// ---------------------------------------------------------------------------
// ESTAGIOS DA BUSCA
//
// A ordem dos valores e de PROXIMIDADE CRESCENTE: quanto maior, mais perto de
// achar o aparelho. E o que permite escolher UMA mensagem final depois de tentar
// todos os perfis - vale a evidencia mais forte que qualquer perfil produziu, e
// empate fica com o primeiro (a BCD3000).
//
// Estes valores viviam dentro do usbdev.cpp. Subiram para ca porque
// findStageMessage() e publica: e ela que o teste percorre, estagio por estagio,
// para provar que nenhuma mensagem estoura o contrato do ASIO.
// ---------------------------------------------------------------------------
enum FindStage {
    kStageHardError   = -1, // err_ JA preenchido; a busca inteira aborta
    kStageNoNode      = 0,  // nao existe no do aparelho no registro
    kStageNoGuid      = 1,  // o no existe, sem DeviceInterfaceGUID
    kStageBadGuid     = 2,  // ha um GUID escrito, e ele nao e um GUID
    kStageNoInterface = 3,  // GUID valido, NENHUMA interface registrada para ele
    kStageNotPresent  = 4,  // interface REGISTRADA e nao presente
    kStageFound       = 5   // registrada, presente e com caminho lido
};

// Quantos estagios existem com valor nao negativo (kStageNoNode..kStageFound).
int findStageCount();

// A mensagem final de uma busca que NAO achou o aparelho, montada para um estagio.
//
// Devolve false, com `out` VAZIO, quando o estagio nao produz mensagem: e o caso do
// kStageFound (achou - nao ha o que explicar), do kStageHardError (a mensagem foi
// escrita na hora, no ponto exato da falha) e de qualquer valor fora da faixa.
//
// `detail` e o texto auxiliar do kStageBadGuid - o GUID que nao e um GUID - e pode
// ser nulo. Ele vem do REGISTRO, ou seja de FORA, e por isso a interpolacao dele e
// limitada no proprio formato. Nenhum outro argumento precisa disso: os outros saem
// da tabela de perfis, e o teste percorre a tabela inteira.
//
// PURA de proposito, e e o que torna a assercao possivel: o teste renderiza cada
// estagio com cada perfil da tabela e com um `detail` de pior caso, num buffer
// grande, e afirma que o resultado cabe em kAsioErrorMax.
bool findStageMessage(int stage, const DeviceProfile& profile, const char* detail,
                      char* out, int size);

// A ORIENTACAO EXTRA de um estagio - o que nao cabe nos 124 bytes do host e nao pode
// desaparecer do log. Devolve 0 quando o estagio nao tem nada a acrescentar.
//
// A dica e um LITERAL, sem nenhum especificador de formato, e isso e propriedade e nao
// estilo: a forma longa e CONCATENADA e nao interpolada, entao um '%' aqui viraria
// argumento faltando na hora do logWrite. O teste afirma que nenhuma dica contem '%'.
const char* findStageHint(int stage);

// A MESMA mensagem do findStageMessage() mais a dica do estagio, para o LOG. Devolve
// false exatamente nos mesmos estagios em que findStageMessage() devolve false, com
// `out` VAZIO - as duas formas existem ou faltam juntas.
//
// A LEI, e e ela que torna o fallback estrutural em vez de humano:
//
//     longa == curta                  quando findStageHint() devolve 0
//     longa == curta + " " + dica     quando devolve texto
//
// Ou seja: um estagio novo que ninguem se lembre de dotar de dica NAO fica sem forma
// longa - ele fica com a curta, que e o pior caso aceitavel e esta escrito aqui, no
// codigo, em vez de descoberto depois. O teste verifica a lei para a FAIXA INTEIRA da
// enum, entao ela vale tambem para o estagio que ainda nao existe.
//
// NAO ha assercao de 124 bytes sobre esta forma, de proposito: o limite do host e do
// host. O que o teste afirma sobre ela e que cabe em kDiagnosticLogMax, medindo o
// tamanho verdadeiro num buffer maior - ver o comentario da constante.
bool findStageMessageLong(int stage, const DeviceProfile& profile, const char* detail,
                          char* out, int size);

struct PipeDesc {
    unsigned char id;
    int           type;            // UsbdPipeTypeIsochronous == 1
    int           maxPacketSize;
    int           interval;
};

// Acesso ao aparelho pelo WinUSB. Nao sabe nada sobre audio.
class UsbDevice {
public:
    UsbDevice();
    ~UsbDevice();

    // open() registra tudo no log. openQuiet() faz exatamente o mesmo, mas SEM as
    // linhas de detalhe do caminho de falha - err_/lastError() continua sendo
    // preenchido igual, e o sucesso continua registrado.
    //
    // Existe por causa do laco de tentativas da passagem de bastao: 15 tentativas
    // falhadas escreviam ~30 linhas iguais por start() ("abrindo <caminho>" e o
    // erro 5 do CreateFile), e o software de DJ retenta a cada ~60 s - 30 linhas por
    // minuto de puro ruido. O erro da ULTIMA tentativa nao se perde: quem chama
    // imprime device.lastError() na linha de resumo.
    bool        open()      { return openInternal(true); }
    bool        openQuiet() { return openInternal(false); }
    void        close();
    bool        isOpen() const { return base_ != 0; }
    const char* lastError() const { return err_; }
    const char* devicePath() const { return path_; }

    // Verifica se o aparelho esta conectado SEM tomar posse dele. Usado pelo
    // init() do ASIO, que e chamado em todo driver instalado durante a
    // enumeracao: abrir ali roubaria o aparelho de quem estivesse usando.
    bool isPresent();

    // O PERFIL QUE CASOU na ultima busca, ou 0 se nenhuma busca casou ainda.
    // Quem precisa saber QUAL modelo esta na mesa usa este - a MidiBridge, por
    // exemplo, recusa modelos cujo protocolo de controles nao e suportado.
    const DeviceProfile* matchedProfile() const { return profile_; }

    // O mesmo, mas NUNCA nulo: o perfil que casou, ou defaultProfile() quando
    // nada casou ainda. E o acessor certo para quem tem de responder alguma coisa
    // de qualquer jeito - getChannelInfo() do ASIO responde antes de o aparelho
    // ser aberto, e a resposta correta nesse caso continua sendo a da BCD3000.
    //
    // SEGURANCA DE THREAD: profile_ so e escrito por findPath(), e SO no caminho em
    // que um perfil casa. findPath() so roda com o aparelho FECHADO (open() e
    // isPresent() devolvem antes quando ele esta aberto), entao enquanto o motor de
    // audio e a ponte MIDI rodam este valor nao muda. Nem close() nem uma busca
    // FALHADA o apagam, e as duas coisas sao decisao:
    //
    //  - close(): a informacao de qual modelo era continua util no caminho de parada
    //    e no log;
    //  - busca falhada: o zeramento que existia no topo do findPath() fazia
    //    profile() voltar a defaultProfile() = BCD3000 depois de um casamento
    //    seguido de falha - e falha e o caso ROTINEIRO (cabo arrancado, outro
    //    programa com o aparelho). Numa maquina com BCD2000, getChannelInfo()
    //    passava a responder Master/Phones/Phono, os oito nomes que a tabela proibe
    //    inventar, por uma porta que contorna o teste que os proibe.
    const DeviceProfile& profile() const
    {
        return profile_ ? *profile_ : defaultProfile();
    }

    WINUSB_INTERFACE_HANDLE playbackIf() const { return play_; }
    WINUSB_INTERFACE_HANDLE captureIf()  const { return cap_; }
    // Pode ser 0 mesmo com o aparelho aberto: a falta da IF3 nao impede o
    // audio, entao open() segue adiante e apenas registra o fato. Quem for usar
    // MIDI TEM de checar este handle antes de tocar no aparelho.
    WINUSB_INTERFACE_HANDLE midiIf()     const { return midi_; }

    bool setAlternate(WINUSB_INTERFACE_HANDLE h, unsigned char alt);
    int  queryPipes(WINUSB_INTERFACE_HANDLE h, unsigned char alt,
                    PipeDesc* out, int maxPipes);

private:
    // logDetails = false silencia SO as linhas de log; err_ e preenchido igual.
    bool openInternal(bool logDetails);
    bool findPath(bool logDetails);
    // Procura UM perfil. Devolve um dos FindStage de usbdev.cpp; em caso de acerto
    // preenche path_. Ver o comentario da funcao.
    int  probeProfile(const DeviceProfile& profile, char* detail, int detailSize,
                      bool logDetails);
    // fail() le GetLastError() por conta propria e SO pode ser chamada quando nada
    // rodou entre a chamada que falhou e ela. Quando ha uma chamada do CRT no meio -
    // um _snprintf que monta o texto, por exemplo - use failWith() com o codigo
    // capturado na linha seguinte a falha: o CRT do MSVC nao promete preservar o
    // ultimo erro, e o "(erro N)" e justamente a linha pela qual um despejo de log e
    // julgado.
    void fail(const char* what, bool logDetails);
    void failWith(const char* what, DWORD gle, bool logDetails);

    HANDLE                  file_;
    WINUSB_INTERFACE_HANDLE base_;
    WINUSB_INTERFACE_HANDLE play_;
    WINUSB_INTERFACE_HANDLE cap_;
    WINUSB_INTERFACE_HANDLE midi_;
    const DeviceProfile*    profile_;
    char                    path_[512];
    char                    err_[256];
};

}
