#pragma once

#include "usbdev.h"

namespace bcd {

// PONTE MIDI DO DRIVER - RELE PARA O PROGRAMA DE CONTROLES
//
// POR QUE ESTA PECA NAO CRIA MAIS A PORTA MIDI VIRTUAL (medido tres vezes no
// hardware em 2026-07-29): o software de DJ NAO volta a procurar o controlador
// depois de a porta virtual desaparecer com ele aberto. A prova sao duas linhas
// consecutivas do proprio log desta classe:
//     antes da queda:  midi: ponte parada (controles=734 leds=546 ledErrors=53)
//     apos a queda:    midi: ponte parada (controles=604 leds=0   ledErrors=0)
// Nos 13 s seguintes o driver LEU 604 mensagens de controle e injetou na porta;
// o host mandou ZERO LEDs. O `leds=0` e a prova de que ele nao estava mais
// escutando - a porta foi recriada em 74 ms, na primeira tentativa, e o driver
// ficou falando sozinho.
// Reproduzido com cabo arrancado, com o botao de forca do aparelho E com troca
// de placa de som - que e uma transicao UNICA e limpa. Logo o defeito nao e a
// porta "piscar": e o host desistir do controlador de uma vez.
//
// CONSEQUENCIA, por eliminacao e nao por preferencia: o dono da porta virtual
// passa a ser o BCD3000Bridge.exe, que e processo de vida longa - ele a cria UMA
// VEZ e nunca a fecha. Qualquer desenho em que o driver segure a porta tem de
// devolve-la em algum momento (quando o audio para e o programa de controles
// precisa dela), e esse momento acontece com o software de DJ aberto: manter a
// porta no driver "por mais tempo" so adia o mesmo evento fatal.
//
// O QUE ESTA CLASSE FAZ AGORA: repassa bytes. Le o endpoint dos controles
// (EP 0x81) e manda os pacotes crus pelo canal local; recebe os comandos de LED
// pelo canal e os escreve no endpoint dos LEDs (EP 0x01). Nao sabe nada de
// MIDI - quem converte pacote USB-MIDI em MIDI de fio, e quem descarta
// enchimento, e o UNICO caminho de injecao do lado do bridge. O audio continua
// sem atravessar fronteira de processo, que era o motivo da arquitetura
// anterior; so o MIDI atravessa, e sao 4 bytes por mensagem com folga de
// milissegundos.
//
// E o driver deixou de depender da teVirtualMIDI64.dll por completo: nada de
// LoadLibrary, nada de criacao de porta, nada de repeticao de criacao de porta.

// O CANAL. O BRIDGE E O SERVIDOR E O DRIVER E O CLIENTE, e isso e necessidade
// MEDIDA nesta maquina, nao estilo: o bridge roda SEM elevacao (atalho na pasta
// Inicializar do usuario) e o software de DJ roda COM elevacao - foi ele que
// conseguiu criar objeto no escopo 'Global\', o que exige
// SeCreateGlobalPrivilege. Um processo elevado abre sem problema um objeto
// criado por um nao elevado; o caminho inverso e o que costuma ser barrado.
// Invertidos os papeis, isto funcionaria nesta maquina e falharia na de outra
// pessoa.
//
// O nome tambem e contrato entre as duas linguagens, como os nomes do evento do
// bastao: mudar aqui sem mudar poc/bridge_service.py nao daria erro de
// compilacao em lugar nenhum - so faria os controles ficarem mudos. Travado por
// teste.
//
// RISCO ACEITO, e escrito aqui de proposito para nao ser redescoberto como novo:
// NINGUEM VERIFICA COM QUEM ESTE LADO ESTA FALANDO. Qualquer processo da mesma
// conta pode criar '\\.\pipe\BCD3000MidiRelay' ANTES do bridge e virar o servidor.
// O que ja esta coberto, e por que isto e aceitavel:
//   - IMPERSONACAO: coberta pelo SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION do
//     connect() - um servidor hostil consegue IDENTIFICAR este cliente, nunca AGIR
//     como ele, e este processo roda dentro do software de DJ, que nesta maquina
//     roda elevado. E o unico item com dano serio, e esta fechado.
//   - MEMORIA: a leitura e limitada a kRelayReadBufBytes e truncada em multiplo de
//     kRelayPacketBytes, entao nao existe caminho de estouro nem de desalinhamento.
//   - NEGACAO DO CANAL: um invasor que agarre o nome faz os controles ficarem mudos.
//     E o que ja acontece com o bridge parado, e o audio nao e afetado.
//   - Um invasor que crie o pipe em modo de BYTES faz o SetNamedPipeHandleState do
//     connect() FALHAR, e o canal e recusado na hora: degrada para "sem canal", que
//     e falha segura.
// Sobra a escrita de 4 bytes arbitrarios no endpoint de LED do aparelho - LEDs
// errados, dano cosmetico, e o aparelho e o proprio do usuario.
// ENDURECIMENTO POSSIVEL, se algum dia valer o custo: GetNamedPipeServerProcessId
// aqui no cliente e GetNamedPipeClientProcessId no servidor, comparando com o
// executavel esperado. NAO implementado - amarraria os dois lados a caminhos de
// executavel e nao fecharia nada que hoje cause dano real.
const wchar_t* const kRelayPipeName = L"\\\\.\\pipe\\BCD3000MidiRelay";

// Tamanho FIXO da mensagem nos dois sentidos: o pacote USB-MIDI 1.0, que e
// exatamente o que o endpoint dos controles entrega e o que o endpoint dos LEDs
// consome. Com isso nao ha enquadramento a inventar, e o canal e de modo
// MENSAGEM (nao de bytes), entao uma mensagem nunca chega partida nem colada na
// seguinte - nao existe forma de dessincronizar o fluxo.
const int kRelayPacketBytes = 4;

// Buffer da leitura do canal no lado do driver. O bridge manda UM pacote por
// mensagem, entao 256 bytes sao 64 mensagens de folga. No sentido contrario o
// driver manda UMA transferencia USB por mensagem, e a leitura do EP 0x81 usa
// buffer de 64 bytes - por isso o servidor do outro lado le com 512.
const int kRelayReadBufBytes = 256;

// Passo entre tentativas de conectar ao canal, em REGIME. O bridge pode nao estar
// rodando quando o audio liga (e pode subir depois), entao a conexao NUNCA e
// desistida: falta de canal deixa os controles mudos, e nunca derruba o audio.
const unsigned kRelayRetryMs = 1000;

// RECUO GRADUADO na PARTIDA: as primeiras kRelayFirstTries tentativas usam
// kRelayFirstRetryMs, e so depois o passo sobe para kRelayRetryMs.
//
// Nao e enfeite de latencia, e o motivo e uma coisa medida: quando o aparelho cai
// com o software de DJ aberto, ele responde com uma TEMPESTADE DE RESET - destroi e
// recria o objeto COM do driver umas 15 vezes em 8 s, ou seja um par stop/start a
// cada ~0,5 a 1 s. Com um passo unico de 1 s, uma instancia que nasca dentro dessa
// janela pode ser destruida ANTES da primeira tentativa de conexao, e no log do
// portao isso aparece como 'semCanal > 0' com 'conexoes=0' em algumas linhas
// 'ponte parada' - que nao e defeito nenhum, mas confunde a leitura da evidencia
// justo no passo do teste que reproduz o defeito original.
//
// Por que nao 100 ms para sempre: sem bridge rodando, a tentativa e um CreateFileW
// que falha - barato, mas o log e o laco nao precisam disso a cada 100 ms pelo resto
// de um set de horas. Dez tentativas curtas cobrem ~1 s de partida, que e a ordem de
// grandeza do arranque do bridge, e depois o custo volta a ser o de regime.
const unsigned kRelayFirstRetryMs = 100;
const unsigned kRelayFirstTries   = 10;

// Prazo da escrita no canal. O thread que le o USB nao pode ficar preso
// escrevendo se o outro lado estiver lento ou morto: estourado o prazo, a
// escrita e cancelada, o canal e considerado quebrado e a reconexao entra.
const unsigned kRelayWriteMs = 250;

// Prazo para uma I/O CANCELADA assentar. Cancelar nao e sincrono: enquanto o
// kernel nao devolver a operacao, o OVERLAPPED e o buffer nao podem ser
// liberados. Estourado este prazo, a memoria e VAZADA de proposito - o mesmo
// criterio do motor de audio, que prefere vazar a liberar recurso em uso.
const unsigned kRelayDrainMs = 1000;

// Prazo da leitura dos controles e da escrita de LED, em politica de pipe do
// WinUSB.
//
// O prazo da ESCRITA e novo e nao e enfeite: agora a escrita de LED roda no
// MESMO thread que le os controles, entao uma escrita sem prazo nao atrasaria
// apenas o LED - pararia os controles. Fecha de passagem a lacuna registrada no
// ledger (o EP 0x01 era o unico dos dois endpoints sem prazo formal, e por isso
// o orcamento da passagem de bastao nao tinha limite superior provado).
const unsigned kCtrlReadTimeoutMs = 300;
const unsigned kLedWriteTimeoutMs = 100;

// Quanto o stop() espera pelo thread antes de declara-lo travado.
const unsigned kStopWaitMs = 2000;

// Canal local de 4 bytes com o BCD3000Bridge.exe, do lado CLIENTE.
//
// Peca separada de proposito: ela nao toca em USB nem em MIDI, e por isso e
// testavel por unidade contra um named pipe de verdade, com um servidor falso no
// proprio processo de teste. E o unico jeito de exercitar o codigo que roda
// dentro do software de DJ sem hardware nenhum.
//
// TODO objeto desta classe pertence a UM SO thread - o da ponte. O thread do
// host (start()/stop()) nunca a toca. Foi a decisao de projeto mais importante
// aqui: os dois defeitos de ciclo de vida que esta sessao achou nesta area
// nasceram na fronteira entre o thread do host e o thread da ponte, e um segundo
// dono do handle do canal criaria uma segunda fronteira igual.
//
// CONTRATO DE DESTRUICAO: nao destruir um RelayLink sem que close() tenha
// devolvido true. Devolver false significa que ha I/O que o kernel ainda pode
// completar sobre ovRx_/ovTx_, e apagar o objeto nesse estado entrega memoria
// morta ao driver. O unico ponto de destruicao no driver respeita isso e vaza o
// objeto quando preciso - e nesse caminho ele chama abandon(), que fecha o handle
// SEM liberar a memoria. Ver o comentario de abandon(): o que protege o OVERLAPPED
// e o vazamento da MEMORIA, nao o handle aberto, e deixar o handle aberto TRAVA o
// servidor do outro lado.
class RelayLink {
public:
    RelayLink();
    ~RelayLink();

