#pragma once

namespace bcd {

// Passagem de bastao entre o driver ASIO e o BCD3000Bridge.exe.
//
// O aparelho so aceita um processo por vez (medido no hardware: com o bridge
// rodando, CreateFile devolve erro 5). Quando o software de DJ liga o audio, o
// driver precisa do aparelho INTEIRO - IF1, IF2 e IF3 -, e o programa de
// controles tem de sair de cena e voltar quando o driver soltar.
//
// SO O APARELHO passa de mao. A porta MIDI virtual "BCD3000" NAO entra nesta
// negociacao: ela e propriedade permanente do BCD3000Bridge.exe, criada uma vez e
// nunca fechada. Ela ja entrou nesta negociacao, e foi exatamente isso que
// produziu o defeito que a arquitetura atual existe para consertar - o software de
// DJ nao volta a procurar o controlador depois de a porta desaparecer com ele
// aberto. Ver o topo de native/bcdasio/midibridge.h, com a medicao.
//
// A sinalizacao e um evento nomeado do Windows com um unico sentido de
// informacao: "o driver quer o aparelho". Quem manda e o driver; o bridge
// apenas obedece e fica tentando reconectar. Nao ha negociacao nem confirmacao
// de volta, e e justamente essa assimetria que torna a coisa robusta: se o
// driver morrer sem devolver, o Windows destroi o evento junto com o processo, o
// bridge deixa de ver o pedido e retoma o aparelho sozinho pelo laco de
// reconexao que ele JA tem (medido: 0,84 s). Nao existe estado preso em disco
// nem registro para limpar.
//
// A propriedade acima depende de o bridge NAO guardar um handle aberto do
// evento: um handle vivo do lado dele manteria o objeto existindo, e ainda
// sinalizado, depois da morte do driver. Esta escrito la, em
// poc/bridge_service.py (driver_quer_aparelho), e nao e otimizavel.

// Os DOIS nomes do evento. O driver cria os DOIS - nao "o primeiro que
// conseguir" - e sinaliza os dois; o lado Python (poc/bridge_service.py,
// driver_quer_aparelho) faz um OU sobre os dois. Como os dois lados cobrem os
// dois nomes, a convergencia deixou de depender de ordem, e com isso deixou de
// depender de ELEVACAO. Os nomes seguem congelados por teste, pelo mesmo motivo que
// kRelayPipeName: mudar um nome nao quebraria compilacao em lugar nenhum, so faria
// os controles morrerem quando o audio liga.
//
// Por que os dois escopos, e nao um:
//   local  - namespace de objetos da SESSAO do Windows. Qualquer processo da
//            mesma sessao interativa cria e abre, elevado ou nao. E este que faz
//            a passagem de bastao funcionar hoje.
//   Global - atravessa sessoes do Windows. Criar objeto nele exige o privilegio
//            SeCreateGlobalPrivilege, que um processo de usuario comum NAO tem.
//            Serve para o cenario futuro de um bridge rodando como servico
//            (sessao 0), em que o nome local nao e compartilhado.
//
// MEDIDO NESTA MAQUINA, e e o motivo desta forma: com o software de DJ iniciado
// de um shell ELEVADO, o log do driver trouxe
// "bastao: evento criado em 'Global\BCD3000_DriverWantsDevice'". O
// BCD3000Bridge.exe iniciado pelo Windows no login roda SEM elevacao, e pode nao
// ter permissao de abrir um objeto criado pelo token elevado (o SID de
// administradores entra como negado-apenas no token filtrado). Se o driver
// criasse apenas no primeiro nome que conseguisse, o par ficaria Global/nada e a
// passagem NUNCA aconteceria - sem erro em lugar nenhum, so os controles mudos.
// Criando SEMPRE o local, um bridge da mesma sessao encontra o pedido de
// qualquer forma, e o Global continua disponivel para o cenario de servico.
const char* const kEventName      = "Global\\BCD3000_DriverWantsDevice";
const char* const kEventNameLocal = "BCD3000_DriverWantsDevice";

// Quantas vezes tentar tomar o aparelho, que o bridge pode ainda estar soltando,
// e o intervalo entre tentativas. Usado em UM lugar: o device.open() da casca
// ASIO. Ja foi usado em dois - a criacao da porta virtual na ponte MIDI era o
// outro -, e esse segundo uso desapareceu junto com a porta do lado do driver.
//
// A alternativa - um Sleep fixo antes da primeira tentativa - e pior nos dois
// extremos: custa o prazo inteiro mesmo quando nao ha bridge nenhum para soltar
// o aparelho, e nao garante nada quando o bridge demora mais que o prazo. Tentar
// em intervalos curtos se AUTO-TEMPORIZA: sem bridge, a primeira tentativa pega
// e nao se paga nada; com bridge, para na tentativa em que ele soltou.
//
// O passo de 200 ms nao e maior que a granularidade do outro lado: o bridge
// consulta o pedido uma vez por volta do laco de leitura dos controles, e essa
// leitura tem prazo de 400 ms (WinUsb_SetPipePolicy em poc/winusb_bcd.py), mais
// o tempo de fechar os handles. O teto de 2,8 s da varias vezes essa folga - e sao
// 2.800 ms, e nao os 3.000 de 15 x 200 que a conta ingenua sugere, porque a PRIMEIRA
// tentativa nao dorme: o Sleep so entra a partir da segunda, entao a espera total e
// (tentativas - 1) x 200 ms (ver bcdasio.cpp, no laco do start()).
// O lado Python tambem nao dorme um prazo inteiro segurando recurso: os
// sonos do laco dele sao fatiados no MESMO passo de 200 ms e reavaliam o pedido a
// cada fatia (dormir_atento).
//
// O ORCAMENTO, TERMO POR TERMO, e onde ele ainda depende de suposicao:
//   1. DETECCAO do pedido:            <= 400 ms  (prazo do EP 0x81 no lado Python;
//                                                 o bridge consulta o pedido uma vez
//                                                 por volta do laco de leitura)
//   2. ESPERA PELA TRAVA DE LED:      <= 100 ms  (prazo do EP 0x01, posto em
//                                                 open_dev_full - ver abaixo)
//   3. FECHAR TRES HANDLES:           ~0 ms      (WinUsb_Free x2 + CloseHandle)
//   TOTAL ~500 ms contra o teto de 15 tentativas x 200 ms = 2,8 s => ~5,6x de folga.
//
// O termo 2 e o que precisa de honestidade, porque uma ressalva verdadeira ja foi
// apagada deste arquivo uma vez: soltar_aparelho() ESPERA pela trava _led_lock, e o
// callback de LED do bridge segura essa trava ATRAVESSANDO a escrita no EP 0x01.
// Ate a rodada de correcao da Tarefa 10 esse endpoint nao tinha prazo NENHUM do lado
// Python - so o lado C++ punha 100 ms (kLedWriteTimeoutMs) no mesmo endpoint -, e o
// termo sem limite superior que o desaparecimento da porta virtual tirou da conta
// simplesmente MUDOU DE LUGAR, do fechamento da porta para a escrita de LED. Hoje o
// open_dev_full() de poc/winusb_bcd.py poe os MESMOS 100 ms nesse endpoint, com a
// simetria escrita nos dois arquivos.
//
// O que continua sendo suposicao, e nao fato provado: threading.Lock do CPython nao
// promete JUSTICA. Com uma rajada de VU (medido: ~46 LEDs/s) o termo 2 e "uma escrita
// em voo" apenas se quem espera receber a trava na proxima liberacao. Na pratica o
// GIL entrega, e a folga de 5,6x absorve varias falhas de vez; mas isto e argumento
// de probabilidade, nao limite superior formal. O desfecho de estourar o teto tambem
// nao e catastrofe: o driver falha em tomar o aparelho, o audio nao sobe, e o
// software de DJ retenta (o VirtualDJ, a cada ~60 s).
const int      kHandoffTries   = 15;
const unsigned kHandoffRetryMs = 200;

// Sinaliza que queremos o aparelho e VOLTA NA HORA - nao dorme. Sinaliza TODOS
// os escopos que existirem, nunca so um: um nome existindo e nao sinalizado faria
// um bridge que olhasse justamente esse nome ler "nenhum pedido".
//
// Devolve false somente se nao houve como sinalizar (nenhum dos dois eventos pode
// ser criado). O chamador deve registrar e SEGUIR: o audio e o produto principal,
// e sem sinalizacao ele ainda funciona em toda maquina onde o bridge nao esteja
// segurando o aparelho.
//
// Quem espera pelo bridge e o chamador, tentando abrir o aparelho algumas vezes
// com kHandoffRetryMs de intervalo.
bool requestDevice();

// Devolve o aparelho ao bridge, zerando TODOS os escopos criados. Seguro chamar
// sem ter pedido, e seguro repetir: a casca ASIO chama no stop(), no destrutor e
// no caminho de erro do start().
void releaseDevice();

}
