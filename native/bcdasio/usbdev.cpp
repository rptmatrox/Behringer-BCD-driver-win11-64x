#include "usbdev.h"
#include "log.h"

#include <new>           // std::nothrow
#include <objbase.h>     // CLSIDFromString
#include <setupapi.h>
#include <stdio.h>
#include <string.h>

namespace bcd {

// ===========================================================================
// A TABELA DE PERFIS
//
// A ORDEM E A ORDEM DE BUSCA. O perfil 0 e a BCD3000, o unico validado no
// hardware, e ele TEM de ser procurado primeiro: na maquina do dono ele casa na
// primeira volta e o perfil 1 nunca chega a ser consultado - custo zero de
// registro para o caminho que esta em producao.
//
// Acrescentar um modelo e acrescentar UMA ENTRADA aqui. Nao ha constante de
// aparelho em nenhum outro arquivo do driver.
// ===========================================================================
static const DeviceProfile kProfiles[] = {

    // -----------------------------------------------------------------------
    // [0] BCD3000 - VALIDADA NO HARDWARE.
    //
    // Tudo nesta entrada foi medido no aparelho do dono: 7min54s de musica com
    // underruns=0 e overruns=0, MIDI e LEDs funcionando, recuperacao de cabo
    // arrancado. Os NOMES DE CANAL, em particular, custaram tres testes cruzados:
    //
    //  - saidas: com o driver embutido do Windows, canais 1 e 2 = Master,
    //    canais 3 e 4 = fone;
    //  - entradas 1 e 2: um fio no pino central do conector A L saturou o ch1
    //    (32768, fundo de escala), com vazamento decrescente em ch2 > ch3 > ch4 -
    //    a hierarquia esperada, sendo ch2 o par estereo do mesmo pre-amplificador;
    //  - entradas 3 e 4: a chave PHONO/LINE da entrada B alterou ch3 e ch4 e nao
    //    tocou em ch1/ch2 (teste isolado, uma variavel).
    //
    // NAO "ARRUME" ESTES NOMES. Eles nao sao rotulo, sao o mapeamento provado; um
    // teste unitario os compara literalmente por causa disto.
    // -----------------------------------------------------------------------
    {
        "BCD3000",                  // model
        0x1397, 0x00BF,             // vid, pid
        "MI_00",                    // functionId
        0x02, 0x83,                 // epPlayback (ISO OUT), epCapture (ISO IN)
        0x81, 0x01,                 // epControls (BULK IN), epLeds (BULK OUT)
        1, 0,                       // altStreaming, altIdle
        0, 1, 2,                    // assocPlayback -> IF1, assocCapture -> IF2,
                                    // assocMidi -> IF3
        kCtrlUsbMidi10,             // controlProtocol
        true,                       // provenOnHardware
        { "Master L", "Master R", "Phones L", "Phones R" },
        { "Phono A L", "Phono A R", "Phono/Line B L", "Phono/Line B R" }
    },

    // -----------------------------------------------------------------------
    // [1] BCD2000 - EXPERIMENTAL. NUNCA RODOU. NINGUEM DESTE PROJETO TEM UMA.
    //
    // Este caminho vai ser publicado sem nunca ter sido executado. E decisao
    // consciente do dono, e a unica forma de faze-lo com responsabilidade e: falha
    // limpa, log que diz exatamente o que nao bateu, e NENHUMA promessa no codigo.
    // E por isso que provenOnHardware e false - o usbdev registra no log que o
    // caminho e experimental no instante em que este perfil casa.
    //
    // O QUE E FATO, e a unica coisa que e (fonte: driver Linux anyc/snd-bcd2000,
    // arquivos midi.c e audio.c):
    //   - VID:PID 1397:00bd;
    //   - audio de saida no EP 0x02 isocrono, entrada no EP 0x83 isocrono;
    //   - formato 16 bits com sinal LE, 4 canais, 44.100 Hz, 8 bytes por frame -
    //     IDENTICO ao da BCD3000, e e por isso que format.h continua sendo
    //     constante do projeto e nao campo de perfil;
    //   - controles no EP 0x81 e LEDs no EP 0x01, como na BCD3000;
    //   - o enquadramento do MIDI e PROPRIETARIO e o aparelho exige uma sequencia
    //     de inicializacao de 52 bytes. Nada disso esta implementado, e e o que
    //     controlProtocol declara.
    //
    // O QUE E SUPOSICAO, herdada da BCD3000 e NAO VERIFICADA. Cada um destes
    // campos e um palpite, e cada um tem um caminho de falha limpa quando o palpite
    // nao se confirmar:
    //   - functionId "MI_00": se a BCD2000 nao expuser as interfaces agrupadas na
    //     funcao 0, a chave do registro nao existe e o diagnostico e "aparelho nao
    //     encontrado no registro";
    //   - altStreaming = 1 / altIdle = 0: se outro alternate setting for o que liga
    //     o streaming, queryPipes() nao acha os endpoints no alt 1 e o motor de
    //     audio para com "endpoints de audio nao encontrados no alternate setting
    //     1"; se achar e o SetCurrentAlternateSetting falhar, sai
    //     "SetCurrentAlternateSetting(1) falhou (erro N)";
    //   - assocPlayback/assocCapture/assocMidi = 0/1/2: se a ordem das interfaces
    //     em WinUsb_GetAssociatedInterface for outra, open() falha com
    //     "GetAssociatedInterface(0|1) falhou" e o audio nao sobe. Pior caso
    //     silencioso: os indices existirem TROCADOS, e nesse caso o
    //     WinUsb_RegisterIsochBuffer ou a primeira transferencia falha - nao ha
    //     como uma interface de captura aceitar um endpoint ISO OUT;
    //   - o wMaxPacketSize NAO esta aqui de proposito: ele e lido do aparelho e
    //     conferido por blockBytesFor(), que ja falha com "wMaxPacketSize=N
    //     incompativel com o bloco de 10 ms" quando nao fecha os 10 ms.
    //
    // OS NOMES DE CANAL SAO GENERICOS, e isto e decisao e nao preguica: o
    // mapeamento de canal da BCD2000 e DESCONHECIDO. Um nome inventado que sugere
    // um conector especifico ("Master L", "Phono A L") seria pior que um nome
    // generico - o usuario ligaria o cabo no lugar errado confiando num rotulo que
    // ninguem verificou.
    // -----------------------------------------------------------------------
    {
        "BCD2000",                  // model
        0x1397, 0x00BD,             // vid, pid
        "MI_00",                    // functionId          - SUPOSICAO
        0x02, 0x83,                 // epPlayback, epCapture
        0x81, 0x01,                 // epControls, epLeds
        1, 0,                       // altStreaming, altIdle - SUPOSICAO
        0, 1, 2,                    // assoc*                - SUPOSICAO
        kCtrlBcd2000Proprietary,    // controlProtocol
        false,                      // provenOnHardware
        { "Out 1", "Out 2", "Out 3", "Out 4" },
        { "In 1", "In 2", "In 3", "In 4" }
    }
};

int profileCount()
{
    return (int)(sizeof(kProfiles) / sizeof(kProfiles[0]));
}

const DeviceProfile* profileAt(int index)
{
    if (index < 0 || index >= profileCount())
        return 0;
    return &kProfiles[index];
}

const DeviceProfile* profileForPid(unsigned short pid)
{
    for (int i = 0; i < profileCount(); i++)
        if (kProfiles[i].pid == pid)
            return &kProfiles[i];
    return 0;
}

const DeviceProfile& defaultProfile()
{
    return kProfiles[0];
}

const char* controlProtocolName(int protocol)
{
    switch (protocol) {
        case kCtrlUsbMidi10:          return "USB-MIDI 1.0 (pacotes de 4 bytes com CIN)";
        case kCtrlBcd2000Proprietary: return "proprietario da BCD2000 (prefixo + tamanho)";
        default:                      return "desconhecido";
    }
}

// A chave e montada, e nao escrita literal, para que o PID viva num lugar so. O
// teste unitario compara o resultado desta funcao, caractere por caractere, com a
// constante literal que existia antes desta tarefa.
bool profileEnumKey(const DeviceProfile& profile, char* out, int size)
{
    if (!out || size <= 0)
        return false;
    out[0] = 0;
    // %04X maiusculo: e assim que o enumerador USB do Windows escreve, e e assim
    // que a constante antiga estava ("VID_1397&PID_00BF&MI_00"). A comparacao do
    // registro e insensivel a caixa, mas o teste que trava esta funcao nao e.
    const int n = _snprintf(out, (size_t)(size - 1),
                            "SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_%04X&PID_%04X&%s",
                            profile.vid, profile.pid, profile.functionId);
    out[size - 1] = 0;
    if (n < 0) {          // _snprintf do MSVC: negativo quando truncou
        out[0] = 0;
        return false;
    }
    return true;
}

// A enum FindStage vive no usbdev.h desde a rodada de correcao das mensagens - ver o
// comentario dela la, e o de findStageMessage() logo abaixo dele.

bool diagnosticFitsAsio(const char* msg)
{
    if (!msg)
        return true;
    return (int)strlen(msg) + 1 <= kAsioErrorMax;
}

int findStageCount()
{
    return kStageFound + 1;
}

// OS BYTES QUE VEM DO REGISTRO NAO ENTRAM CRUS NUMA LINHA DE LOG.
//
// O unico argumento de fora que chega a findStageMessage() e o `detail` do
// kStageBadGuid: o valor que o Zadig gravou em HKLM, e HKLM e editavel por
// administrador. O TIPO ja e conferido na leitura e o COMPRIMENTO ja esta limitado no
// formato (%.90s), entao NAO ha estouro de buffer - isso foi auditado. O que passava
// eram os BYTES.
//
// O caminho de falha concreto: um REG_SZ com '\n' no meio produz LINHAS FORJADAS no
// asio.log, e 90 bytes bastam para escrever uma inteira. A cadeia
// "\n12:00:00.000 usbdev: aberto (IF1 e IF2 obtidas, IF3 obtida)" tem 60 caracteres, e
// e uma das cinco cadeias que os criterios de teste de hardware deste projeto leem
// como EVIDENCIA. Exige administrador, entao e defeito menor - mas o log e a base de
// prova deste projeto, e prova forjavel nao e prova.
//
// A REGRA E POR FAIXA E NAO POR LISTA: tudo fora de 0x20..0x7E vira '.'. Uma lista
// ("trocar \r e \n") deixaria de fora o \t, os codigos de controle de terminal e os
// bytes >= 0x80, que num log lido como UTF-8 saem como sequencia invalida. A faixa e
// FECHADA nos dois extremos: 0x20 (o espaco) e 0x7E (~) FICAM, e trocar um <= por <
// aqui apagaria o espaco de todo valor legitimo.
//
// `out` nunca fica sem terminador, e `in` nulo da cadeia vazia.
static void sanitizeForLog(const char* in, char* out, int size)
{
    if (!out || size <= 0)
        return;
    out[0] = 0;
    if (!in)
        return;
    int n = 0;
    while (in[n] != 0 && n < size - 1) {
        const unsigned char c = (unsigned char)in[n];
        out[n] = (c >= 0x20 && c <= 0x7E) ? (char)c : '.';
        n++;
    }
    out[n] = 0;
}

// A mensagem final da busca, por estagio. Extraida do findPath() para poder ser
// PURA - e o que permite ao teste renderizar todas e medir o tamanho de cada uma.
//
// TODA mensagem daqui cabe em kAsioErrorMax bytes, e isso NAO e coincidencia: e
// requisito, esta travado por test_mensagens_do_host e o motivo esta no usbdev.h.
// Uma mensagem mais longa chega ao dono cortada, e ja chegou uma vez - cortada
// exatamente em cima da causa verdadeira.
//
// ESTA E A FORMA CURTA, a do HOST. A orientacao que nao cabe em 124 bytes nao foi
// jogada fora: ela mora em findStageHint() e chega ao LOG por findStageMessageLong().
// Nao acrescente texto aqui para "melhorar o diagnostico" - acrescente na dica.
bool findStageMessage(int stage, const DeviceProfile& profile, const char* detail,
                      char* out, int size)
{
    if (!out || size <= 0)
        return false;
    out[0] = 0;
    // kStageFound nao tem o que explicar, kStageHardError ja escreveu a mensagem dele
    // no ponto da falha, e valor fora da faixa nao inventa texto.
    if (stage < kStageNoNode || stage >= kStageFound)
        return false;

    switch (stage) {
    case kStageNotPresent:
        // A ORDEM DAS CAUSAS E O CONTEUDO DESTA MENSAGEM, e as duas coisas foram
        // pagas com um incidente real.
        //
        // A versao de antes de 2026-07-29 tinha 217 caracteres e listava "outro
        // programa" primeiro, "o cabo pode estar fora" no meio e o Zadig no fim. O
        // aparelho do dono desapareceu por CABO MAL ENCAIXADO, o host recebeu os 127
        // primeiros caracteres e o log mostrou "outro programa pode estar com o
        // aparelh": a causa errada, com a certa cortada fora. Duas propriedades saem
        // disso e nenhuma redacao nova pode perde-las:
        //
        //  1. cabo PRIMEIRO. E a causa medida, e e a mais barata de conferir;
        //  2. Zadig POR ULTIMO. Refazer o vinculo e a operacao mais perigosa de toda a
        //     instalacao - mandar alguem para la por causa de um conector solto e o pior
        //     desfecho possivel. E porque as causas estao em ordem de probabilidade, um
        //     corte futuro perde o FIM, que e a causa menos provavel.
        //
        // ESTE TEXTO FOI ESCOLHIDO PELO DONO DO PROJETO, palavra por palavra, e o que
        // ele conserta em relacao a versao anterior ("a %s tem WinUSB aqui e nao esta
        // ativa: ...") e de PRODUTO: "tem WinUSB aqui" e telegrafico, "aqui" quer dizer
        // "nesta maquina" e nao parece, "WinUSB" so significa algo para quem lembra do
        // Zadig, e as causas estavam nomeadas como DIAGNOSTICO em vez de ACAO - quem
        // esta com o show comecando precisa de verbo.
        //
        // O QUE "ja funcionou aqui" CARREGA, e e por isso que a frase nao e enfeite: e a
        // informacao diagnostica que separa este estagio do kStageNoInterface - houve
        // vinculo WinUSB nesta maquina contra NUNCA houve. Era o que "tem WinUSB aqui"
        // dizia, e a troca so foi aceitavel porque diz a mesma coisa em portugues comum.
        //
        // TUDO EM ASCII, SEM ACENTO, E ISSO E DECISAO E NAO DESCUIDO: getErrorMessage e
        // `char*` sem codificacao declarada, e um host que renderize como UTF-8 mostraria
        // "nao" acentuado como mojibake exatamente na tela que existe para nao enganar.
        // Nao "consertar" a acentuacao depois.
        //
        // E a mensagem CABE: 116 caracteres, 117 bytes com o terminador, contra os 124
        // do contrato (7 bytes de folga, e o modelo pode chegar a 14 caracteres antes de
        // apertar - test_mensagens_do_host mede isso para cada perfil da tabela).
        //
        // A orientacao que NAO cabe em 124 bytes nao desapareceu: ela mora em
        // findStageHint() e vai para o LOG pela forma longa. Ver o comentario de
        // kDiagnosticLogMax em usbdev.h.
        _snprintf(out, (size_t)(size - 1),
                  "a %s nao responde e ja funcionou aqui. Nesta ordem: "
                  "1) reencaixe o cabo USB 2) feche outro programa/VM 3) Zadig",
                  profile.model);
        break;
    case kStageBadGuid: {
        // DUAS LIMITACOES SOBRE O MESMO ARGUMENTO, e elas defendem coisas diferentes.
        //
        // %.90s e nao %s defende o COMPRIMENTO: `detail` e o valor que o Zadig gravou no
        // registro, e HKLM e editavel por administrador. Sem o limite, um valor de 127
        // caracteres faria esta mensagem passar de 150 e chegar cortada ao host.
        // 27 + 90 = 117 cabe.
        //
        // sanitizeForLog() defende os BYTES, que passavam pelo limite de comprimento sem
        // ser tocados: um '\n' no valor do registro forja uma LINHA no asio.log, e 90
        // bytes bastam. Ver o comentario da funcao, que tem o caminho de falha completo.
        // A ordem importa: sanear ANTES de interpolar, senao o formato ja escreveu os
        // bytes crus.
        //
        // O buffer local e maior que os 90 que o formato usa, de proposito: com 128 a
        // sanitizacao nunca decide nada sobre comprimento - quem decide continua sendo o
        // %.90s -, e as duas defesas ficam independentes uma da outra.
        char safe[128];
        sanitizeForLog(detail, safe, (int)sizeof(safe));
        _snprintf(out, (size_t)(size - 1), "GUID invalido no registro: %.90s", safe);
        break;
    }
    case kStageNoGuid:
        _snprintf(out, (size_t)(size - 1),
                  "DeviceInterfaceGUID ausente (o Zadig aplicou o WinUSB na funcao %s?)",
                  profile.functionId);
        break;
    case kStageNoNode:
        _snprintf(out, (size_t)(size - 1),
                  "aparelho nao encontrado no registro (a %s esta conectada e com "
                  "WinUSB aplicado pelo Zadig?)", profile.model);
        break;
    case kStageNoInterface:
    default:
        // Ha GUID valido no registro e NENHUMA interface registrada para ele,
        // presente ou nao. Nunca houve vinculo com este GUID aqui, e a pergunta
        // antiga - mantida palavra por palavra - e a certa neste caso.
        _snprintf(out, (size_t)(size - 1),
                  "nenhuma interface WinUSB presente (a %s esta conectada?)",
                  profile.model);
        break;
    }
    out[size - 1] = 0;
    return true;
}

// A ORIENTACAO QUE NAO CABE NOS 124 BYTES DO HOST, por estagio.
//
// Ela existe porque a rodada que fez a mensagem caber no contrato do ASIO encurtou
// tambem o que ia para o LOG - de 217 para 117 caracteres - e o log passou a dizer
// MENOS do que dizia sobre o cenario que custou uma hora de investigacao em
// 2026-07-29. Voltam aqui, entre outras coisas, as duas frases que se perderam palavra
// por palavra: "(maquina virtual?)" e "o vinculo do Zadig se perdeu". E volta o que a
// versao curta nao tem espaco para dizer: "3) Zadig" nao diz o que FAZER com o Zadig.
//
// TRES REGRAS, e cada uma fecha um caminho de falha:
//
//  1. LITERAL, sem nenhum especificador de formato. A forma longa e CONCATENADA e nao
//     interpolada, entao um '%' aqui viraria argumento faltando no logWrite do
//     findPath. O teste afirma que nenhuma dica tem '%';
//  2. ASCII imprimivel, o mesmo motivo das mensagens curtas - o teste afirma isso
//     tambem, para as duas formas;
//  3. `default: return 0`, e ISSO E O FALLBACK DECLARADO: um estagio novo que ninguem
//     se lembre de dotar de dica NAO fica sem forma longa, fica com a curta. A lei
//     completa esta no comentario de findStageMessageLong() em usbdev.h, e o teste a
//     verifica para a FAIXA INTEIRA da enum - ou seja, tambem para o estagio que ainda
//     nao existe.
//
// Sem limite de 124 aqui, e nao por descuido: quem consome esta forma e o log, e o log
// nao tem contrato com ninguem. O unico limite e kDiagnosticLogMax, que existe contra
// truncamento SILENCIOSO.
const char* findStageHint(int stage)
{
    switch (stage) {
    case kStageNotPresent:
        return "No log, por extenso: houve vinculo WinUSB deste aparelho NESTA maquina "
               "e a interface nao esta ativa agora. 'outro programa' inclui MAQUINA "
               "VIRTUAL - o arbitrador de USB do VMware pegou este aparelho duas vezes "
               "no mesmo dia, e daqui nao da para separar capturado de desplugado. E "
               "'Zadig' quer dizer: o vinculo do Zadig se perdeu, rodar o Zadig outra "
               "vez e reaplicar o WinUSB - a operacao mais perigosa da instalacao, e "
               "por isso a ultima.";
    case kStageBadGuid:
        return "O valor acima veio do registro, editavel por administrador: 90 "
               "caracteres no maximo, e todo byte fora de 0x20..0x7E trocado por '.' "
               "para nao dar para forjar linha neste log. Se nao parece um GUID entre "
               "chaves, o vinculo do Zadig foi escrito errado.";
    case kStageNoGuid:
        return "O no do aparelho EXISTE no registro, entao o Windows o enumera; falta o "
               "DeviceInterfaceGUID nele. Ou o driver desta funcao nao e o WinUSB, ou o "
               "Zadig foi aplicado em OUTRA funcao do mesmo aparelho - ele e composto, "
               "e a funcao usada aqui e a nomeada acima.";
    case kStageNoNode:
        return "O Windows nao enumera este VID/PID nesta maquina. Nesta ordem: chave de "
               "forca e luzes do painel, encaixe do cabo USB, porta USB 2.0 (o aparelho "
               "e USB 1.1 full speed). Rodar o Zadig NAO ajuda antes disso - ele so "
               "lista o que ja esta enumerado.";
    case kStageNoInterface:
        return "Nunca houve vinculo com este GUID nesta maquina, e e isso que separa "
               "este caso do 'ja funcionou aqui'. Aqui o Zadig NAO e ultimo recurso, e "
               "o passo que falta: rodar o Zadig, escolher o aparelho e aplicar WinUSB "
               "na funcao dele.";
    default:
        // Estagio sem nada a acrescentar - inclusive kStageFound, kStageHardError e
        // qualquer valor fora da faixa, que tambem nao produzem mensagem curta.
        return 0;
    }
}

bool findStageMessageLong(int stage, const DeviceProfile& profile, const char* detail,
                          char* out, int size)
{
    // A curta primeiro, e o retorno dela E o retorno daqui: as duas formas existem ou
    // faltam JUNTAS, e quem zera `out` no caminho de falha e ela.
    if (!findStageMessage(stage, profile, detail, out, size))
        return false;

    const char* const hint = findStageHint(stage);
    if (!hint)
        return true;                    // FALLBACK declarado: sem dica, a longa E a curta

    const int used = (int)strlen(out);
    if (used >= size - 1)
        return true;                    // a curta ja encheu o buffer: nada a acrescentar
    // Concatenacao e nao interpolacao - ver a regra 1 de findStageHint(). O `used` sai de
    // uma cadeia que findStageMessage() garantiu terminada, entao (size - 1 - used) e
    // sempre positivo por causa da guarda acima.
    _snprintf(out + used, (size_t)(size - 1 - used), " %s", hint);
    out[size - 1] = 0;
    return true;
}

// As interfaces REGISTRADAS para este GUID, presentes ou nao. A unica diferenca
// para a chamada do caminho normal e a ausencia de DIGCF_PRESENT, e e ela que
// separa duas situacoes que antes davam a MESMA mensagem:
//
//   registrada e presente  -> e nossa, abre (caminho normal)
//   registrada e ausente   -> o aparelho JA foi vinculado ao WinUSB nesta maquina,
//                             e a interface nao esta ativa agora
//   nunca registrada       -> nunca houve vinculo com este GUID aqui
//
// MEDIDO NESTA MAQUINA, com o arbitrador de USB do VMware segurando o aparelho: o
// registro tem o no de instancia e o registro de classe de interface
// (Control\DeviceClasses\{guid}\...) SEM a subchave #\Control, ou seja registrada e
// nao ativa - exatamente o estagio kStageNotPresent.
static bool interfaceRegistered(const GUID& guid)
{
    HDEVINFO set = SetupDiGetClassDevsA(&guid, 0, 0, DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return false;
    SP_DEVICE_INTERFACE_DATA did;
    did.cbSize = sizeof(did);
    const bool any = SetupDiEnumDeviceInterfaces(set, 0, &guid, 0, &did) ? true : false;
    SetupDiDestroyDeviceInfoList(set);
    return any;
}

UsbDevice::UsbDevice()
    : file_(INVALID_HANDLE_VALUE), base_(0), play_(0), cap_(0), midi_(0), profile_(0)
{
    path_[0] = 0;
    err_[0]  = 0;
}

UsbDevice::~UsbDevice()
{
    close();
}

// err_ e SEMPRE preenchido; logDetails decide apenas se a linha vai para o log.
// Quem chama com false (o laco de tentativas da passagem de bastao) le err_ depois
// por lastError() e escreve UMA linha de resumo.
void UsbDevice::failWith(const char* what, DWORD gle, bool logDetails)
{
    _snprintf(err_, sizeof(err_) - 1, "%s (erro %lu)", what, gle);
    err_[sizeof(err_) - 1] = 0;
    if (logDetails)
        logWrite("usbdev: %s", err_);
}

// GetLastError() e lido na LISTA DE ARGUMENTOS, e nao no corpo do failWith: assim ele
// e a primeira coisa que roda depois da chamada que falhou, sem nenhum _snprintf no
// meio. Ver a nota das duas funcoes no usbdev.h.
void UsbDevice::fail(const char* what, bool logDetails)
{
    failWith(what, GetLastError(), logDetails);
}

// Procura UM perfil: le o DeviceInterfaceGUID que o Zadig gravou no no daquele
// VID/PID e enumera a interface. Devolve um FindStage.
//
// NAO escreve err_ nos estagios de "nao achei" - quem escolhe a mensagem e
// findPath(), depois de ver o que TODOS os perfis produziram. Escreve err_ e
// devolve kStageHardError so nas falhas que nao sao "nao achei": elas abortam a
// busca inteira, e nesses casos o texto de err_ e o log sao letra por letra os
// mesmos de antes desta tarefa.
int UsbDevice::probeProfile(const DeviceProfile& profile, char* detail, int detailSize,
                            bool logDetails)
{
    char enumKey[kEnumKeyMax];
    if (!profileEnumKey(profile, enumKey, (int)sizeof(enumKey))) {
        // Impossivel com a tabela atual (a chave da BCD3000 tem 57 caracteres, contra
        // os 256 de kEnumKeyMax - 199 de folga), e tratado de qualquer forma: um perfil
        // novo com functionId comprido nao pode virar meia chave de registro.
        // O 57 e medido, nao estimado: e o comprimento de
        // "SYSTEM\CurrentControlSet\Enum\USB\VID_1397&PID_00BF&MI_00", que
        // test_perfis_de_aparelho compara caractere por caractere com o resultado de
        // profileEnumKey(). O numero que estava aqui - 61 - nao correspondia a nada.
        _snprintf(err_, sizeof(err_) - 1,
                  "a chave do registro do perfil %s nao cabe em %d bytes",
                  profile.model, (int)sizeof(enumKey));
        err_[sizeof(err_) - 1] = 0;
        if (logDetails)
            logWrite("usbdev: %s", err_);
        return kStageHardError;
    }

    HKEY root;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, enumKey, 0, KEY_READ, &root) != ERROR_SUCCESS)
        return kStageNoNode;