    bool isConnected() const { return pipe_ != INVALID_HANDLE_VALUE; }

    // Tenta conectar ao servidor. false quando NAO HA servidor - o bridge nao
    // esta rodando -, que e situacao normal e nao erro: o chamador repete mais
    // tarde. O codigo de erro sai em *errOut.
    bool connect(DWORD* errOut);

    // Resolve a I/O pendente e fecha o canal. Devolve false se alguma operacao
    // NAO pode ser resolvida no prazo: nesse caso o handle NAO e fechado e o
    // objeto nao pode ser destruido nem reutilizado.
    bool close();

    // Fecha o handle do canal SEM CONDICAO e sem tocar na memoria. Para uso
    // EXCLUSIVO no caminho do vazamento deliberado, depois de close() devolver
    // false, e nunca antes.
    //
    // POR QUE ISTO E SEGURO: quem protege o ovRx_/ovTx_ de uma I/O que o kernel
    // ainda pode completar e o VAZAMENTO DA MEMORIA, nao o handle aberto. Um
    // CloseHandle sobre handle com I/O pendente faz o kernel completar ou cancelar
    // a operacao; o OVERLAPPED continua endereco valido justamente porque a memoria
    // foi vazada de proposito e nunca sera reciclada.
    //
    // POR QUE ISTO E NECESSARIO: o servidor do outro lado e criado com
    // nMaxInstances = 1 e so devolve a instancia quando Peek/Read/Write FALHA. Com
    // este handle ainda aberto nada falha - o PeekNamedPipe dele sucede com 0 bytes
    // disponiveis e a escrita de LED enche os 4096 bytes do buffer e BLOQUEIA PARA
    // SEMPRE (o pipe dele e PIPE_WAIT e sem sobreposicao). Como o mesmo thread do
    // bridge faz os dois sentidos, o caminho dos CONTROLES morre junto, e a
    // recuperacao so vem quando o processo do software de DJ morrer. Ou seja: vazar
    // o handle troca um vazamento de 464 bytes por um travamento permanente do
    // programa de controles.
    void abandon();

    // Envia UMA mensagem. `bytes` tem de ser multiplo de kRelayPacketBytes.
    // false = canal quebrado (o chamador fecha e reconecta). `stopEvent` pode
    // ser 0; quando dado, encurta a espera se a parada for pedida.
    bool send(const unsigned char* data, DWORD bytes, HANDLE stopEvent, DWORD* errOut);

    // Deixa uma leitura pendente. O evento serve para o WaitForMultipleObjects
    // do chamador. Idempotente: com leitura ja pendente, devolve true sem fazer
    // nada. false = canal quebrado.
    bool   armRead(DWORD* errOut);
    HANDLE readEvent() const  { return rxEvent_; }
    bool   readPending() const { return rxPending_; }

    // Colhe a leitura pendente. Devolve quantos bytes UTEIS chegaram (sempre
    // multiplo de kRelayPacketBytes; o resto de uma mensagem mal formada e
    // descartado com ela, porque em modo mensagem nao existe fluxo a
    // ressincronizar), ou -1 se o canal quebrou.
    int    finishRead(DWORD* errOut);

    // Nao e const de proposito: o WinUsb_WritePipe pede PUCHAR, e converter
    // const away no ponto de chamada esconderia o fato de que este buffer e
    // memoria mutavel de trabalho do thread da ponte.
    unsigned char* readBuffer() { return rxBuf_; }

private:
    bool ensureEvents(DWORD* errOut);