    char guidStr[128];
    guidStr[0] = 0;

    for (DWORD i = 0; guidStr[0] == 0; i++) {
        char sub[256];
        DWORD subLen = sizeof(sub);
        if (RegEnumKeyExA(root, i, sub, &subLen, 0, 0, 0, 0) != ERROR_SUCCESS)
            break;

        char paramKey[600];
        _snprintf(paramKey, sizeof(paramKey) - 1, "%s\\%s\\Device Parameters", enumKey, sub);
        paramKey[sizeof(paramKey) - 1] = 0;

        HKEY dp;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, paramKey, 0, KEY_READ, &dp) != ERROR_SUCCESS)
            continue;

        static const char* names[2] = { "DeviceInterfaceGUIDs", "DeviceInterfaceGUID" };
        for (int n = 0; n < 2 && guidStr[0] == 0; n++) {
            char  val[512];
            DWORD valLen = sizeof(val);
            DWORD type   = 0;
            if (RegQueryValueExA(dp, names[n], 0, &type, (LPBYTE)val, &valLen) == ERROR_SUCCESS &&
                (type == REG_SZ || type == REG_MULTI_SZ)) {
                // REG_MULTI_SZ ou REG_SZ: em ambos, a primeira string serve. O TIPO
                // e conferido, e nao ignorado: `val` nao e inicializado, e um valor
                // de outro tipo (REG_BINARY, REG_DWORD) nao tem terminador nenhum -
                // o strncpy abaixo leria pilha nao inicializada ate os 512 bytes e
                // poria lixo dentro de guidStr. O registro e escrito pelo Zadig,
                // mas HKLM e editavel por administrador e o custo da checagem e uma
                // linha. O caminho de erro ja existe e e o certo: com guidStr vazio,
                // a funcao devolve kStageNoGuid.
                strncpy(guidStr, val, sizeof(guidStr) - 1);
                guidStr[sizeof(guidStr) - 1] = 0;
            }
        }
        RegCloseKey(dp);
    }
    RegCloseKey(root);

    if (guidStr[0] == 0)
        return kStageNoGuid;

    GUID  guid;
    WCHAR wide[128];
    MultiByteToWideChar(CP_ACP, 0, guidStr, -1, wide, 128);
    if (CLSIDFromString(wide, &guid) != NOERROR) {
        _snprintf(detail, (size_t)(detailSize - 1), "%s", guidStr);
        detail[detailSize - 1] = 0;
        return kStageBadGuid;
    }

    HDEVINFO set = SetupDiGetClassDevsA(&guid, 0, 0,
                                        DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        fail("SetupDiGetClassDevs falhou", logDetails);
        return kStageHardError;
    }

    SP_DEVICE_INTERFACE_DATA did;
    did.cbSize = sizeof(did);

    if (!SetupDiEnumDeviceInterfaces(set, 0, &guid, 0, &did)) {
        SetupDiDestroyDeviceInfoList(set);
        // AQUI mora o diagnostico novo. Ver interfaceRegistered().
        return interfaceRegistered(guid) ? kStageNotPresent : kStageNoInterface;
    }

    int stage = kStageHardError;
    DWORD needed = 0;
    SetupDiGetDeviceInterfaceDetailA(set, &did, 0, 0, &needed, 0);
    if (needed > 0 && needed < 4096) {
        // new (std::nothrow) e a decisao SISTEMICA da Tarefa 4, e este era o
        // ultimo `new` cru do DLL: uma std::bad_alloc escapando daqui subiria
        // pelo caminho de COM dentro do processo do software de DJ, que nao tem
        // como tratar excecao de C++ nossa. Codigo de erro sempre e melhor.
        char* raw = new (std::nothrow) char[needed];
        if (!raw) {
            strcpy(err_, "sem memoria para o detalhe da interface");
            if (logDetails)
                logWrite("usbdev: %s", err_);
            SetupDiDestroyDeviceInfoList(set);
            return kStageHardError;
        }
        SP_DEVICE_INTERFACE_DETAIL_DATA_A* det =
            (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)raw;
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        if (SetupDiGetDeviceInterfaceDetailA(set, &did, det, needed, 0, 0)) {
            strncpy(path_, det->DevicePath, sizeof(path_) - 1);
            path_[sizeof(path_) - 1] = 0;
            stage = kStageFound;
        } else {
            fail("SetupDiGetDeviceInterfaceDetail falhou", logDetails);
            stage = kStageHardError;
        }
        delete[] raw;
    } else {
        fail("tamanho invalido no detalhe da interface", logDetails);
        stage = kStageHardError;
    }

    SetupDiDestroyDeviceInfoList(set);
    return stage;
}