    HANDLE     pipe_;
    HANDLE     rxEvent_;
    HANDLE     txEvent_;
    OVERLAPPED ovRx_;
    OVERLAPPED ovTx_;
    bool       rxPending_;
    // Uma escrita cancelada que NAO assentou no prazo. Enquanto isto for true, o
    // handle nao pode ser fechado nem o objeto destruido.
    bool       txStuck_;
    unsigned char rxBuf_[kRelayReadBufBytes];

    // Sem copia: o objeto e dono de handles.
    RelayLink(const RelayLink&);
    RelayLink& operator=(const RelayLink&);
};

// ESTA PONTE SABE REPASSAR ESTE APARELHO? Predicado PURO sobre o perfil - nao
// toca em USB, nem em registro, nem em thread.
//
// Existe separado do start() de proposito: e a DECISAO, e nao apenas o dado, que
// precisa de teste. Um teste que so conferisse `profile.controlProtocol !=
// kCtrlUsbMidi10` na tabela nao pegaria um start() que esquecesse de olhar o campo -
// e o modo de falha desse esquecimento e a ponte ler enquadramento proprietario como
// se fosse USB-MIDI e injetar mensagens aleatorias na porta MIDI do usuario.
//
// Nenhuma maquina deste projeto tem uma BCD2000 para exercitar o caminho de verdade;
// este predicado e a unica coisa aqui que da para provar sem o aparelho, e por isso
// ele existe.
bool bridgeSupportsProfile(const DeviceProfile& profile);

// Ponte de controles e LEDs. UM thread faz os dois sentidos, e e ele quem e dono
// de todo recurso de I/O que cria.
//
// Nao tem nada a ver com o audio: usa outra interface USB (IF3) e outro tipo de
// transferencia (bulk, nao isocrono). Por isso nao disputa banda com o audio nem
// interfere no relogio.
class MidiBridge {
public:
    MidiBridge();
    ~MidiBridge();