// Procura os perfis NA ORDEM DA TABELA e para no primeiro que casar. A BCD3000 e o
// perfil 0: na maquina do dono ela casa na primeira volta e nada mais e consultado.
bool UsbDevice::findPath(bool logDetails)
{
    // profile_ NAO e zerado aqui, e a linha que fazia isso foi REMOVIDA. Ela dava um
    // resultado errado no caso rotineiro: com um perfil ja casado, uma busca seguinte
    // que falhasse (cabo arrancado, outro programa com o aparelho) fazia profile()
    // voltar a defaultProfile() = BCD3000, e numa maquina com BCD2000 o
    // getChannelInfo() passava a anunciar Master/Phones/Phono - os oito nomes que a
    // tabela proibe inventar, por uma porta que contorna o teste que os proibe.
    // Sem a linha, profile_ e escrito num lugar so (o caminho de ACERTO abaixo), o
    // contrato documentado de matchedProfile() passa a ser verdade, e o argumento de
    // seguranca de thread do usbdev.h fica mais forte e nao mais fraco: uma escrita
    // menos.
    int                  bestStage   = kStageNoNode;
    const DeviceProfile* bestProfile = 0;
    char                 bestDetail[160];
    bestDetail[0] = 0;

    for (int i = 0; i < profileCount(); i++) {
        const DeviceProfile* const p = profileAt(i);
        char detail[160];
        detail[0] = 0;

        const int stage = probeProfile(*p, detail, (int)sizeof(detail), logDetails);

        if (stage == kStageHardError)
            return false;          // err_ e o log ja foram escritos por probeProfile

        if (stage == kStageFound) {
            profile_ = p;
            // Registrado SO para perfil nao validado, e de proposito: assim o
            // fluxo de log da BCD3000 - que os testes de hardware leem como
            // evidencia - continua byte a byte o mesmo de antes desta tarefa.
            //
            // E SO com logDetails, que e a segunda guarda e faltava. Esta linha tem
            // 261 caracteres e estava no caminho de SUCESSO sem consultar logDetails,
            // ou seja furava o openQuiet(): numa maquina com BCD2000 e o aparelho
            // ocupado ela sairia 15 vezes por start() falhado (o laco da passagem de
            // bastao), e o host retenta a cada ~60 s. O openQuiet existe exatamente
            // para isso - ele levou 31 linhas por start() falhado para 3.
            //
            // A BCD3000 nao muda uma virgula: a guarda do provenOnHardware ja a
            // excluia, e o perfil 0 tem provenOnHardware = true.
            if (!p->provenOnHardware && logDetails)
                logWrite("usbdev: perfil %s (VID_%04X&PID_%04X) casou - caminho "
                         "EXPERIMENTAL, nunca executado por ninguem deste projeto. "
                         "Alternate settings, indices de "
                         "WinUsb_GetAssociatedInterface e mapeamento de canal sao "
                         "SUPOSICOES herdadas da BCD3000; ver a tabela em "
                         "usbdev.cpp", p->model, p->vid, p->pid);
            return true;
        }

        if (bestProfile == 0 || stage > bestStage) {
            bestStage   = stage;
            bestProfile = p;
            strncpy(bestDetail, detail, sizeof(bestDetail) - 1);
            bestDetail[sizeof(bestDetail) - 1] = 0;
        }
    }

    // ---- NENHUM perfil casou: escolher UMA mensagem ----
    //
    // Antes desta tarefa havia uma pergunta ("a BCD3000 esta conectada?") para dois
    // casos muito diferentes, e ela MENTIA no mais comum deles: o aparelho vinculado
    // nesta maquina e capturado por outro programa - o arbitrador de USB do VMware,
    // duas vezes no mesmo dia na maquina do dono. A pergunta mandava conferir o cabo
    // ou, pior, refazer o vinculo do Zadig, que e a operacao mais perigosa de todo o
    // processo de instalacao.
    //
    // O QUE O SISTEMA REALMENTE PERMITE DISTINGUIR, medido nesta maquina com o
    // arbitrador do VMware segurando o aparelho: o no de instancia PnP aparece com
    // status "ausente" tanto com o cabo fora quanto com o aparelho tomado por uma
    // maquina virtual - DIGCF_PRESENT nao ve nenhum dos dois. Ou seja: "capturado"
    // e "desplugado" NAO sao separaveis daqui. O que E separavel, e o que vale a
    // mensagem nova, e "houve vinculo WinUSB nesta maquina" (interface registrada)
    // contra "nunca houve" (nem registro de interface). Por isso a mensagem do
    // primeiro caso lista as tres causas possiveis em ordem de probabilidade e
    // deixa o Zadig POR ULTIMO, em vez de manda-lo primeiro.
    // O texto de cada estagio mora em findStageMessage(), que e PURA - e e isso que
    // permite ao teste unitario renderizar todas as mensagens e medir o tamanho de
    // cada uma contra o contrato do getErrorMessage do ASIO.
    //
    // bestStage aqui e sempre um estagio de "nao achei" (kStageNoNode..kStageNotPresent):
    // kStageFound e kStageHardError devolvem antes deste ponto. O retorno e checado de
    // qualquer forma, para que uma faixa nova de estagio nao deixe err_ com o texto de
    // uma busca anterior sem ninguem notar.
    const DeviceProfile& p = bestProfile ? *bestProfile : defaultProfile();

    // DUAS FORMAS, DOIS CONSUMIDORES, E CADA UMA NO SEU LUGAR.
    //
    // err_ recebe a CURTA, porque err_ e o que vira lastError() e chega ao software de
    // DJ pelo getErrorMessage(), cujo contrato e de 124 bytes.
    //
    // O LOG recebe a LONGA, porque o log e a ferramenta de diagnostico deste projeto e
    // nao tem contrato com ninguem. A rodada que fez a mensagem caber no host encurtou
    // as duas de uma vez, e o log passou a dizer menos do que dizia sobre o cenario que
    // custou uma hora de investigacao em 2026-07-29.
    //
    // ESTE E O UNICO PONTO DE LOG DA BUSCA, e e por isso que a forma longa nao espalha
    // invariante nenhuma: um estagio novo entra pelo findStageMessage/findStageHint e
    // aparece aqui sem ninguem tocar nesta funcao. A linha continua sendo UMA linha, e o
    // prefixo continua sendo "usbdev: " letra por letra.
    if (!findStageMessage(bestStage, p, bestDetail, err_, (int)sizeof(err_)))
        err_[0] = 0;
    if (logDetails) {
        char longa[kDiagnosticLogMax];
        // Se a longa nao existir (estagio fora da faixa, o mesmo caso em que err_ ficou
        // vazio), cai em err_ e o log fica exatamente como ficava antes desta mudanca.
        if (findStageMessageLong(bestStage, p, bestDetail, longa, (int)sizeof(longa)))
            logWrite("usbdev: %s", longa);
        else
            logWrite("usbdev: %s", err_);
    }
    return false;
}

bool UsbDevice::isPresent()
{
    if (isOpen())
        return true;        // ja e nosso
    err_[0] = 0;
    return findPath(true);  // so consulta registro e SetupAPI, nao abre nada
}

bool UsbDevice::openInternal(bool logDetails)
{
    if (isOpen())
        return true;

    err_[0] = 0;
    // findPath() e refeito em CADA tentativa de proposito, e nao cacheado: o
    // caminho da interface pode mudar quando o WinUSB reenumera o aparelho, que e
    // exatamente o transiente que o laco de tentativas existe para atravessar.
    // Custa uma consulta de registro + uma enumeracao SetupAPI por tentativa.
    if (!findPath(logDetails))
        return false;

    // findPath() acabou de casar um perfil, entao matchedProfile() nao e nulo aqui.
    const DeviceProfile& p = profile();

    if (logDetails)
        logWrite("usbdev: abrindo %s", path_);

    file_ = CreateFileA(path_,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        0, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, 0);
    if (file_ == INVALID_HANDLE_VALUE) {
        fail("CreateFile falhou (outro programa esta usando o aparelho?)", logDetails);
        return false;
    }

    if (!WinUsb_Initialize(file_, &base_)) {
        fail("WinUsb_Initialize falhou", logDetails);
        close();
        return false;
    }
    // Os indices vem do perfil. Para a BCD3000 sao 0, 1 e 2 - os mesmos valores das
    // constantes que existiam aqui -, e as mensagens de erro saem identicas porque
    // o indice e interpolado. Para um perfil experimental, e AQUI que a suposicao
    // sobre a ordem das interfaces falha limpo.
    // O CODIGO DE ERRO E CAPTURADO NA LINHA SEGUINTE A FALHA, e nao lido depois.
    //
    // Estes dois ramos passaram a montar o texto com _snprintf quando o indice virou
    // campo do perfil, e com isso ficou uma chamada do CRT entre a chamada que falhou e
    // a leitura do erro. O CRT do MSVC nao e documentado como preservador do ultimo
    // erro; se ele o sujar, o "(erro N)" perde o sentido - e este e justamente o par de
    // linhas pelo qual o despejo de log de um dono de BCD2000 seria julgado, porque a
    // ordem das interfaces dela e SUPOSICAO herdada da BCD3000.
    char what[128];
    if (!WinUsb_GetAssociatedInterface(base_, p.assocPlayback, &play_)) {
        const DWORD gle = GetLastError();
        _snprintf(what, sizeof(what) - 1,
                  "GetAssociatedInterface(%u) falhou - interface de playback (IF1)",
                  p.assocPlayback);
        what[sizeof(what) - 1] = 0;
        failWith(what, gle, logDetails);
        close();
        return false;
    }
    if (!WinUsb_GetAssociatedInterface(base_, p.assocCapture, &cap_)) {
        const DWORD gle = GetLastError();
        _snprintf(what, sizeof(what) - 1,
                  "GetAssociatedInterface(%u) falhou - interface de captura (IF2)",
                  p.assocCapture);
        what[sizeof(what) - 1] = 0;
        failWith(what, gle, logDetails);
        close();
        return false;
    }

    // IF3 (MIDI) e a UNICA das tres cuja falta NAO e fatal. Falhar aqui e fechar
    // tudo mataria o audio - que e o produto e esta validado no hardware - por
    // causa de uma adicao. Registrar, deixar midi_ = 0 e seguir; MidiBridge
    // checa midiIf() antes de tocar no aparelho.
    //
    // De proposito NAO usa fail(): open() vai devolver true, e deixar uma
    // mensagem em err_ faria device.lastError() mentir para o proximo chamador
    // que consultasse depois de um sucesso.
    //
    // As duas linhas abaixo saem MESMO em modo silencioso, e de proposito: elas
    // acontecem no caminho de SUCESSO, uma vez por abertura, e sao o marcador que
    // os testes de hardware procuram. O que o modo silencioso corta e a repeticao
    // do caminho de falha, nao o registro do que deu certo.
    //
    // Aqui o GetLastError() esta na propria lista de argumentos do logWrite e NAO ha
    // chamada do CRT entre a falha e ele - o outro argumento e a leitura de um campo -,
    // entao este ramo nao tem o problema dos dois de cima e nao precisa da captura.
    if (!WinUsb_GetAssociatedInterface(base_, p.assocMidi, &midi_)) {
        midi_ = 0;
        logWrite("usbdev: GetAssociatedInterface(%u) falhou (erro %lu) - sem "
                 "interface MIDI (IF3); o audio segue, os controles nao",
                 p.assocMidi, GetLastError());
    }

    logWrite("usbdev: aberto (IF1 e IF2 obtidas, IF3 %s)",
             midi_ ? "obtida" : "AUSENTE");
    return true;
}