    // Falhar aqui e situacao PREVISTA, e o chamador NAO deve tratar como erro
    // fatal: o aparelho pode nao ter entregue a IF3. Registrar lastError() e
    // seguir com o audio.
    //
    // O bridge NAO estar rodando nao e falha de start(): o thread sobe, tenta
    // conectar em laco, e o MIDI passa a funcionar sozinho quando o bridge subir.
    bool start(UsbDevice* dev);

    // Idempotente e seguro sem start(): tanto o dono quanto o destrutor chamam.
    //
    // Continua tendo de rodar ANTES de o aparelho ser fechado: o thread escreve
    // LED no aparelho, e o AbortPipe daqui e o que encurta a saida dele.
    void stop();

    bool        isRunning() const { return running_; }
    const char* lastError() const { return err_; }

    // Pacotes de 4 bytes lidos do EP 0x81 e ENTREGUES ao canal. Conta o cru,
    // enchimento incluido: o filtro mora do outro lado, num caminho unico.
    unsigned packetsForwarded() const { return packets_; }
    // Pacotes de LED recebidos do canal e escritos no EP 0x01.
    unsigned ledsForwarded() const    { return leds_; }
    unsigned ledErrors() const        { return ledErrors_; }
    // Pacotes de controle jogados fora por nao haver canal (bridge parado).
    unsigned packetsDropped() const   { return dropped_; }
    unsigned relayConnects() const    { return connects_; }

private:
    static DWORD WINAPI threadEntry(void* self);
    void   threadMain();

    // volatile no PONTEIRO, nao no apontado: dev_ e escrito pelo thread do host
    // (start()/stop()) e lido pelo thread da ponte. Quem le tira UMA copia local
    // e trabalha sobre ela - duas leituras do mesmo membro volatile podem
    // devolver valores diferentes.
    UsbDevice* volatile dev_;
    // thread_ e stopEvent_ NAO sao volatile, de proposito: sao tocados somente
    // pelo thread do host - start(), stop() e o destrutor. O thread da ponte tira
    // uma copia do evento na entrada e nunca le o membro de novo.
    HANDLE        thread_;
    HANDLE        stopEvent_;
    volatile bool running_;
    volatile bool stopRequested_;
    // O thread nao saiu no prazo do stop() e pode estar vivo em algum lugar.
    // Nesse estado dev_ NAO e zerado (o stop() seguinte precisa dele para repetir
    // o AbortPipe), o handle do thread e o evento de parada sao MANTIDOS ABERTOS
    // de proposito - o handle e a unica forma de descobrir depois que ele saiu, e
    // o evento e o que ele espera - e um start() seguinte e recusado enquanto ele
    // nao sair: dois leitores disputando o mesmo pipe entregariam as mensagens do
    // controlador fora de ordem ou pela metade.
    // O estado e REVERSIVEL: start() reavalia sem bloquear e, se o thread ja
    // saiu (o caso comum - device.close() faz a leitura pendente falhar), a ponte
    // sobe de novo. Irreversivel, um transiente de 2 s deixaria o controlador de
    // DJ sem botoes pelo resto da vida desta instancia do driver.
    volatile bool threadStuck_;
    char          err_[256];
    volatile unsigned packets_;
    volatile unsigned leds_;
    // Falhas de escrita de LED. Contador PROPRIO, e nao leds_: leds_ conta
    // sucesso e CONGELA durante uma sequencia de falhas, o que transformava a
    // guarda de volume do log numa constante (ou logava toda falha para sempre,
    // ou nenhuma). Incrementado ANTES do teste, como inStarves_ no audioengine.
    volatile unsigned ledErrors_;
    volatile unsigned dropped_;
    volatile unsigned connects_;
};

}