void UsbDevice::close()
{
    // Ordem inversa da aquisicao: IF3, IF2, IF1, base, arquivo.
    //
    // profile_ NAO e apagado aqui, e e decisao: o caminho de parada e o log ainda
    // querem saber qual modelo era, e apagar criaria uma escrita neste membro fora
    // de findPath() - ou seja, uma segunda fronteira de thread onde hoje nao existe
    // nenhuma. Ele e sobrescrito na proxima busca.
    if (midi_) { WinUsb_Free(midi_); midi_ = 0; }
    if (cap_)  { WinUsb_Free(cap_);  cap_  = 0; }
    if (play_) { WinUsb_Free(play_); play_ = 0; }
    if (base_) { WinUsb_Free(base_); base_ = 0; }
    if (file_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
}

bool UsbDevice::setAlternate(WINUSB_INTERFACE_HANDLE h, unsigned char alt)
{
    if (!h) {
        strcpy(err_, "setAlternate com handle nulo");
        return false;
    }
    if (!WinUsb_SetCurrentAlternateSetting(h, alt)) {
        // Mesma captura dos ramos do GetAssociatedInterface, e por identidade de
        // situacao: aqui tambem ha um _snprintf entre a falha e a leitura do erro. Este
        // ramo e ANTERIOR a tarefa que criou os outros dois - ele foi o precedente que
        // eles seguiram -, e foi corrigido junto porque a correcao e a mesma linha e
        // porque deixar um caso do idioma antigo no arquivo e o que faz a proxima
        // varredura concluir que o idioma antigo e aceitavel.
        const DWORD gle = GetLastError();
        char what[128];
        _snprintf(what, sizeof(what) - 1, "SetCurrentAlternateSetting(%u) falhou", alt);
        what[sizeof(what) - 1] = 0;
        failWith(what, gle, true);
        return false;
    }
    logWrite("usbdev: alternate setting %u aplicado", alt);
    return true;
}

int UsbDevice::queryPipes(WINUSB_INTERFACE_HANDLE h, unsigned char alt,
                          PipeDesc* out, int maxPipes)
{
    if (!h)
        return 0;

    int n = 0;
    for (unsigned char i = 0; n < maxPipes && i < 16; i++) {
        WINUSB_PIPE_INFORMATION pi;
        if (!WinUsb_QueryPipe(h, alt, i, &pi))
            break;
        out[n].id            = pi.PipeId;
        out[n].type          = (int)pi.PipeType;
        out[n].maxPacketSize = (int)pi.MaximumPacketSize;
        out[n].interval      = (int)pi.Interval;
        n++;
    }
    return n;
}

}
